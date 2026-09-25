/* paths.c -- where the port's files live (see paths.h)
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "config.h"
#include "paths.h"

#define PATH_CAP 512

static char s_base[PATH_CAP]  = "sdmc:/switch/pcl_nx";
static char s_posix[PATH_CAP] = "/switch/pcl_nx/";
static char s_how[PATH_CAP + 64] = "default";

// Folders tried when the launcher gives us nothing usable.
static const char *const s_fallbacks[] = {
  "sdmc:/switch/pcl_nx",
  "sdmc:/switch/PocketCrystalLeague",
  "sdmc:/switch/pocket_crystal_league",
  "sdmc:/switch/Pocket Crystal League",
  "sdmc:/pcl_nx",
};

// "sdmc:/x" -> offset just past "sdmc:" (i.e. of the first '/'), else 0.
static size_t device_len(const char *p) {
  const char *colon = strchr(p, ':');
  const char *slash = strchr(p, '/');
  if (colon && (!slash || colon < slash)) return (size_t)(colon - p) + 1;
  return 0;
}

static int is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

// Make `in` an absolute device path without a trailing slash (except "dev:/").
static int make_abs(const char *in, char *out, size_t n) {
  if (!in || !*in) return 0;
  int w;
  if (device_len(in)) {
    w = snprintf(out, n, "%s", in);
  } else if (in[0] == '/') {
    w = snprintf(out, n, "sdmc:%s", in);         // the default device at boot
  } else {
    char cwd[PATH_CAP];
    if (!getcwd(cwd, sizeof(cwd))) return 0;
    w = snprintf(out, n, "%s/%s", cwd, in);
  }
  if (w <= 0 || (size_t)w >= n) return 0;

  // collapse "//" runs after the device prefix
  const size_t dl = device_len(out);
  char *dst = out + dl;
  for (const char *src = out + dl; *src; src++) {
    if (*src == '/' && dst > out + dl && dst[-1] == '/') continue;
    *dst++ = *src;
  }
  *dst = 0;

  size_t len = strlen(out);
  while (len > dl + 1 && out[len - 1] == '/') out[--len] = 0;
  if (len == dl) { out[len++] = '/'; out[len] = 0; }   // "sdmc:" -> "sdmc:/"
  return 1;
}

static int dirname_of(const char *path, char *out, size_t n) {
  char tmp[PATH_CAP];
  if (!make_abs(path, tmp, sizeof(tmp))) return 0;
  char *slash = strrchr(tmp, '/');
  if (!slash) return 0;
  if (slash > tmp && slash[-1] == ':') slash[1] = 0;   // keep "sdmc:/"
  else *slash = 0;
  return snprintf(out, n, "%s", tmp) < (int)n;
}

static int join(const char *base, const char *rel, char *out, size_t n) {
  const size_t bl = strlen(base);
  const int w = snprintf(out, n, "%s%s%s", base, (bl && base[bl - 1] == '/') ? "" : "/", rel);
  return (w > 0 && (size_t)w < n) ? 0 : -1;
}

// Does this folder look like a Pocket Crystal League install?
static int has_game(const char *dir) {
  char p[PATH_CAP + 64];
  if (join(dir, SO_NAME, p, sizeof(p)) == 0 && is_file(p)) return 1;
  if (join(dir, APK_NAME, p, sizeof(p)) == 0 && is_file(p)) return 1;
  DIR *d = opendir(dir);
  if (!d) return 0;
  int found = 0;
  struct dirent *e;
  while (!found && (e = readdir(d)) != NULL) {
    const size_t l = strlen(e->d_name);
    if (l > 4 && !strcasecmp(e->d_name + l - 4, ".apk")) found = 1;
  }
  closedir(d);
  return found;
}

static void set_base(const char *dir, const char *how) {
  snprintf(s_base, sizeof(s_base), "%s", dir);
  const char *posix = s_base + device_len(s_base);
  const size_t pl = strlen(posix);
  snprintf(s_posix, sizeof(s_posix), "%s%s", posix, (pl && posix[pl - 1] == '/') ? "" : "/");
  snprintf(s_how, sizeof(s_how), "%s: %s", how, s_base);
}

void paths_init(int argc, char *argv[]) {
  char cand[8][PATH_CAP];
  const char *why[8];
  int nc = 0;

  if (argc > 0 && argv && argv[0] && strchr(argv[0], '/') &&
      dirname_of(argv[0], cand[nc], PATH_CAP)) {
    why[nc++] = "argv[0]";
  }
  {
    char cwd[PATH_CAP];
    if (getcwd(cwd, sizeof(cwd)) && make_abs(cwd, cand[nc], PATH_CAP))
      why[nc++] = "cwd";
  }
  for (unsigned i = 0; i < sizeof(s_fallbacks) / sizeof(*s_fallbacks) && nc < 8; i++) {
    snprintf(cand[nc], PATH_CAP, "%s", s_fallbacks[i]);
    why[nc++] = "fallback";
  }

  int chosen = -1;
  for (int i = 0; i < nc && chosen < 0; i++)
    if (is_dir(cand[i]) && has_game(cand[i])) chosen = i;
  if (chosen < 0)
    for (int i = 0; i < nc && chosen < 0; i++)
      if (is_dir(cand[i])) chosen = i;

  if (chosen >= 0) set_base(cand[chosen], why[chosen]);
  else             set_base(s_fallbacks[0], "nothing found, default");

  chdir(s_base);
}

const char *paths_base(void)       { return s_base; }
const char *paths_base_posix(void) { return s_posix; }
const char *paths_how(void)        { return s_how; }

int paths_join(char *out, size_t n, const char *rel) {
  return join(s_base, rel, out, n);
}
