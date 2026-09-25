/* extensions.h -- GameMaker extension dispatch
 *
 * Pocket Crystal League ships no extensions (empty EXTN chunk, none listed in
 * options.ini). The runner would reach native ones through
 * RunnerActivity.CallExtensionFunction(class, func, argc, double[], Object[]),
 * which jni_fake.c forwards here; this build just logs and returns 0.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __EXTENSIONS_H__
#define __EXTENSIONS_H__

// Dispatch one extension call. Returns a boxed Double or String local ref
// (see jni_make_double / jni_make_string), or NULL for a void return.
void *ext_call(const char *cls, const char *func, int argc,
               const double *dargs, int dargc, const char *sarg);

// Called once per frame from the main loop: decays any active rumble envelope.
void ext_rumble_update(void);

// Called on suspend / shutdown so the pads do not latch on.
void ext_rumble_stop(void);

#endif
