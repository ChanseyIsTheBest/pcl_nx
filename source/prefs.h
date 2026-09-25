/* prefs.h -- persistent key/value store backing the runner's preference store
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 *
 * On Android the runner forwards its get/set ForKey calls to Java, which stores
 * them in SharedPreferences. We replicate that with a tiny flat-file store so
 * settings and any progress flags the game keeps here survive across launches.
 * The file is "YoYoPrefsFile.txt" in the writable directory, matching the name
 * the GameMaker runner uses on Android.
 */

#ifndef __PREFS_H__
#define __PREFS_H__

void prefs_init(const char *path);
void prefs_flush(void);

const char *prefs_get_string(const char *key, const char *def);
int   prefs_get_bool(const char *key, int def);
int   prefs_get_int(const char *key, int def);
float prefs_get_float(const char *key, float def);

void prefs_set_string(const char *key, const char *val);
void prefs_set_bool(const char *key, int val);
void prefs_set_int(const char *key, int val);
void prefs_set_float(const char *key, float val);
void prefs_delete(const char *key);

#endif
