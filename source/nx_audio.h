/* nx_audio.h -- small polling audout backend for the OpenSL software mixer */

#ifndef __NX_AUDIO_H__
#define __NX_AUDIO_H__

#include <stdint.h>

typedef void (*NxAudioMixCallback)(void *userdata, int16_t *samples, int frames);

int nx_audio_init(int requested_rate, NxAudioMixCallback callback, void *userdata);
int nx_audio_is_ready(void);
int nx_audio_get_rate(void);
void nx_audio_shutdown(void);

#endif
