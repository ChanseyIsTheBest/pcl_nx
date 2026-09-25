/* input.c -- Switch controls -> GameMaker Android input (see input.h)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <switch.h>

#include "config.h"
#include "input.h"
#include "nx_pointer.h"
#include "util.h"

// The runner keeps per-device state for touch ids 0..9 (TouchEvent ignores the
// bookkeeping for anything higher).
#define MAX_TOUCH_ID 10

static InputHooks H;
static PadState s_pad;
static int s_ready = 0;

static const u64 s_btn_bits[BTN_COUNT] = {
  HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
  HidNpadButton_L, HidNpadButton_R, HidNpadButton_ZL, HidNpadButton_ZR,
  HidNpadButton_Plus, HidNpadButton_Minus,
  HidNpadButton_Up, HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Right,
  HidNpadButton_StickL, HidNpadButton_StickR,
};

// ---------------------------------------------------------------------------
// keys
// ---------------------------------------------------------------------------

// Reference counts, so two buttons mapped to the same key never leave it stuck.
static uint8_t s_key_refs[256];
static uint8_t s_btn_key_held[BTN_COUNT];  // this button currently holds its key
static uint8_t s_rs_key_held[4];           // right stick arrows: U D L R

static int key_unicode(int code) {
  if (code >= AKEY_A && code < AKEY_A + 26) return 'a' + (code - AKEY_A);
  if (code >= AKEY_0 && code < AKEY_0 + 10) return '0' + (code - AKEY_0);
  if (code == AKEY_SPACE) return ' ';
  return 0;
}

// Generic modifier aliases (see InputHooks.vk_state), refcounted so overlapping
// sources (a mapped Shift button and keyboard injection) cannot drop it early.
static uint8_t s_alias_refs[3];   // vk_shift, vk_control, vk_alt

static int modifier_alias(int code) {
  switch (code) {
    case 59: case 60:   return 0;   // SHIFT_LEFT / SHIFT_RIGHT -> vk_shift (16)
    case 113: case 114: return 1;   // CTRL_LEFT / CTRL_RIGHT   -> vk_control (17)
    case 57: case 58:   return 2;   // ALT_LEFT / ALT_RIGHT     -> vk_alt (18)
    default:            return -1;
  }
}

// Every key event goes through here.
static void send_key(int action, int code, int uni) {
  const int a = modifier_alias(code);
  if (a >= 0 && H.vk_state && action == 0 && s_alias_refs[a]++ == 0) H.vk_state(16 + a, 1);
  H.key(action, code, uni);
  if (a >= 0 && H.vk_state && action == 1 && s_alias_refs[a] && --s_alias_refs[a] == 0) H.vk_state(16 + a, 0);
}

static void key_ref(int code, int down) {
  if (code <= 0 || code >= 256) return;
  if (down) {
    if (s_key_refs[code]++ == 0) send_key(0, code, key_unicode(code));
  } else if (s_key_refs[code]) {
    if (--s_key_refs[code] == 0) send_key(1, code, key_unicode(code));
  }
}

// ---------------------------------------------------------------------------
// software keyboard text injection
//
// sc_text_get_input() in the game reads ONE keyboard_check_pressed() per step
// and lowercases unless vk_shift is held, so each character gets its own
// down / up / idle frame, with Shift held around capitals.
// ---------------------------------------------------------------------------

typedef struct { uint8_t code, shift, uni; } InjKey;
#define INJ_MAX 192
static InjKey s_inj[INJ_MAX];
static int s_inj_n = 0, s_inj_pos = 0, s_inj_phase = 0;
static volatile int s_kbd_request = 0;

static void inj_push(int code, int shift, int uni) {
  if (s_inj_n >= INJ_MAX) return;
  s_inj[s_inj_n].code = (uint8_t)code;
  s_inj[s_inj_n].shift = (uint8_t)(shift ? 1 : 0);
  s_inj[s_inj_n].uni = (uint8_t)uni;
  s_inj_n++;
}

static void inj_step(void) {
  if (s_inj_pos >= s_inj_n) { s_inj_n = s_inj_pos = s_inj_phase = 0; return; }
  const InjKey *k = &s_inj[s_inj_pos];
  switch (s_inj_phase) {
    case 0:
      if (k->shift) send_key(0, AKEY_SHIFT_LEFT, 0);
      send_key(0, k->code, k->uni);
      s_inj_phase = 1;
      break;
    case 1:
      send_key(1, k->code, k->uni);
      if (k->shift) send_key(1, AKEY_SHIFT_LEFT, 0);
      s_inj_phase = 2;
      break;
    default:            // idle frame so the next press is a fresh edge
      s_inj_phase = 0;
      s_inj_pos++;
      break;
  }
}

static void inj_abort(void) {
  if (s_inj_pos < s_inj_n && s_inj_phase == 1) {
    const InjKey *k = &s_inj[s_inj_pos];
    send_key(1, k->code, k->uni);
    if (k->shift) send_key(1, AKEY_SHIFT_LEFT, 0);
  }
  s_inj_n = s_inj_pos = s_inj_phase = 0;
}

void input_service_keyboard(void) {
  if (!s_ready || !s_kbd_request) return;
  s_kbd_request = 0;

  input_release_all();

  SwkbdConfig kbd;
  if (R_FAILED(swkbdCreate(&kbd, 0))) {
    debugPrintf("input: swkbdCreate failed\n");
    return;
  }
  swkbdConfigMakePresetDefault(&kbd);
  swkbdConfigSetHeaderText(&kbd, "Pocket Crystal League");
  swkbdConfigSetGuideText(&kbd, "Letters, numbers and spaces");
  swkbdConfigSetStringLenMax(&kbd, 24);

  char out[128] = {0};
  const Result rc = swkbdShow(&kbd, out, sizeof(out));
  swkbdClose(&kbd);
  if (R_FAILED(rc)) {
    debugPrintf("input: keyboard cancelled (0x%x)\n", rc);
    return;
  }

  inj_abort();
  for (int i = 0; i < config.kbd_clear; i++)
    inj_push(AKEY_DEL, 0, 0);

  int typed = 0;
  for (const unsigned char *p = (const unsigned char *)out; *p; p++) {
    const int c = *p;
    if (c >= 'a' && c <= 'z')      inj_push(AKEY_A + (c - 'a'), 0, c);
    else if (c >= 'A' && c <= 'Z') inj_push(AKEY_A + (c - 'A'), 1, c);
    else if (c >= '0' && c <= '9') inj_push(AKEY_0 + (c - '0'), 0, c);
    else if (c == ' ')             inj_push(AKEY_SPACE, 0, ' ');
    else continue;                 // the game's name entry only reads these
    typed++;
  }
  if (config.kbd_enter)
    inj_push(AKEY_ENTER, 0, 0);

  debugPrintf("input: keyboard -> \"%s\" (%d chars queued)\n", out, typed);
}

// ---------------------------------------------------------------------------
// mouse buttons / touch arbitration
// ---------------------------------------------------------------------------

static uint32_t s_real_down = 0;   // touch ids currently down
static int s_mb_sent = 0;          // mouse buttons the runner thinks are down
static int s_mb_block = 0;         // held when a finger landed; wait for release
static int s_mb_desired_last = 0;
static float s_last_mx = -1.0f, s_last_my = -1.0f;

static void release_mouse_buttons(void) {
  for (int i = 0; i < 3; i++)
    if (s_mb_sent & (1 << i)) H.mouse_button(i, 0);
  s_mb_sent = 0;
}

// ---------------------------------------------------------------------------
// repeat helper for wheel buttons / right stick
// ---------------------------------------------------------------------------

static int repeat_tick(int *counter, int active, int delay, int interval) {
  if (!active) { *counter = 0; return 0; }
  const int c = (*counter)++;
  if (c == 0) return 1;
  if (interval < 1) interval = 1;
  return (c >= delay && ((c - delay) % interval) == 0) ? 1 : 0;
}

static int s_btn_rep[BTN_COUNT];
static int s_rs_rep = 0;

// ---------------------------------------------------------------------------

void input_init(const InputHooks *hooks) {
  memset(&H, 0, sizeof(H));
  if (hooks) H = *hooks;
  padInitializeDefault(&s_pad);  // nxp_init() already called padConfigureInput
  s_ready = H.touch && H.key && H.mouse_move && H.mouse_button && H.mouse_wheel;
  if (!s_ready) debugPrintf("input: missing runner hooks, input disabled\n");
}

void input_release_all(void) {
  if (!s_ready) return;
  inj_abort();
  for (int b = 0; b < BTN_COUNT; b++) {
    if (s_btn_key_held[b]) { key_ref(config.map[b].code, 0); s_btn_key_held[b] = 0; }
    s_btn_rep[b] = 0;
  }
  static const int rs_codes[4] = { AKEY_DPAD_UP, AKEY_DPAD_DOWN, AKEY_DPAD_LEFT, AKEY_DPAD_RIGHT };
  for (int i = 0; i < 4; i++)
    if (s_rs_key_held[i]) { key_ref(rs_codes[i], 0); s_rs_key_held[i] = 0; }
  // anything still referenced (shouldn't happen) is forced up
  for (int c = 0; c < 256; c++)
    if (s_key_refs[c]) { s_key_refs[c] = 1; key_ref(c, 0); }
  release_mouse_buttons();
  s_mb_block = s_mb_desired_last;  // physically held buttons must be let go first
  s_rs_rep = 0;
}

void input_update(void) {
  nxp_update();
  if (!s_ready) return;

  // --- touchscreen -----------------------------------------------------------
  NxpEvent ev[24];
  const int n = nxp_poll(ev, (int)(sizeof(ev) / sizeof(*ev)));
  for (int i = 0; i < n; i++) {
    const int id = ev[i].id;
    if (id < 0 || id >= MAX_TOUCH_ID) continue;
    const int action = ev[i].phase == NXP_DOWN ? 0 : ev[i].phase == NXP_UP ? 1 : 2;

    if (action == 0) {
      if (!s_real_down) {
        // A finger takes device 0: lift the cursor's buttons first and keep them
        // blocked until they are released, or the two streams leave a press stuck.
        release_mouse_buttons();
        s_mb_block = s_mb_desired_last;
      }
      s_real_down |= 1u << id;
    }

    H.touch(action, id, ev[i].x, ev[i].y);

    if (id == 0) {
      // TouchEvent(id 0) also moves the runner's mouse; park the cursor there so
      // hover state stays consistent when the stick takes over again.
      nxp_set_cursor_pos(ev[i].x, ev[i].y);
      nxp_cursor_pos(&s_last_mx, &s_last_my);
    }
    if (action == 1) s_real_down &= ~(1u << id);
  }

  // --- buttons ---------------------------------------------------------------
  padUpdate(&s_pad);
  const u64 held = padGetButtons(&s_pad);
  const u64 down = padGetButtonsDown(&s_pad);

  int desired = 0, wheel = 0;
  for (int b = 0; b < BTN_COUNT; b++) {
    const InputAction *a = &config.map[b];
    const u64 bit = s_btn_bits[b];
    const int is_held = (held & bit) != 0, is_down = (down & bit) != 0;
    switch (a->type) {
      case ACT_LCLICK: if (is_held) desired |= 1; break;
      case ACT_RCLICK: if (is_held) desired |= 2; break;
      case ACT_MCLICK: if (is_held) desired |= 4; break;
      case ACT_WHEEL_UP:   wheel += repeat_tick(&s_btn_rep[b], is_held, 18, 6); break;
      case ACT_WHEEL_DOWN: wheel -= repeat_tick(&s_btn_rep[b], is_held, 18, 6); break;
      case ACT_KEY:
        if (is_down && !s_btn_key_held[b]) { key_ref(a->code, 1); s_btn_key_held[b] = 1; }
        else if (!is_held && s_btn_key_held[b]) { key_ref(a->code, 0); s_btn_key_held[b] = 0; }
        break;
      case ACT_KEYBOARD: if (is_down) s_kbd_request = 1; break;
      case ACT_RECENTER: if (is_down) nxp_recenter(); break;
      case ACT_GYRO:     if (is_down) nxp_toggle_gyro(); break;
      default: break;
    }
  }

  // --- USB mouse passthrough ---------------------------------------------------
  const uint32_t mb = nxp_mouse_buttons();
  if (mb & HidMouseButton_Left)   desired |= 1;
  if (mb & HidMouseButton_Right)  desired |= 2;
  if (mb & HidMouseButton_Middle) desired |= 4;
  wheel += nxp_mouse_wheel();

  // --- right stick -------------------------------------------------------------
  const HidAnalogStickState rs = padGetStickPos(&s_pad, 1);
  const float rx = rs.x / 32767.0f, ry = rs.y / 32767.0f;
  if (config.rstick == RSTICK_WHEEL) {
    const float mag = fabsf(ry);
    const int dir = ry > 0.5f ? 1 : (ry < -0.5f ? -1 : 0);
    // light push: a tick every ~12 frames; full push: every 3
    const int interval = 12 - (int)(9.0f * (mag - 0.5f) / 0.5f);
    wheel += dir * repeat_tick(&s_rs_rep, dir != 0, interval, interval);
  } else if (config.rstick == RSTICK_ARROWS) {
    static const int codes[4] = { AKEY_DPAD_UP, AKEY_DPAD_DOWN, AKEY_DPAD_LEFT, AKEY_DPAD_RIGHT };
    const float v[4] = { ry, -ry, -rx, rx };
    for (int i = 0; i < 4; i++) {
      const float thr = s_rs_key_held[i] ? 0.35f : 0.55f;   // hysteresis
      const int on = v[i] > thr;
      if (on && !s_rs_key_held[i])      { key_ref(codes[i], 1); s_rs_key_held[i] = 1; }
      else if (!on && s_rs_key_held[i]) { key_ref(codes[i], 0); s_rs_key_held[i] = 0; }
    }
  }

  // --- cursor motion -----------------------------------------------------------
  float cx, cy;
  nxp_cursor_pos(&cx, &cy);
  if (!s_real_down && (cx != s_last_mx || cy != s_last_my)) {
    H.mouse_move(cx, cy);
    s_last_mx = cx;
    s_last_my = cy;
  }

  // --- mouse buttons -------------------------------------------------------------
  s_mb_block &= desired;          // a blocked button clears once it is released
  s_mb_desired_last = desired;
  const int eff = s_real_down ? 0 : (desired & ~s_mb_block);
  for (int i = 0; i < 3; i++) {
    const int bit = 1 << i;
    if ((eff ^ s_mb_sent) & bit) H.mouse_button(i, (eff & bit) != 0);
  }
  s_mb_sent = eff;

  if (wheel)
    H.mouse_wheel((float)(config.wheel_invert ? -wheel : wheel));

  inj_step();
}
