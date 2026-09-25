/* paths.h -- where the port's files live
 *
 * Everything (libyoyo.so, the APK, config.txt, debug.log, saves, cursor.png,
 * the optional assets/ folder) lives in ONE folder: the folder the NRO was
 * launched from, wherever that is -- any name, any depth, any mounted device.
 *
 * Resolution order:
 *   1. the directory of argv[0] (hbmenu, hbloader, most forwarders)
 *   2. the current directory (libnx chdirs there when argv[0] is on sdmc:)
 *   3. a few conventional folders, for forwarders that pass no argv[0]
 * The first candidate that actually contains the game (libyoyo.so or an APK)
 * wins; if none does, the first candidate is used so the error message can
 * say exactly where the files were expected.
 *
 * All paths handed out are absolute ("sdmc:/switch/pcl_nx/..."), because the
 * runner is free to chdir() underneath us.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __PATHS_H__
#define __PATHS_H__

#include <stddef.h>

void paths_init(int argc, char *argv[]);

// Absolute base directory with device, no trailing slash ("sdmc:/switch/pcl_nx",
// or "sdmc:/" for the card root).
const char *paths_base(void);

// The same directory as a device-less POSIX path WITH a trailing slash
// ("/switch/pcl_nx/"). This is the form the runner wants for its save dir.
const char *paths_base_posix(void);

// out = base + "/" + rel. Returns 0 on success, -1 if it did not fit.
int paths_join(char *out, size_t n, const char *rel);

// One line describing how the base directory was chosen (for the log).
const char *paths_how(void);

#endif
