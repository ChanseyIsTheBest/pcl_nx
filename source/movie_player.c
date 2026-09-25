/* movie_player.c -- video playback stub
 *
 * Pocket Crystal League has no video: game.droid calls no video_* function
 * and ships no .mp4/.webm assets. Earlier GameMaker ports needed an
 * ffmpeg-backed FMV player here; keeping the API but stubbing the body lets the
 * jni_fake video paths compile unchanged while dropping ten link libraries
 * (avformat, avcodec, swresample, swscale, avutil, dav1d, opus, vorbisidec,
 * webp, ogg) from the build.
 *
 * If a future build ever adds a cutscene, restore the implementation
 * from the upstream hmd_nx / stt16bit_nx repositories and put the ffmpeg
 * libraries back in the Makefile.
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "movie_player.h"
#include "util.h"

int movie_play(const char *name) {
  debugPrintf("movie_play('%s') -- no video support in this build\n", name ? name : "");
  return 0; // "not found": callers carry on
}

void movie_set_gl_invalidate(void (*fn)(void)) { (void)fn; }
