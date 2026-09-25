/* config.c -- config.txt parser / writer
 *
 * Copyright (C) 2021 Andy Nguyen, fgsfds (original parser)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "config.h"

Config config;

int screen_width = 1280;
int screen_height = 720;
int render_width = 1280;
int render_height = 720;
int render_offset_x = 0;
int render_offset_y = 0;

// ---------------------------------------------------------------------------
// key names
// ---------------------------------------------------------------------------

static const struct { const char *name; int code; } s_named_keys[] = {
  { "space", AKEY_SPACE },       { "enter", AKEY_ENTER },     { "return", AKEY_ENTER },
  { "esc", AKEY_ESCAPE },        { "escape", AKEY_ESCAPE },   { "tab", AKEY_TAB },
  { "backspace", AKEY_DEL },     { "delete", AKEY_FORWARD_DEL },
  { "up", AKEY_DPAD_UP },        { "down", AKEY_DPAD_DOWN },
  { "left", AKEY_DPAD_LEFT },    { "right", AKEY_DPAD_RIGHT },
  { "shift", AKEY_SHIFT_LEFT },  { "ctrl", 113 },             { "alt", 57 },
  { "back", AKEY_BACK },         { "minus", 69 },             { "equals", 70 },
  { "comma", 55 },               { "period", 56 },            { "slash", 76 },
};

int keycode_from_name(const char *name) {
  if (!name || !*name) return -1;
  for (unsigned i = 0; i < sizeof(s_named_keys) / sizeof(*s_named_keys); i++)
    if (!strcasecmp(name, s_named_keys[i].name)) return s_named_keys[i].code;
  if (name[1] == 0) {
    const int c = tolower((unsigned char)name[0]);
    if (c >= 'a' && c <= 'z') return AKEY_A + (c - 'a');
    if (c >= '0' && c <= '9') return AKEY_0 + (c - '0');
  }
  if ((name[0] == 'f' || name[0] == 'F') && isdigit((unsigned char)name[1])) {
    const int n = atoi(name + 1);
    if (n >= 1 && n <= 12) return 131 + (n - 1);   // AKEYCODE_F1..F12
  }
  if (!strncasecmp(name, "code", 4) && isdigit((unsigned char)name[4]))
    return atoi(name + 4);                          // raw Android keycode escape hatch
  return -1;
}

const char *keycode_name(int code) {
  // small ring so a few results can be used in one expression
  static char ring[4][16];
  static unsigned slot = 0;
  char *buf = ring[slot++ & 3];
  for (unsigned i = 0; i < sizeof(s_named_keys) / sizeof(*s_named_keys); i++)
    if (s_named_keys[i].code == code) return s_named_keys[i].name;
  if (code >= AKEY_A && code < AKEY_A + 26) { buf[0] = (char)('a' + code - AKEY_A); buf[1] = 0; return buf; }
  if (code >= AKEY_0 && code < AKEY_0 + 10) { buf[0] = (char)('0' + code - AKEY_0); buf[1] = 0; return buf; }
  if (code >= 131 && code <= 142) { snprintf(buf, 16, "f%d", code - 130); return buf; }
  snprintf(buf, 16, "code%d", code);
  return buf;
}

// ---------------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------------

static const struct { const char *name; int type; } s_actions[] = {
  { "none", ACT_NONE },          { "lclick", ACT_LCLICK },       { "rclick", ACT_RCLICK },
  { "mclick", ACT_MCLICK },      { "wheel_up", ACT_WHEEL_UP },   { "wheel_down", ACT_WHEEL_DOWN },
  { "keyboard", ACT_KEYBOARD },  { "recenter", ACT_RECENTER },   { "gyro", ACT_GYRO },
};

static int parse_action(const char *v, InputAction *out) {
  if (!strncasecmp(v, "key:", 4)) {
    const int code = keycode_from_name(v + 4);
    if (code < 0) return -1;
    out->type = ACT_KEY;
    out->code = code;
    return 0;
  }
  for (unsigned i = 0; i < sizeof(s_actions) / sizeof(*s_actions); i++) {
    if (!strcasecmp(v, s_actions[i].name)) {
      out->type = s_actions[i].type;
      out->code = 0;
      return 0;
    }
  }
  return -1;
}

static void action_to_string(const InputAction *a, char *out, size_t n) {
  if (a->type == ACT_KEY) {
    snprintf(out, n, "key:%s", keycode_name(a->code));
    return;
  }
  for (unsigned i = 0; i < sizeof(s_actions) / sizeof(*s_actions); i++)
    if (s_actions[i].type == a->type) { snprintf(out, n, "%s", s_actions[i].name); return; }
  snprintf(out, n, "none");
}

static const char *const s_btn_names[BTN_COUNT] = {
  "btn_a", "btn_b", "btn_x", "btn_y",
  "btn_l", "btn_r", "btn_zl", "btn_zr",
  "btn_plus", "btn_minus",
  "btn_dup", "btn_ddown", "btn_dleft", "btn_dright",
  "btn_lstick", "btn_rstick",
};

// ---------------------------------------------------------------------------
// defaults
// ---------------------------------------------------------------------------

static void set_defaults(void) {
  memset(&config, 0, sizeof(config));
  config.resolution   = 0;
  config.scale        = SCALE_FIT;
  config.vsync        = 1;
  config.sleep_margin = 10;
  config.cursor_speed = 12.0f;
  config.rstick       = RSTICK_WHEEL;
  config.wheel_invert = 0;
  config.kbd_clear    = 12;
  config.kbd_enter    = 1;
  config.debug_log    = 0;

  // Pocket Crystal League is a mouse-driven card game with a handful of
  // keyboard shortcuts (read out of its GML): arrows / A-D page between menu
  // screens and deck views, Space ends the turn, Tab sorts the hand, Up/W held
  // auto-attacks, right-click inspects cards, the wheel scrolls lists.
  config.map[BTN_A]      = (InputAction){ ACT_LCLICK, 0 };
  config.map[BTN_ZR]     = (InputAction){ ACT_LCLICK, 0 };
  config.map[BTN_B]      = (InputAction){ ACT_RCLICK, 0 };
  config.map[BTN_ZL]     = (InputAction){ ACT_RCLICK, 0 };
  config.map[BTN_X]      = (InputAction){ ACT_KEY, AKEY_SPACE };
  config.map[BTN_Y]      = (InputAction){ ACT_KEY, AKEY_TAB };
  config.map[BTN_L]      = (InputAction){ ACT_WHEEL_UP, 0 };
  config.map[BTN_R]      = (InputAction){ ACT_WHEEL_DOWN, 0 };
  config.map[BTN_PLUS]   = (InputAction){ ACT_KEY, AKEY_ESCAPE };
  config.map[BTN_MINUS]  = (InputAction){ ACT_KEYBOARD, 0 };
  config.map[BTN_DUP]    = (InputAction){ ACT_KEY, AKEY_DPAD_UP };
  config.map[BTN_DDOWN]  = (InputAction){ ACT_KEY, AKEY_DPAD_DOWN };
  config.map[BTN_DLEFT]  = (InputAction){ ACT_KEY, AKEY_DPAD_LEFT };
  config.map[BTN_DRIGHT] = (InputAction){ ACT_KEY, AKEY_DPAD_RIGHT };
  config.map[BTN_LSTICK] = (InputAction){ ACT_RECENTER, 0 };
  config.map[BTN_RSTICK] = (InputAction){ ACT_MCLICK, 0 };
}

// ---------------------------------------------------------------------------
// parse
// ---------------------------------------------------------------------------

static void parse_var(const char *name, const char *value) {
  for (int i = 0; i < BTN_COUNT; i++) {
    if (!strcasecmp(name, s_btn_names[i])) {
      InputAction a;
      if (parse_action(value, &a) == 0) config.map[i] = a;
      return;
    }
  }

  if (!strcmp(name, "scale")) {
    if (!strcasecmp(value, "fit"))          config.scale = SCALE_FIT;
    else if (!strcasecmp(value, "sharp"))   config.scale = SCALE_SHARP;
    else if (!strcasecmp(value, "integer")) config.scale = SCALE_INTEGER;
    else config.scale = atoi(value);
    if (config.scale < SCALE_FIT || config.scale > SCALE_INTEGER) config.scale = SCALE_FIT;
    return;
  }
  if (!strcmp(name, "rstick")) {
    if (!strcasecmp(value, "none"))        config.rstick = RSTICK_NONE;
    else if (!strcasecmp(value, "wheel"))  config.rstick = RSTICK_WHEEL;
    else if (!strcasecmp(value, "arrows")) config.rstick = RSTICK_ARROWS;
    else config.rstick = atoi(value);
    if (config.rstick < RSTICK_NONE || config.rstick > RSTICK_ARROWS) config.rstick = RSTICK_WHEEL;
    return;
  }
  if (!strcmp(name, "resolution")) {
    const int v = atoi(value);
    config.resolution = (v == 720 || v == 1080) ? v : 0;
    return;
  }
  if (!strcmp(name, "cursor_speed")) {
    config.cursor_speed = (float)atof(value);
    if (config.cursor_speed < 1.0f)  config.cursor_speed = 1.0f;
    if (config.cursor_speed > 80.0f) config.cursor_speed = 80.0f;
    return;
  }
  if (!strcmp(name, "vsync"))        { config.vsync = atoi(value) ? 1 : 0; return; }
  if (!strcmp(name, "sleep_margin")) { config.sleep_margin = atoi(value); return; }
  if (!strcmp(name, "wheel_invert")) { config.wheel_invert = atoi(value) ? 1 : 0; return; }
  if (!strcmp(name, "kbd_clear"))    { config.kbd_clear = atoi(value); if (config.kbd_clear < 0) config.kbd_clear = 0; if (config.kbd_clear > 64) config.kbd_clear = 64; return; }
  if (!strcmp(name, "kbd_enter"))    { config.kbd_enter = atoi(value) ? 1 : 0; return; }
  if (!strcmp(name, "debug_log"))    { config.debug_log = atoi(value) ? 1 : 0; return; }
}

int read_config(const char *file) {
  char line[1024];

  set_defaults();

  FILE *f = fopen(file, "r");
  if (f == NULL)
    return -1;

  // lines of the forms
  //   <spaces> # <whatever>
  //   <spaces> NAME <spaces> VALUE <spaces> [# comment]
  while (fgets(line, sizeof(line), f) != NULL) {
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    char *name = line;
    while (*name && isspace((unsigned char)*name)) ++name;
    if (!*name) continue;
    char *tmp = name;
    while (*tmp && !isspace((unsigned char)*tmp)) ++tmp;
    if (!*tmp) continue;
    *tmp = 0;
    char *value = tmp + 1;
    while (*value && isspace((unsigned char)*value)) ++value;
    for (tmp = value + strlen(value) - 1; tmp >= value && isspace((unsigned char)*tmp); --tmp) *tmp = 0;
    if (*value) parse_var(name, value);
  }

  fclose(f);
  return 0;
}

int write_config(const char *file) {
  FILE *f = fopen(file, "w");
  if (f == NULL)
    return -1;

  static const char *scale_names[] = { "fit", "sharp", "integer" };
  static const char *rstick_names[] = { "none", "wheel", "arrows" };

  fprintf(f, "# Pocket Crystal League -- Switch wrapper configuration\n");
  fprintf(f, "# Written on first launch. Edit freely; delete to restore defaults.\n\n");

  fprintf(f, "# Framebuffer size: 0 = follow the mode at launch (720p handheld,\n");
  fprintf(f, "# 1080p docked), or force 720 / 1080.\n");
  fprintf(f, "resolution %d\n\n", config.resolution);

  fprintf(f, "# fit     = game draws at screen resolution (default)\n");
  fprintf(f, "# sharp   = game draws at the next integer multiple of 512x288 and is\n");
  fprintf(f, "#           filtered down: even pixels, slightly soft edges\n");
  fprintf(f, "# integer = largest exact multiple that fits, with a black border\n");
  fprintf(f, "scale %s\n\n", scale_names[config.scale]);

  fprintf(f, "# Frame pacing. 1 = the display paces the game, exactly like Android\n");
  fprintf(f, "# (recommended). 0 = the runner sleeps to 60 fps by itself and frames are\n");
  fprintf(f, "# presented without waiting; try it if the game drops to 30 fps in heavy\n");
  fprintf(f, "# scenes. sleep_margin (ms) only matters with vsync 0.\n");
  fprintf(f, "vsync %d\n", config.vsync);
  fprintf(f, "sleep_margin %d\n\n", config.sleep_margin);

  fprintf(f, "# How fast the left stick moves the game's cursor (pixels per frame at\n");
  fprintf(f, "# full tilt, 720p units).\n");
  fprintf(f, "cursor_speed %.1f\n\n", (double)config.cursor_speed);

  fprintf(f, "# Right stick: wheel (scroll lists), arrows, none\n");
  fprintf(f, "rstick %s\n", rstick_names[config.rstick]);
  fprintf(f, "wheel_invert %d\n\n", config.wheel_invert);

  fprintf(f, "# Software keyboard (name entry): backspaces sent first to clear the\n");
  fprintf(f, "# field, and whether Enter is pressed after typing.\n");
  fprintf(f, "kbd_clear %d\n", config.kbd_clear);
  fprintf(f, "kbd_enter %d\n\n", config.kbd_enter);

  fprintf(f, "# Button mapping. Actions: lclick rclick mclick wheel_up wheel_down\n");
  fprintf(f, "# keyboard recenter gyro none, or key:<name> where <name> is a\n");
  fprintf(f, "# letter, digit, f1-f12, space enter esc tab backspace delete up down\n");
  fprintf(f, "# left right shift ctrl alt, or code<N> for a raw Android keycode.\n");
  for (int i = 0; i < BTN_COUNT; i++) {
    char buf[32];
    action_to_string(&config.map[i], buf, sizeof(buf));
    fprintf(f, "%s %s\n", s_btn_names[i], buf);
  }

  fprintf(f, "\n# 1 writes debug.log next to the NRO (slower; for troubleshooting).\n");
  fprintf(f, "debug_log %d\n", config.debug_log);

  fclose(f);
  return 0;
}
