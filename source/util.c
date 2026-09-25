/* util.c -- misc utility functions
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <switch.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "config.h"

// Runtime switch (config.txt: debug_log). Logging code is compiled in unless
// the build sets DEBUG_LOG=0.
int g_debug_log = 1;

#if DEBUG_LOG

// One persistent log handle, flushed after every line so the last messages
// survive a crash. Reopening the file per call is far too slow on SD when the
// engine logs thousands of lines during boot.
static FILE *s_log = NULL;
static Mutex s_log_lock;
static char s_log_path[600] = LOG_NAME;   // absolute once debug_log_init runs

void userAppInit(void) {
  mutexInit(&s_log_lock);
}

void userAppExit(void) {
  // The process closes the handle. Keeping it alive avoids teardown races when
  // libc++ emits one final abort/exception message from another thread.
}

#endif

void debug_log_init(const char *path) {
#if DEBUG_LOG
  if (!g_debug_log) return;
  mutexLock(&s_log_lock);
  if (s_log) fclose(s_log);
  if (path) snprintf(s_log_path, sizeof(s_log_path), "%s", path);
  s_log = fopen(s_log_path, "w");
  if (s_log) {
    fprintf(s_log, "=== Pocket Crystal League NX debug log ===\n");
    fflush(s_log);
  }
  mutexUnlock(&s_log_lock);
#else
  (void)path;
#endif
}

void debug_log_close(void) {
#if DEBUG_LOG
  mutexLock(&s_log_lock);
  if (s_log) fclose(s_log);
  s_log = NULL;
  mutexUnlock(&s_log_lock);
#endif
}

int debugPrintf(const char *text, ...) {
#if DEBUG_LOG
  if (!g_debug_log) return 0;
  mutexLock(&s_log_lock);
  if (!s_log) {
    s_log = fopen(s_log_path, "a");
    if (!s_log) { mutexUnlock(&s_log_lock); return 0; }
  }
  va_list list;
  va_start(list, text);
  vfprintf(s_log, text, list);
  va_end(list);
  fflush(s_log);
  mutexUnlock(&s_log_lock);
#else
  (void)text;
#endif
  return 0;
}

int ret0(void) { return 0; }

int retm1(void) { return -1; }
