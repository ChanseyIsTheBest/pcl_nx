/* extensions.c -- GameMaker extension dispatch
 *
 * Pocket Crystal League ships no extensions (game.droid's EXTN chunk is empty,
 * and options.ini lists none), so RunnerActivity.CallExtensionFunction should
 * never be reached. Anything that does arrive is logged and answered with 0 so
 * the GML side takes its "unavailable" path. The rumble hooks are kept as
 * no-ops so the shared main loop does not need special cases.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "extensions.h"
#include "jni_fake.h"

void *ext_call(const char *cls, const char *func, int argc,
               const double *dargs, int dargc, const char *sarg) {
  (void)dargs; (void)dargc; (void)sarg;
  debugPrintf("ext: UNHANDLED extension call %s::%s (argc=%d)\n",
              cls ? cls : "?", func ? func : "?", argc);
  return jni_make_double(0.0);
}

void ext_rumble_update(void) { }
void ext_rumble_stop(void) { }
