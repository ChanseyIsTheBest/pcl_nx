/* imports.h -- .so import resolution for libyoyo.so
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __IMPORTS_H__
#define __IMPORTS_H__

#include <stdio.h>
#include <stdlib.h>
#include "so_util.h"

extern FILE *stderr_fake;
extern DynLibFunction dynlib_functions[];
extern size_t dynlib_numfunctions;

// relocate + resolve a single module against the shim table (and the other
// already-loaded modules, for cross-module references).
void ct_resolve_imports(so_module *mod);

// Did the runner resolve the GLES2-only block during initGLFuncs?
int gl_saw_gles2_import(void);

#endif
