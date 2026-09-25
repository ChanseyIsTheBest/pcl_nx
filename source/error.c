/* error.c -- error handler
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <switch.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "util.h"
#include "error.h"

static void (*s_cleanup)(void) = NULL;

void fatal_set_cleanup(void (*fn)(void)) { s_cleanup = fn; }

void fatal_error(const char *fmt, ...) {
  // Format first: the arguments may point into state the cleanup tears down.
  va_list list;
  va_start(list, fmt);
  char message[1024];
  vsnprintf(message, sizeof(message), fmt, list);
  va_end(list);
  debugPrintf("FATAL: %s\n", message);

  void (*cleanup)(void) = s_cleanup;
  s_cleanup = NULL;               // never re-enter if the cleanup itself fails
  if (cleanup) cleanup();

  PadState pad;
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&pad);

  consoleInit(NULL);
  printf("Pocket Crystal League NX\n\n%s", message);

  printf("\n\nPress A to exit.");

  consoleUpdate(NULL);

  while (appletMainLoop()) {
    padUpdate(&pad);
    const u64 keys = padGetButtonsDown(&pad);
    if (keys & HidNpadButton_A) break;
  }

  consoleExit(NULL);
  exit(1);
}
