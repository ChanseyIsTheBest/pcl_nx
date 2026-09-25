/* config.h -- Pocket Crystal League Switch wrapper configuration
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

// libyoyo.so for Pocket Crystal League 3.0.2 spans ~17 MiB of virtual image
// space (last PT_LOAD ends at 0x11033f0). so_load stages the file through the
// newlib heap and copies it here, so the reservation only has to hold the load
// image plus the stack-guard page. Everything else stays on the newlib heap,
// which also backs Mesa/nouveau and the ~116 MB game.droid the runner inflates
// out of the APK.
#define SO_HEAP_RESERVE_MB 48

// The runner statically links its C++ runtime; there is no libc++_shared.so.
#define SO_NAME "libyoyo.so"

// Required. The runner zip_open()s this and reads assets/game.droid out of it.
#define APK_NAME "game.apk"

// Optional loose copy of the APK's assets/ folder, served through the fake
// AAssetManager in case the runner ever asks for an asset that way.
#define ASSETS_DIR "assets"

#define CONFIG_NAME "config.txt"
#define LOG_NAME    "debug.log"

// Identity reported to the runner (from the APK's AndroidManifest.xml).
#define GAME_PACKAGE      "com.company.game"
#define GAME_VERSION      "3.0.2"
#define GAME_VERSION_CODE 3000002

// Logging code is always compiled in; config.debug_log decides at runtime
// whether anything is written. Build with DEBUG_LOG=0 to strip it entirely.
#ifndef DEBUG_LOG
#define DEBUG_LOG 1
#endif

#define LANG_DEFAULT "en"

// The game's base resolution (GEN8 window size in game.droid).
#define GAME_BASE_W 512
#define GAME_BASE_H 288

// Physical framebuffer (the NWindow / EGL surface).
extern int screen_width;
extern int screen_height;

// Logical window handed to RunnerJNILib.Process(). display.c renders this into
// an offscreen target and scales it onto the panel. render_offset_* are only
// used by the direct-to-panel fallback when the offscreen target is unavailable.
extern int render_width;
extern int render_height;
extern int render_offset_x;
extern int render_offset_y;

// --- scaling ---------------------------------------------------------------
enum {
  SCALE_FIT     = 0, // runner draws at panel resolution (default)
  SCALE_SHARP   = 1, // runner draws at an integer multiple >= panel, filtered down
  SCALE_INTEGER = 2, // runner draws at an integer multiple <= panel, black border
};

// --- right stick ---------------------------------------------------------------
enum {
  RSTICK_NONE   = 0,
  RSTICK_WHEEL  = 1, // up/down = mouse wheel (scroll lists)
  RSTICK_ARROWS = 2, // four directions = arrow keys
};

// --- button actions ------------------------------------------------------------
enum {
  ACT_NONE = 0,
  ACT_LCLICK,     // mouse left
  ACT_RCLICK,     // mouse right
  ACT_MCLICK,     // mouse middle
  ACT_WHEEL_UP,
  ACT_WHEEL_DOWN,
  ACT_KEY,        // keyboard key; code = Android keycode
  ACT_KEYBOARD,   // open the Switch software keyboard and type the result
  ACT_RECENTER,   // move the cursor to the centre of the screen
  ACT_GYRO,       // toggle gyro pointing
};

typedef struct {
  int type;
  int code;
} InputAction;

enum {
  BTN_A, BTN_B, BTN_X, BTN_Y,
  BTN_L, BTN_R, BTN_ZL, BTN_ZR,
  BTN_PLUS, BTN_MINUS,
  BTN_DUP, BTN_DDOWN, BTN_DLEFT, BTN_DRIGHT,
  BTN_LSTICK, BTN_RSTICK,
  BTN_COUNT
};

typedef struct {
  int   resolution;    // 0 = follow launch mode, 720, 1080
  int   scale;         // SCALE_*
  int   vsync;         // 1 = display paces (Android default), 0 = runner paces itself
  int   sleep_margin;  // ms handed to the runner's frame limiter
  float cursor_speed;  // px/frame at full stick deflection, in 720p units
  int   rstick;        // RSTICK_*
  int   wheel_invert;
  int   kbd_clear;     // backspaces sent before typed text
  int   kbd_enter;     // press Enter after typed text
  int   debug_log;
  InputAction map[BTN_COUNT];
} Config;

extern Config config;

// Returns 0 on success, -1 if the file does not exist.
int read_config(const char *file);
int write_config(const char *file);

// Android keycode helpers shared with input.c
#define AKEY_BACK        4
#define AKEY_0           7
#define AKEY_DPAD_UP     19
#define AKEY_DPAD_DOWN   20
#define AKEY_DPAD_LEFT   21
#define AKEY_DPAD_RIGHT  22
#define AKEY_A           29
#define AKEY_SHIFT_LEFT  59
#define AKEY_TAB         61
#define AKEY_SPACE       62
#define AKEY_ENTER       66
#define AKEY_DEL         67   // backspace
#define AKEY_ESCAPE      111
#define AKEY_FORWARD_DEL 112

// Name <-> keycode for config.txt ("space", "tab", "a", "7", "f1", ...).
int         keycode_from_name(const char *name);  // -1 if unknown
const char *keycode_name(int code);                // NULL if unnamed

#endif
