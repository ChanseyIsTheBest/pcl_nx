#ifndef YY_COMPAT_H
#define YY_COMPAT_H

#include <time.h>

void yy_assert2(const char *file, int line, const char *function,
                 const char *expression);
char *yy_if_indextoname(unsigned int ifindex, char *ifname);
char *yy_strcasestr(const char *haystack, const char *needle);
time_t yy_timegm(struct tm *value);
int yy_scandir64(const char *path, void ***namelist,
                  int (*filter)(const void *),
                  int (*compar)(const void *, const void *));

#endif
