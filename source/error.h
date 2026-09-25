/* error.h -- error handler
 *
 * Copyright (C) 2021 fgsfds
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __ERROR_H__
#define __ERROR_H__

void fatal_error(const char *fmt, ...) __attribute__((noreturn));

// Called once before the error screen is shown. main.c registers its EGL
// teardown here: the text console needs the default NWindow, and it cannot get
// it while an EGL surface still owns it (the message would never appear).
void fatal_set_cleanup(void (*fn)(void));

#endif
