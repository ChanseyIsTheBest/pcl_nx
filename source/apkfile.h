/* apkfile.h -- just enough zip to look inside the user's APK
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#ifndef __APKFILE_H__
#define __APKFILE_H__

// 1 if the archive contains an entry with exactly this name.
int apk_has_entry(const char *apk, const char *name);

// Extract one entry to out_path (via out_path.part, CRC-checked).
// Returns 0 on success, negative on failure.
int apk_extract(const char *apk, const char *name, const char *out_path);

#endif
