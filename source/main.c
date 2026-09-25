/* main.c -- Pocket Crystal League (GameMaker) Switch wrapper entry point
 *
 * Loads the Android ARM64 libyoyo.so, wires its imports to native shims, and
 * drives the GameMaker JNI lifecycle the way the APK's DemoRenderer does.
 *
 * Based on POINPY NX (see LICENSE / README for the full lineage).
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <switch.h>
#include <SDL2/SDL.h>

#include "config.h"
#include "util.h"
#include "libc_shim.h"
#include "error.h"
#include "so_util.h"
#include "imports.h"
#include "jni_fake.h"
#include "asset.h"
#include "gfx.h"
#include "opensles.h"
#include "prefs.h"
#include "extensions.h"
#include "display.h"
#include "nx_pointer.h"
#include "input.h"
#include "paths.h"
#include "apkfile.h"
#include <dirent.h>
#include <strings.h>

#define RUNNER_ENTRY "lib/arm64-v8a/libyoyo.so"
#define DATA_ENTRY   "assets/game.droid"

static char s_so_file[640];    // absolute path of libyoyo.so
static char s_apk_file[640];   // absolute path of the APK (any name)

// The runner's key-down table (exported _IO_KeyDown[256], read by keyboard_check).
static volatile uint8_t *s_io_keydown = NULL;

static void *heap_so_base = NULL;
static size_t heap_so_limit = 0;

so_module game_mod;  // libyoyo.so

// provide a replacement heap init so the newlib heap is separate from the .so
void __libnx_initheap(void) {
  void *addr;
  size_t size = 0, fake_heap_size = 0;
  size_t mem_available = 0, mem_used = 0;

  if (envHasHeapOverride()) {
    addr = envGetHeapOverrideAddr();
    size = envGetHeapOverrideSize();
  } else {
    svcGetInfo(&mem_available, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&mem_used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    if (mem_available > mem_used + 0x200000)
      size = (mem_available - mem_used - 0x200000) & ~0x1FFFFF;
    if (size == 0)
      size = 0x2000000 * 16;
    Result rc = svcSetHeapSize(&addr, size);
    if (R_FAILED(rc))
      diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed));
  }

  // The newlib heap backs runner allocations, the inflated game.droid and
  // Mesa/nouveau GPU memory. Reserve only a fixed slice for the libyoyo image.
  size_t so_reserve = (size_t)SO_HEAP_RESERVE_MB * 1024 * 1024;
  if (so_reserve > size / 2)
    so_reserve = size / 2;
  fake_heap_size = size - so_reserve;

  extern char *fake_heap_start;
  extern char *fake_heap_end;
  fake_heap_start = (char *)addr;
  fake_heap_end   = (char *)addr + fake_heap_size;

  heap_so_base = (char *)addr + fake_heap_size;
  heap_so_base = (void *)ALIGN_MEM((uintptr_t)heap_so_base, 0x1000);
  heap_so_limit = (char *)addr + size - (char *)heap_so_base;
}

static void check_syscalls(void) {
  if (!envIsSyscallHinted(0x77)) fatal_error("svcMapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x78)) fatal_error("svcUnmapProcessCodeMemory is unavailable.");
  if (!envIsSyscallHinted(0x73)) fatal_error("svcSetProcessMemoryPermission is unavailable.");
  if (envGetOwnProcessHandle() == INVALID_HANDLE) fatal_error("Own process handle is unavailable.");
}

static int file_exists(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

// The APK may keep whatever name it was downloaded with. Prefer game.apk; else
// the first *.apk in the folder that actually contains the game data.
static int find_apk(char *out, size_t n) {
  if (paths_join(out, n, APK_NAME) == 0 && file_exists(out)) return 1;
  DIR *d = opendir(paths_base());
  if (!d) return 0;
  int found = 0;
  struct dirent *e;
  while (!found && (e = readdir(d)) != NULL) {
    const size_t l = strlen(e->d_name);
    if (l < 5 || strcasecmp(e->d_name + l - 4, ".apk") != 0) continue;
    if (paths_join(out, n, e->d_name) != 0 || !file_exists(out)) continue;
    if (apk_has_entry(out, DATA_ENTRY)) found = 1;
  }
  closedir(d);
  return found;
}

// The runner opens the APK with its own zip reader and inflates
// assets/game.droid out of it; the loose assets/ folder is only a fallback.
static void check_data(void) {
  if (!find_apk(s_apk_file, sizeof(s_apk_file)))
    fatal_error("No Pocket Crystal League APK found in\n  %s\n\n"
                "Copy the arm64 Android APK into that\nfolder (any file name ending in .apk).\n"
                "An .xapk/.apks bundle must be unpacked\nfirst: use the base APK inside it.",
                paths_base());

  struct stat st;
  stat(s_apk_file, &st);
  FILE *f = fopen(s_apk_file, "rb");
  unsigned char magic[4] = { 0 };
  if (f) { fread(magic, 1, 4, f); fclose(f); }
  if (st.st_size < 1024 * 1024 || magic[0] != 'P' || magic[1] != 'K')
    fatal_error("%s\nis not a valid APK (%lld bytes).", s_apk_file, (long long)st.st_size);
  if (!apk_has_entry(s_apk_file, DATA_ENTRY))
    fatal_error("%s\ndoes not contain %s.\nIs this the Pocket Crystal League APK?", s_apk_file, DATA_ENTRY);

  // libyoyo.so: use the one in the folder, or pull it out of the APK once.
  paths_join(s_so_file, sizeof(s_so_file), SO_NAME);
  if (!file_exists(s_so_file)) {
    if (!apk_has_entry(s_apk_file, RUNNER_ENTRY))
      fatal_error("The APK has no 64-bit runner\n(%s).\nUse the arm64 build of the game.", RUNNER_ENTRY);
    debugPrintf("pcl_nx: extracting %s from the APK\n", RUNNER_ENTRY);
    const int rc = apk_extract(s_apk_file, RUNNER_ENTRY, s_so_file);
    if (rc != 0)
      fatal_error("Could not extract libyoyo.so from\n%s\n(error %d). Is the SD card full\nor read-only?", s_apk_file, rc);
  }

  // Only the 64-bit runner can load here.
  f = fopen(s_so_file, "rb");
  unsigned char ehdr[20] = { 0 };
  if (f) { fread(ehdr, 1, sizeof(ehdr), f); fclose(f); }
  if (memcmp(ehdr, "\x7f" "ELF", 4) != 0 || ehdr[4] != 2 || ehdr[18] != 0xB7)
    fatal_error("%s is not an ARM64 ELF.\nDelete it (it will be re-extracted from\nthe APK) or take lib/arm64-v8a/libyoyo.so.", s_so_file);

  char assets[640];
  paths_join(assets, sizeof(assets), ASSETS_DIR);
  debugPrintf("pcl_nx: apk=%s (%lld bytes), runner=%s, assets/ %s\n", s_apk_file,
              (long long)st.st_size, s_so_file, stat(assets, &st) == 0 ? "present" : "absent (not required)");
}

static void set_screen_size(void) {
  if (config.resolution == 720)       { screen_width = 1280; screen_height = 720; }
  else if (config.resolution == 1080) { screen_width = 1920; screen_height = 1080; }
  else if (appletGetOperationMode() == AppletOperationMode_Console) {
    screen_width = 1920; screen_height = 1080;
  } else {
    screen_width = 1280; screen_height = 720;
  }
}

// ---------------------------------------------------------------------------
// EGL / GLES2 context on the default NWindow
// ---------------------------------------------------------------------------

static EGLDisplay s_display = EGL_NO_DISPLAY;
static EGLContext s_context = EGL_NO_CONTEXT;
static EGLSurface s_surface = EGL_NO_SURFACE;

static int egl_init(void) {
  s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (!s_display) { debugPrintf("egl: no display\n"); return 0; }
  eglInitialize(s_display, NULL, NULL);
  if (!eglBindAPI(EGL_OPENGL_ES_API)) { debugPrintf("egl: bindAPI failed\n"); return 0; }

  // the runner's requested GL attributes: RGBA8888, depth24, stencil8, no MSAA
  const EGLint cfg_attr[] = {
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_NONE
  };
  EGLConfig cfg;
  EGLint num = 0;
  if (!eglChooseConfig(s_display, cfg_attr, &cfg, 1, &num) || num < 1) {
    debugPrintf("egl: no config\n");
    return 0;
  }

  NWindow *win = nwindowGetDefault();
  nwindowSetDimensions(win, screen_width, screen_height);
  s_surface = eglCreateWindowSurface(s_display, cfg, win, NULL);
  if (!s_surface) { debugPrintf("egl: no surface\n"); return 0; }

  const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  s_context = eglCreateContext(s_display, cfg, EGL_NO_CONTEXT, ctx_attr);
  if (!s_context) { debugPrintf("egl: no context\n"); return 0; }

  eglMakeCurrent(s_display, s_surface, s_surface, s_context);
  eglSwapInterval(s_display, config.vsync ? 1 : 0);
  return 1;
}

static void egl_deinit(void) {
  if (s_display == EGL_NO_DISPLAY) return;
  eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (s_context) eglDestroyContext(s_display, s_context);
  if (s_surface) eglDestroySurface(s_display, s_surface);
  eglTerminate(s_display);
  s_display = EGL_NO_DISPLAY;
}

// ---------------------------------------------------------------------------
// GameMaker runner entry points (exported by libyoyo.so)
//
// Signatures follow THIS runner's classes.dex (RunnerJNILib natives):
//   Startup(String apk, String saveDir, String packageName, int sleepMargin,
//           boolean useDynamicAssetDelivery)
//   Process(int w, int h, float ax, float ay, float az, int orient, int type,
//           float refreshRate) -> int   (0 = quit, 2 = game_restart)
//   TouchEvent(int action, int id, float x, float y)          <-- 4 args here
//   KeyEvent(int action, int keycode, int unicode, int source, int repeat)
//   MouseMoveEvent(float x, float y), MouseButtonEvent(int btn, boolean down),
//   MouseWheelEvent(float vscroll)
// Every JNI native also takes (JNIEnv *, jclass) first.
// ---------------------------------------------------------------------------

static int  (*e_JNI_OnLoad)(void *vm, void *reserved);
static void (*e_Startup)(void *env, void *cls, void *apk, void *save, void *pkg, int sleep_margin, int dyn_assets);
static int  (*e_Process)(void *env, void *cls, int w, int h, float ax, float ay, float az, int orient, int type, float refresh);
static void (*e_TouchEvent)(void *env, void *cls, int action, int id, float x, float y);
static void (*e_KeyEvent)(void *env, void *cls, int action, int keycode, int unicode, int source, int repeat);
static void (*e_MouseMoveEvent)(void *env, void *cls, float x, float y);
static void (*e_MouseButtonEvent)(void *env, void *cls, int button, int down);
static void (*e_MouseWheelEvent)(void *env, void *cls, float delta);
static void (*e_Pause)(void *env, void *cls, int code);
static void (*e_Resume)(void *env, void *cls, int code);
// Argument is a BOOLEAN: 1 -> GLFuncImport(true) -> GLES2 entry points.
static int  (*e_initGLFuncs)(void *env, void *cls, int gles2);

extern void *g_dsMapCreate;
extern void *g_dsMapAddString;
extern void *g_dsMapAddInt;

#define RX(sym) so_try_find_addr_rx(&game_mod, sym)
#define JNI(sym) RX("Java_com_yoyogames_runner_RunnerJNILib_" sym)

static void resolve_entry_points(void) {
  e_JNI_OnLoad        = (void *)RX("JNI_OnLoad");
  e_Startup           = (void *)JNI("Startup");
  e_Process           = (void *)JNI("Process");
  e_TouchEvent        = (void *)JNI("TouchEvent");
  e_KeyEvent          = (void *)JNI("KeyEvent");
  e_MouseMoveEvent    = (void *)JNI("MouseMoveEvent");
  e_MouseButtonEvent  = (void *)JNI("MouseButtonEvent");
  e_MouseWheelEvent   = (void *)JNI("MouseWheelEvent");
  e_Pause             = (void *)JNI("Pause");
  e_Resume            = (void *)JNI("Resume");
  e_initGLFuncs       = (void *)JNI("initGLFuncs");
  g_onGPDeviceAdded   = (void *)JNI("onGPDeviceAdded");
  g_onGamepadChange   = (void *)JNI("onGamepadChange");
  g_dsMapCreate       = (void *)JNI("dsMapCreate");
  g_dsMapAddString    = (void *)JNI("dsMapAddString");
  g_dsMapAddInt       = (void *)JNI("dsMapAddInt");
  s_io_keydown        = (volatile uint8_t *)RX("_IO_KeyDown");
  debugPrintf("pcl_nx: entry points resolved (mouse %s)\n",
              (e_MouseMoveEvent && e_MouseButtonEvent && e_MouseWheelEvent) ? "yes" : "NO");
}

static void *thiz; // fake jclass / activity handed to the natives

// ---------------------------------------------------------------------------
// input hooks -> runner
// ---------------------------------------------------------------------------

#define AINPUT_SOURCE_KEYBOARD 0x101

static void hook_touch(int action, int id, float x, float y) {
  if (e_TouchEvent) e_TouchEvent(fake_env, thiz, action, id, x, y);
}
static void hook_key(int action, int keycode, int unicode) {
  if (e_KeyEvent) e_KeyEvent(fake_env, thiz, action, keycode, unicode, AINPUT_SOURCE_KEYBOARD, 0);
}
static void hook_mouse_move(float x, float y) {
  if (e_MouseMoveEvent) e_MouseMoveEvent(fake_env, thiz, x, y);
}
static void hook_mouse_button(int button, int down) {
  if (e_MouseButtonEvent) e_MouseButtonEvent(fake_env, thiz, button, down ? 1 : 0);
}
static void hook_mouse_wheel(float delta) {
  if (e_MouseWheelEvent) e_MouseWheelEvent(fake_env, thiz, delta);
}

static void hook_vk_state(int vk, int down) {
  if (s_io_keydown && vk > 0 && vk < 256) s_io_keydown[vk] = down ? 1 : 0;
}

// Touch panel (1280x720, top-left) -> game window, through the letterbox.
static void map_touch(float px, float py, float *x, float *y) {
  const float sx = px * (float)screen_width / 1280.0f;
  const float sy = py * (float)screen_height / 720.0f;
  display_screen_to_game(sx, sy, x, y);
}

static void nxp_log(const char *msg) { debugPrintf("%s", msg); }

static void pointer_init(void) {
  NxpConfig c;
  memset(&c, 0, sizeof(c));
  c.screen_w = render_width;       // the cursor lives in game-window space
  c.screen_h = render_height;
  c.panel_w  = 1280;
  c.panel_h  = 720;
  c.data_dir = paths_base();
  c.handle_touch = 1;
  c.cursor_id = 8;
  c.stick_speed = config.cursor_speed * (float)render_height / 720.0f;
  c.custom_buttons = 1;            // input.c owns every button
  // The game draws its own cursor at mouse_x/mouse_y, so no overlay is drawn:
  // nx_pointer only moves the pointer (stick, gyro, USB mouse) and reads touch.
  c.cursor_mode = 2;
  c.mouse_passthrough = 1;
  c.map_touch = map_touch;
  c.log = nxp_log;
  nxp_init(&c);

  const InputHooks h = { hook_touch, hook_key, hook_mouse_move, hook_mouse_button, hook_mouse_wheel, hook_vk_state };
  input_init(&h);
  debugPrintf("pcl_nx: pointer ready (%dx%d)\n", c.screen_w, c.screen_h);
}

// ---------------------------------------------------------------------------

static char s_wdir[512];
static char s_apk_path[600];
static char s_save_path[600];

static void call_startup(void) {
  debugPrintf("pcl_nx: Startup(apk=%s, save=%s, pkg=%s, sleep_margin=%d)\n",
              s_apk_path, s_save_path, GAME_PACKAGE, config.sleep_margin);
  e_Startup(fake_env, thiz, jni_make_string(s_apk_path), jni_make_string(s_save_path),
            jni_make_string(GAME_PACKAGE), config.sleep_margin, 0);
  debugPrintf("pcl_nx: Startup returned\n");
}

int main(int argc, char *argv[]) {
  // Where are we? (argv[0] folder, cwd, or a conventional fallback.) Every path
  // below is absolute, because the runner may chdir() underneath us.
  paths_init(argc, argv);

  // Config first: it decides whether we log at all.
  char cfg_path[640], log_path[640];
  paths_join(cfg_path, sizeof(cfg_path), CONFIG_NAME);
  paths_join(log_path, sizeof(log_path), LOG_NAME);
  if (read_config(cfg_path) != 0) write_config(cfg_path);
  g_debug_log = config.debug_log;

  debug_log_init(log_path);
  debugPrintf("pcl_nx: build for %s %s, compiled %s %s\n", GAME_PACKAGE, GAME_VERSION, __DATE__, __TIME__);
  debugPrintf("pcl_nx: base directory (%s), save dir \"%s\"\n", paths_how(), paths_base_posix());

  libc_shim_set_base_dir(paths_base());
  fsdev_guard_init();
  ctype_init();
  {
    char assets[640];
    paths_join(assets, sizeof(assets), ASSETS_DIR);
    asset_set_root(assets);
  }

  check_syscalls();
  check_data();
  set_screen_size();

  plInitialize(PlServiceType_User);
  gfx_init();

  SDL_SetMainReady();
  if (!egl_init())
    fatal_error("Failed to create an OpenGL ES 2.0 context.");
  fatal_set_cleanup(egl_deinit);   // error screen needs the window back

  if (!display_init())
    debugPrintf("pcl_nx: WARNING no offscreen target; drawing direct to panel\n");

  pointer_init();

  debugPrintf("pcl_nx: loading %s\n", s_so_file);
  const int lrc = so_load(&game_mod, s_so_file, heap_so_base, heap_so_limit);
  if (lrc < 0)
    fatal_error("Could not load\n%s (%d).", s_so_file, lrc);
  debugPrintf("pcl_nx: load=%p virt=%p size=0x%zx\n",
              game_mod.load_base, game_mod.load_virtbase, game_mod.load_size);

  ct_resolve_imports(&game_mod);
  const int guard_patches = so_patch_tpidr_stack_guard(&game_mod);
  if (guard_patches <= 0)
    fatal_error("Could not redirect GameMaker stack guards (%d).", guard_patches);
  debugPrintf("pcl_nx: redirected %d stack guard reads\n", guard_patches);

  // Resolve now, while the symbol tables in load_base are still readable.
  resolve_entry_points();
  if (!e_Startup || !e_Process)
    fatal_error("Could not resolve the GameMaker\nStartup/Process entry points.\nIs this the right libyoyo.so?");
  if (!e_TouchEvent || !e_MouseMoveEvent)
    debugPrintf("pcl_nx: WARNING input entry points missing -- wrong runner build?\n");

  so_finalize(&game_mod);
  so_flush_caches(&game_mod);
  so_execute_init_array(&game_mod);
  so_free_temp(&game_mod);

  // --- JNI + GameMaker bootstrap ---
  jni_init();
  thiz = jni_make_object("RunnerJNILib");

  snprintf(s_wdir, sizeof(s_wdir), "%s", paths_base());
  jni_set_writable_path(s_wdir);
  {
    char prefs_path[600];
    paths_join(prefs_path, sizeof(prefs_path), "YoYoPrefsFile.txt");
    prefs_init(prefs_path);
  }

  if (e_initGLFuncs) {
    const int gl_rc = e_initGLFuncs(fake_env, thiz, 1);
    debugPrintf("pcl_nx: initGLFuncs(1) returned %d\n", gl_rc);
    if (!gl_saw_gles2_import())
      fatal_error("GL init took the GLES1 path.\ninitGLFuncs rc=%d", gl_rc);
  }

  if (e_JNI_OnLoad)
    e_JNI_OnLoad(fake_vm, NULL);

  // apk: full device path, whatever the file is called. save dir: POSIX path
  // WITH a trailing '/' and WITHOUT the device -- the runner concatenates file
  // names onto it. Device-less paths resolve on the device we chdir'd to, so
  // this works from sdmc or any other mounted device (see sanitize_path too).
  snprintf(s_apk_path, sizeof(s_apk_path), "%s", s_apk_file);
  snprintf(s_save_path, sizeof(s_save_path), "%s", paths_base_posix());
  call_startup();

  int paused = 0;
  int frames = 0;
  int normal_frames = 0;
  int last_rc = -1;
  int save_ticker = 0;
  u64 perf_ticks = 0, perf_max_ticks = 0;
  int perf_frames = 0;

  while (appletMainLoop() && !jni_quit_requested) {
    const int focused = appletGetFocusState() == AppletFocusState_InFocus;
    if (!focused && !paused) {
      input_release_all();
      if (e_Pause) e_Pause(fake_env, thiz, 0);
      paused = 1;
      prefs_flush();
    } else if (focused && paused) {
      if (e_Resume) e_Resume(fake_env, thiz, 0);
      paused = 0;
    }
    if (paused) {
      svcSleepThread(16000000ull);
      continue;
    }

    const u64 frame_start = armGetSystemTick();
    input_update();

    const int rc = e_Process(fake_env, thiz, render_width, render_height,
                             0.0f, 0.0f, 0.0f, 0, 0, 60.0f);
    if (rc != last_rc) {
      debugPrintf("pcl_nx: Process returned %d (frame %d)\n", rc, frames);
      last_rc = rc;
    }
    if (rc == 1) {
      normal_frames++;
    } else if (normal_frames > 30) {
      // DemoRenderer: 0 -> ExitApplication, 2 -> "due to restart" (game_restart)
      if (rc == 0) {
        debugPrintf("pcl_nx: runner asked to quit\n");
        jni_quit_requested = 1;
      } else if (rc == 2) {
        debugPrintf("pcl_nx: runner asked to restart\n");
        input_release_all();
        normal_frames = 0;
        call_startup();
      }
    }

    display_present();
    eglSwapBuffers(s_display, s_surface);

    // Blocking system applet -- only ever between frames.
    input_service_keyboard();

    const u64 frame_ticks = armGetSystemTick() - frame_start;
    perf_ticks += frame_ticks;
    if (frame_ticks > perf_max_ticks) perf_max_ticks = frame_ticks;
    if (++perf_frames == 600) {
      const u64 freq = armGetSystemTickFreq();
      debugPrintf("perf: avg=%llu us max=%llu us\n",
                  (unsigned long long)(freq ? (perf_ticks * 1000000ull) / (freq * 600ull) : 0),
                  (unsigned long long)(freq ? (perf_max_ticks * 1000000ull) / freq : 0));
      perf_ticks = perf_max_ticks = 0;
      perf_frames = 0;
    }
    frames++;

    // Periodic prefs flush (~5 s) instead of per-write SD traffic.
    if (++save_ticker >= 300) {
      save_ticker = 0;
      prefs_flush();
    }
  }

  input_release_all();
  prefs_flush();
  display_shutdown();
  opensles_shutdown();
  egl_deinit();
  plExit();

  extern void NX_NORETURN __libnx_exit(int rc);
  __libnx_exit(0);
  return 0;
}
