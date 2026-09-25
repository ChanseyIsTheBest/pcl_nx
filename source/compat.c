#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <dirent.h>

#include "compat.h"
#include "util.h"

typedef struct {
  uint64_t d_ino;
  int64_t d_off;
  uint16_t d_reclen;
  uint8_t d_type;
  char d_name[256];
} BionicDirent64;

void yy_assert2(const char *file, int line, const char *function,
                 const char *expression) {
  debugPrintf("assertion failed: %s (%s:%d, %s)\n",
              expression ? expression : "?", file ? file : "?", line,
              function ? function : "?");
  abort();
}

char *yy_if_indextoname(unsigned int ifindex, char *ifname) {
  if (!ifname || ifindex == 0) {
    errno = ENXIO;
    return NULL;
  }
  // Networking is stubbed, but the runner enumerates index 1 during startup.
  snprintf(ifname, 16, "%s", ifindex == 1 ? "lo" : "nx0");
  return ifname;
}

char *yy_strcasestr(const char *haystack, const char *needle) {
  if (!haystack || !needle) return NULL;
  if (!*needle) return (char *)haystack;
  const size_t n = strlen(needle);
  for (const char *p = haystack; *p; ++p)
    if (strncasecmp(p, needle, n) == 0) return (char *)p;
  return NULL;
}

time_t yy_timegm(struct tm *value) {
  if (!value) return (time_t)-1;
  int64_t year = (int64_t)value->tm_year + 1900;
  unsigned month = (unsigned)value->tm_mon + 1;
  const unsigned day = (unsigned)value->tm_mday;
  year -= month <= 2;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned)(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + (int64_t)doe - 719468;
  return (time_t)(days * 86400 + value->tm_hour * 3600 +
                  value->tm_min * 60 + value->tm_sec);
}

int yy_scandir64(const char *path, void ***namelist,
                  int (*filter)(const void *),
                  int (*compar)(const void *, const void *)) {
  if (!namelist) {
    errno = EINVAL;
    return -1;
  }
  *namelist = NULL;
  DIR *dir = opendir(path);
  if (!dir) return -1;

  size_t count = 0;
  size_t capacity = 16;
  BionicDirent64 **items = calloc(capacity, sizeof(*items));
  if (!items) {
    closedir(dir);
    return -1;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    BionicDirent64 candidate;
    memset(&candidate, 0, sizeof(candidate));
    candidate.d_ino = entry->d_ino;
    candidate.d_reclen = sizeof(candidate);
    candidate.d_type = entry->d_type;
    snprintf(candidate.d_name, sizeof(candidate.d_name), "%s", entry->d_name);
    if (filter && !filter(&candidate)) continue;

    if (count == capacity) {
      capacity *= 2;
      void *grown = realloc(items, capacity * sizeof(*items));
      if (!grown) goto fail;
      items = grown;
    }
    items[count] = malloc(sizeof(candidate));
    if (!items[count]) goto fail;
    *items[count++] = candidate;
  }
  closedir(dir);

  if (compar && count > 1)
    qsort(items, count, sizeof(*items), compar);
  *namelist = (void **)items;
  return (int)count;

fail:
  closedir(dir);
  for (size_t i = 0; i < count; ++i) free(items[i]);
  free(items);
  return -1;
}
