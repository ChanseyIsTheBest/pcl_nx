/* util.h -- misc utility functions
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>

extern int g_debug_log;   // runtime switch, see config.txt debug_log

void debug_log_init(const char *path);
void debug_log_close(void);
int debugPrintf(const char *text, ...);

// NOTE: CPU boost is deliberately absent. appletSetCpuBoostMode(FastLoad) drives
// the SoC to 1785 MHz and pins the memory clock, which caused instability here,
// so the port runs entirely at the system's normal clocks. Do not reintroduce it
// without testing under sustained load.

int ret0(void);
int retm1(void);

static inline uint64_t umin(uint64_t a, uint64_t b) {
  return (a < b) ? a : b;
}

#endif
