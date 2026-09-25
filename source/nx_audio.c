/* nx_audio.c -- libnx audout backend without SDL/audren event waits
 *
 * The SDL Switch driver blocks in audrenWaitFrame. On the target setup that
 * path repeatedly returned through a corrupted instruction address. audout is
 * simpler and this backend polls released buffers, avoiding eventWait entirely.
 */

#include <switch.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nx_audio.h"
#include "util.h"

#define NX_AUDIO_BUFFER_COUNT 3
#define NX_AUDIO_FRAMES       1024
#define NX_AUDIO_CHANNELS     2
#define NX_AUDIO_BYTES        (NX_AUDIO_FRAMES * NX_AUDIO_CHANNELS * sizeof(int16_t))
#define NX_AUDIO_ALLOC_BYTES  ((NX_AUDIO_BYTES + 0xFFF) & ~0xFFF)

static AudioOutBuffer s_buffers[NX_AUDIO_BUFFER_COUNT];
static void *s_pcm[NX_AUDIO_BUFFER_COUNT];
static NxAudioMixCallback s_mix = NULL;
static void *s_mix_userdata = NULL;
static pthread_t s_thread;
static volatile int s_running = 0;
static int s_thread_created = 0;
static int s_service_ready = 0;
static int s_output_started = 0;
static int s_ready = 0;
static int s_rate = 48000;

static void fill_and_flush(AudioOutBuffer *buffer) {
  if (s_mix)
    s_mix(s_mix_userdata, (int16_t *)buffer->buffer, NX_AUDIO_FRAMES);
  else
    memset(buffer->buffer, 0, NX_AUDIO_BYTES);
  armDCacheFlush(buffer->buffer, buffer->buffer_size);
}

static void *audio_thread_main(void *unused) {
  (void)unused;
  debugPrintf("nx_audio: worker started thread=%p\n", (void *)pthread_self());

  for (int i = 0; i < NX_AUDIO_BUFFER_COUNT && s_running; i++) {
    fill_and_flush(&s_buffers[i]);
    Result rc = audoutAppendAudioOutBuffer(&s_buffers[i]);
    if (R_FAILED(rc)) {
      debugPrintf("nx_audio: initial append[%d] failed rc=0x%x\n", i, rc);
      s_running = 0;
    }
  }

  debugPrintf("nx_audio: initial buffers submitted\n");
  while (s_running) {
    AudioOutBuffer *released = NULL;
    u32 released_count = 0;
    Result rc = audoutGetReleasedAudioOutBuffer(&released, &released_count);
    if (R_FAILED(rc)) {
      debugPrintf("nx_audio: get released failed rc=0x%x\n", rc);
      break;
    }
    if (!released || released_count == 0) {
      svcSleepThread(2000000); // 2 ms; triple buffering leaves ample headroom.
      continue;
    }

    fill_and_flush(released);
    released->next = NULL;
    rc = audoutAppendAudioOutBuffer(released);
    if (R_FAILED(rc)) {
      debugPrintf("nx_audio: reappend failed rc=0x%x count=%u\n", rc, released_count);
      break;
    }
  }
  s_ready = 0;

  s_running = 0;
  debugPrintf("nx_audio: worker stopped\n");
  return NULL;
}

int nx_audio_init(int requested_rate, NxAudioMixCallback callback, void *userdata) {
  if (s_ready && s_running)
    return 1;
  if (s_service_ready || s_output_started || s_thread_created)
    nx_audio_shutdown();


  s_mix = callback;
  s_mix_userdata = userdata;
  debugPrintf("nx_audio: init requested_rate=%d\n", requested_rate);

  Result rc = audoutInitialize();
  if (R_FAILED(rc)) {
    debugPrintf("nx_audio: audoutInitialize failed rc=0x%x\n", rc);
    return 0;
  }
  s_service_ready = 1;
  s_rate = (int)audoutGetSampleRate();
  debugPrintf("nx_audio: device rate=%d channels=%u format=%u state=%u\n",
              s_rate, audoutGetChannelCount(), audoutGetPcmFormat(), audoutGetDeviceState());

  for (int i = 0; i < NX_AUDIO_BUFFER_COUNT; i++) {
    s_pcm[i] = memalign(0x1000, NX_AUDIO_ALLOC_BYTES);
    if (!s_pcm[i]) {
      debugPrintf("nx_audio: buffer allocation failed index=%d\n", i);
      nx_audio_shutdown();
      return 0;
    }
    memset(s_pcm[i], 0, NX_AUDIO_ALLOC_BYTES);
    s_buffers[i].next = NULL;
    s_buffers[i].buffer = s_pcm[i];
    s_buffers[i].buffer_size = NX_AUDIO_ALLOC_BYTES;
    s_buffers[i].data_size = NX_AUDIO_BYTES;
    s_buffers[i].data_offset = 0;
  }

  rc = audoutStartAudioOut();
  if (R_FAILED(rc)) {
    debugPrintf("nx_audio: audoutStartAudioOut failed rc=0x%x\n", rc);
    nx_audio_shutdown();
    return 0;
  }
  s_output_started = 1;
  s_running = 1;

  int prc = pthread_create(&s_thread, NULL, audio_thread_main, NULL);
  if (prc != 0) {
    debugPrintf("nx_audio: pthread_create failed rc=%d\n", prc);
    s_running = 0;
    nx_audio_shutdown();
    return 0;
  }
  s_thread_created = 1;
  s_ready = 1;
  debugPrintf("nx_audio: ready buffers=%d frames=%d\n",
              NX_AUDIO_BUFFER_COUNT, NX_AUDIO_FRAMES);
  return 1;
}

int nx_audio_is_ready(void) {
  return s_ready && s_running;
}

int nx_audio_get_rate(void) {
  return s_rate;
}

void nx_audio_shutdown(void) {
  s_ready = 0;
  s_running = 0;
  if (s_thread_created) {
    pthread_join(s_thread, NULL);
    s_thread_created = 0;
  }
  if (s_output_started) {
    Result rc = audoutStopAudioOut();
    debugPrintf("nx_audio: stop rc=0x%x\n", rc);
    s_output_started = 0;
  }
  if (s_service_ready) {
    audoutExit();
    s_service_ready = 0;
  }
  for (int i = 0; i < NX_AUDIO_BUFFER_COUNT; i++) {
    free(s_pcm[i]);
    s_pcm[i] = NULL;
    memset(&s_buffers[i], 0, sizeof(s_buffers[i]));
  }
  s_mix = NULL;
  s_mix_userdata = NULL;
}
