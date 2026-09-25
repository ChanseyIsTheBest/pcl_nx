/* apkfile.c -- just enough zip to look inside the user's APK
 *
 * Used at startup to (a) confirm the APK really is Pocket Crystal League
 * (assets/game.droid present) and (b) pull lib/arm64-v8a/libyoyo.so out of it
 * when the user did not extract it themselves. Handles stored and deflated
 * entries; APKs are never zip64 at this size.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "apkfile.h"
#include "util.h"

typedef struct {
  uint16_t method;
  uint32_t crc, csize, usize, local_off;
} ZipEntry;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int find_entry(FILE *f, const char *name, ZipEntry *out) {
  if (fseek(f, 0, SEEK_END) != 0) return -1;
  const long size = ftell(f);
  if (size < 22) return -1;

  // End of central directory: last 22 bytes + up to 64 KiB of comment.
  const long scan = size < 22 + 65535 ? size : 22 + 65535;
  uint8_t *tail = malloc((size_t)scan);
  if (!tail) return -1;
  if (fseek(f, size - scan, SEEK_SET) != 0 || fread(tail, 1, (size_t)scan, f) != (size_t)scan) { free(tail); return -1; }

  long eocd = -1;
  for (long i = scan - 22; i >= 0; i--)
    if (rd32(tail + i) == 0x06054b50) { eocd = i; break; }
  if (eocd < 0) { free(tail); return -1; }
  const uint32_t cd_size = rd32(tail + eocd + 12);
  const uint32_t cd_off  = rd32(tail + eocd + 16);
  free(tail);
  if ((long)cd_off + (long)cd_size > size) return -1;

  uint8_t *cd = malloc(cd_size ? cd_size : 1);
  if (!cd) return -1;
  if (fseek(f, cd_off, SEEK_SET) != 0 || fread(cd, 1, cd_size, f) != cd_size) { free(cd); return -1; }

  const size_t nlen = strlen(name);
  int found = -1;
  for (uint32_t p = 0; p + 46 <= cd_size;) {
    if (rd32(cd + p) != 0x02014b50) break;
    const uint16_t fnl = rd16(cd + p + 28), exl = rd16(cd + p + 30), cml = rd16(cd + p + 32);
    if (p + 46 + fnl > cd_size) break;
    if (fnl == nlen && !memcmp(cd + p + 46, name, nlen)) {
      out->method    = rd16(cd + p + 10);
      out->crc       = rd32(cd + p + 16);
      out->csize     = rd32(cd + p + 20);
      out->usize     = rd32(cd + p + 24);
      out->local_off = rd32(cd + p + 42);
      found = 0;
      break;
    }
    p += 46u + fnl + exl + cml;
  }
  free(cd);
  return found;
}

static long data_offset(FILE *f, const ZipEntry *e) {
  uint8_t lh[30];
  if (fseek(f, e->local_off, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30) return -1;
  if (rd32(lh) != 0x04034b50) return -1;
  return (long)e->local_off + 30 + rd16(lh + 26) + rd16(lh + 28);
}

int apk_has_entry(const char *apk, const char *name) {
  FILE *f = fopen(apk, "rb");
  if (!f) return 0;
  ZipEntry e;
  const int ok = find_entry(f, name, &e) == 0;
  fclose(f);
  return ok;
}

int apk_extract(const char *apk, const char *name, const char *out_path) {
  FILE *f = fopen(apk, "rb");
  if (!f) return -1;
  ZipEntry e;
  long off;
  if (find_entry(f, name, &e) != 0 || (off = data_offset(f, &e)) < 0 ||
      (e.method != 0 && e.method != 8) || fseek(f, off, SEEK_SET) != 0) {
    fclose(f);
    return -2;
  }

  char tmp_path[640];
  snprintf(tmp_path, sizeof(tmp_path), "%s.part", out_path);
  FILE *o = fopen(tmp_path, "wb");
  if (!o) { fclose(f); return -3; }

  enum { CHUNK = 256 * 1024 };
  uint8_t *in = malloc(CHUNK), *outb = malloc(CHUNK);
  int rc = (in && outb) ? 0 : -4;
  uLong crc = crc32(0L, Z_NULL, 0);
  uint32_t left = e.csize, written = 0;

  if (!rc && e.method == 0) {
    while (left && !rc) {
      const size_t want = left > CHUNK ? CHUNK : left;
      if (fread(in, 1, want, f) != want) { rc = -5; break; }
      crc = crc32(crc, in, (uInt)want);
      if (fwrite(in, 1, want, o) != want) rc = -6;
      left -= (uint32_t)want;
      written += (uint32_t)want;
    }
  } else if (!rc) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) rc = -7;
    int zr = Z_OK;
    while (!rc && zr != Z_STREAM_END) {
      if (zs.avail_in == 0 && left) {
        const size_t want = left > CHUNK ? CHUNK : left;
        if (fread(in, 1, want, f) != want) { rc = -5; break; }
        zs.next_in = in;
        zs.avail_in = (uInt)want;
        left -= (uint32_t)want;
      }
      zs.next_out = outb;
      zs.avail_out = CHUNK;
      zr = inflate(&zs, Z_NO_FLUSH);
      if (zr != Z_OK && zr != Z_STREAM_END) { rc = -8; break; }
      const size_t got = CHUNK - zs.avail_out;
      crc = crc32(crc, outb, (uInt)got);
      if (got && fwrite(outb, 1, got, o) != got) rc = -6;
      written += (uint32_t)got;
      if (zr != Z_STREAM_END && zs.avail_in == 0 && !left && got == 0) { rc = -9; break; }
    }
    inflateEnd(&zs);
  }

  free(in);
  free(outb);
  fclose(f);
  if (fclose(o) != 0 && !rc) rc = -6;

  if (!rc && (written != e.usize || (uint32_t)crc != e.crc)) {
    debugPrintf("apk: %s: size %u/%u crc %08x/%08x mismatch\n", name,
                (unsigned)written, (unsigned)e.usize, (unsigned)crc, (unsigned)e.crc);
    rc = -10;
  }
  if (rc) {
    unlink(tmp_path);
    return rc;
  }
  unlink(out_path);                 // Horizon will not rename over a file
  if (rename(tmp_path, out_path) != 0) {
    unlink(tmp_path);
    return -11;
  }
  return 0;
}
