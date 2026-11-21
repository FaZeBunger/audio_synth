/* Includes ******************************************************************/
#include <stdio.h>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

/* Definitions ***************************************************************/
#define MY_FORMAT ma_format_f32
#define MY_CHANNEL_COUNT 2
#define MY_SAMPLE_RATE 48000
#define MY_FREQUENCY 440
#define MY_AMPLITUDE 1

// Controls the minimum number of tracks that the TrackList should hold
#define MIN_TRACK_LIST_SIZE 5

typedef enum {
  SINE = ma_waveform_type_sine,         // 0
  SAW = ma_waveform_type_sawtooth,      // 1
  SQUARE = ma_waveform_type_square,     // 2
  TRIANGLE = ma_waveform_type_triangle, // 3
  WAVE_TYPE_COUNT,                      // 4
} WaveTypes;

typedef enum {
  WHITE = ma_noise_type_white,    // 0
  PINK = ma_noise_type_pink,      // 1
  BROWN = ma_noise_type_brownian, // 2
  NOISE_TYPE_COUNT,               // 3
} NoiseTypes;

typedef enum {
  // (0..3)
  SOUND_SINE = SINE,
  SOUND_SAW = SAW,
  SOUND_SQUARE = SQUARE,
  SOUND_TRIANGLE = TRIANGLE,

  // (4..6)
  SOUND_WHITE = WAVE_TYPE_COUNT + WHITE, // (4 + 0) = 4
  SOUND_PINK = WAVE_TYPE_COUNT + PINK,   // (4 + 1) = 5
  SOUND_BROWN = WAVE_TYPE_COUNT + BROWN, // (4 + 2) = 6
  SOUND_TYPE_COUNT,
} SoundType;

typedef enum {
  LPF1,
  LPF2,
  LPFH,
  HPF1,
  HPF2,
  HPFH,
  // TODO: Add more filters
} Filter;

/* Data **********************************************************************/
struct my_data_source {
  ma_data_source_base base;
};

/* deviceConfig = ma_device_config_init(ma_device_type_playback); */
/* deviceConfig.playback.format = wave_config.format; */
/* deviceConfig.playback.channels = wave_config.channels; */
/* deviceConfig.sampleRate = wave_config.sampleRate; */
/* deviceConfig.dataCallback = data_callback; */
/* deviceConfig.pUserData = &wave; */

struct device_data {
  ma_format format;
  ma_uint32 channel_count;
  ma_uint32 sample_rate;
};

typedef struct Track Track;

typedef struct {
  // Reads PCM frames into pFramesOut and returns frames read
  ma_uint64 (*read)(Track *pTrack, void *pFramesOut, ma_uint64 frameCount);

  // Optional: Some Tracks may not implement all of these
  // EX: Filters cannot change frequency in which case can be NULL
  void (*setFrequency)(Track *pTrack, double freq);
  void (*uninit)(Track *pTrack);
} TrackVTable;

struct Track {
  const TrackVTable *pVTable;
  float volume;
  ma_bool32 muted;
};

typedef struct {
  Track base;
  ma_waveform wave;
} WaveTrack;

typedef struct {
  Track base;
  ma_noise noise;
} NoiseTrack;

typedef struct TrackList {
  Track **tracks;
  int num_tracks;
  int max_count;
} TrackList;

/* Implementations ***********************************************************/

/*
 * Returns the number of frames read into frames out.
 */
static ma_uint64 wave_track_read(Track *pTrack, void *pFramesOut,
                                 ma_uint64 frameCount) {
  WaveTrack *pSelf = (WaveTrack *)pTrack;
  ma_uint64 framesRead = 0;
  ma_waveform_read_pcm_frames(&pSelf->wave, pFramesOut, frameCount,
                              &framesRead);
  return framesRead;
}

// Sets frequency of a wave_track
static void wave_track_set_freq(Track *pTrack, double freq) {
  WaveTrack *pSelf = (WaveTrack *)pTrack;
  ma_waveform_set_frequency(&pSelf->wave, freq);
}

static void wave_track_uninit(Track *pTrack) {
  WaveTrack *pSelf = (WaveTrack *)pTrack;
  ma_waveform_uninit(&pSelf->wave);
  free(pSelf);
}

static ma_uint64 noise_track_read(Track *pTrack, void *pFramesOut,
                                  ma_uint64 frameCount) {
  NoiseTrack *pSelf = (NoiseTrack *)pTrack;
  ma_uint64 framesRead = 0;
  ma_noise_read_pcm_frames(&pSelf->noise, pFramesOut, frameCount, &framesRead);
  return framesRead;
}

static void noise_track_uninit(Track *pTrack) {
  NoiseTrack *pSelf = (NoiseTrack *)pTrack;
  ma_noise_uninit(&pSelf->noise, NULL);
  free(pSelf);
}

static const TrackVTable g_NoiseTrackVTable = {
    .read = noise_track_read,
    .uninit = noise_track_uninit,
    .setFrequency = NULL, // Cannot change frequency of random noise
};

static const TrackVTable g_WaveTrackVTable = {
    .read = wave_track_read,
    .setFrequency = wave_track_set_freq,
    .uninit = wave_track_uninit,
};

Track *wave_track_create(WaveTypes type, double amp, double freq) {
  WaveTrack *pTrack = malloc(sizeof(WaveTrack));
  if (!pTrack)
    return NULL;

  pTrack->base.pVTable = &g_WaveTrackVTable;
  pTrack->base.volume = amp;
  pTrack->base.muted = MA_FALSE;

  ma_waveform_config wave_cfg =
      ma_waveform_config_init(MY_FORMAT, MY_CHANNEL_COUNT, MY_SAMPLE_RATE,
                              (ma_waveform_type)type, 1, freq);

  ma_result result = ma_waveform_init(&wave_cfg, &pTrack->wave);
  if (result != MA_SUCCESS) {
    printf("Unable to create WaveTrack.\n");
    return NULL;
  }

  return (Track *)pTrack;
}

/*
 * Returns a Track* specifically a NoiseTrack* with the given amp and seed.
 * Currently uses global format MY_FORMAT
 */
Track *noise_track_create(NoiseTypes type, double amp, ma_uint32 seed) {
  NoiseTrack *pTrack = malloc(sizeof(NoiseTrack));
  if (!pTrack)
    return NULL;

  pTrack->base.pVTable = &g_NoiseTrackVTable;
  pTrack->base.volume = amp;
  pTrack->base.muted = MA_FALSE;

  ma_noise_config cfg = ma_noise_config_init(MY_FORMAT, MY_CHANNEL_COUNT,
                                             (ma_noise_type)type, seed, 1);

  ma_result result = ma_noise_init(&cfg, NULL, &pTrack->noise);
  if (result != MA_SUCCESS) {
    printf("Unable to create Noise Track.\n");
    return NULL;
  }

  return (Track *)pTrack;
}

static inline ma_uint64 track_read(Track *t, void *out, ma_uint64 count) {
  if (!t || t->muted || !t->pVTable->read)
    return 0;
  return t->pVTable->read(t, out, count);
}

static inline void set_track_volume(Track *t, float vol) { t->volume = vol; }

static inline void set_track_frequency(Track *t, double freq) {
  if (!t || !t->pVTable->setFrequency)
    return;
  // If we are here then t is not NULL, and the pointer to setFrequency is not
  // NULL.
  return t->pVTable->setFrequency(t, freq);
}

static inline void mute_track(Track *t) { t->muted = MA_TRUE; }

static inline void unmute_track(Track *t) { t->muted = MA_FALSE; }

static inline void toggle_track_mute(Track *t) {
  if (t->muted) {
    unmute_track(t);
  } else {
    mute_track(t);
  }
}

static inline TrackList *track_list_create() {
  TrackList *pTrackList = malloc(sizeof(TrackList));
  if (pTrackList == NULL) {
    fprintf(stderr, "ERROR: Out of Memory. Could not initialize TrackList.\n");
    return NULL;
  }
  pTrackList->tracks = malloc(sizeof(Track *) * MIN_TRACK_LIST_SIZE);

  if (pTrackList->tracks == NULL) {
    fprintf(stderr, "ERROR: Out of Memory. Could not initialize TrackList.\n");
    return NULL;
  }

  pTrackList->max_count = MIN_TRACK_LIST_SIZE;
  pTrackList->num_tracks = 0;

  return pTrackList;
}

/*
 * Frees all the Tracks and the track list
 */
static inline void track_list_uninit(TrackList *pTrackList) {
  for (int i = 0; i < pTrackList->num_tracks; ++i) {
    pTrackList->tracks[i]->pVTable->uninit(pTrackList->tracks[i]);
  }
  free(pTrackList->tracks);
  free(pTrackList);
}

static inline void append_to_track_list(TrackList *pTrackList, Track *pTrack) {
  if (pTrackList->num_tracks == pTrackList->max_count) {
    Track **new = realloc(pTrackList->tracks,
                          pTrackList->max_count * 2 * sizeof(Track *));
    if (new == NULL) {
      fprintf(stderr,
              "ERROR: Out of Memory. Could not initialize TrackList.\n");
      return;
    }

    pTrackList->tracks = new;
    pTrackList->max_count *= 2;
  }
  pTrackList->tracks[pTrackList->num_tracks] = pTrack;
  pTrackList->num_tracks++;
}

// Sets the number of tracks to zero and uninitializes previous tracks
static inline void clear_track_list(TrackList *pTrackList) {
  for (int i = 0; i < pTrackList->num_tracks; ++i) {
    pTrackList->tracks[i]->pVTable->uninit(pTrackList->tracks[i]);
  }
  pTrackList->num_tracks = 0;
}

/*
 * TODO: Right now because i don't want to. I'm only letting this work for
 * ma_format_f32. Format is defined in MY_FORMAT. Currently this should not be
 * changed from ma_format_f32. Fix this. */
void mix_tracks(TrackList *pTrackList, float *pOutput, ma_uint32 frameCount) {
  // Temporary buffer for one track's output
  float tempBuffer[4096 * 2];
  // Clear master output buffer
  for (ma_uint32 i = 0; i < frameCount * 2; i++) {
    pOutput[i] = 0.0f;
  }

  for (size_t i = 0; i < pTrackList->num_tracks; i++) {
    Track *t = pTrackList->tracks[i];
    ma_uint64 read = track_read(t, tempBuffer, frameCount);

    for (ma_uint64 s = 0; s < read * 2; s++) {
      pOutput[s] += tempBuffer[s] * t->volume;
    }
  }
}

/* Callback ******************************************************************/

/*
  In playback mode copy data to pOutput. In capture mode read data from
  pInput. In full-duplex mode, both pOutput and pInput will be valid and you
  can move data from pInput into pOutput. Never process more than frameCount
  frames.
 */
void data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                   ma_uint32 frameCount) {
  mix_tracks(pDevice->pUserData, pOutput, frameCount);
  (void)pInput;
}
/* Init **********************************************************************/

void device_config_init(ma_device_config *device_cfg, ma_device *device, TrackList* track_list) {
  *device_cfg = ma_device_config_init(ma_device_type_playback);
  device_cfg->playback.format = MY_FORMAT;
  device_cfg->playback.channels = MY_CHANNEL_COUNT;
  device_cfg->sampleRate = MY_SAMPLE_RATE;
  device_cfg->dataCallback = data_callback;
  device_cfg->pUserData = track_list;
}

int main(int argc, char *argv[]) {
  ma_result result;
  ma_device_config deviceConfig;
  ma_device device;
  ma_waveform_config wave_config;

  Track *track1;
  Track *track2;
  Track *track3;
  Track *track4;
  TrackList *track_list;

  track_list = track_list_create();

  track1 = wave_track_create(SQUARE, 0.2, 440);
  track2 = wave_track_create(SQUARE, 0.2, 220);
  track3 = wave_track_create(SQUARE, 0.2, 880);
  track4 = wave_track_create(SQUARE, 0.2, 110);

  append_to_track_list(track_list, track1);
  append_to_track_list(track_list, track2);
  append_to_track_list(track_list, track3);
  append_to_track_list(track_list, track4);

  device_config_init(&deviceConfig, &device, track_list);

  if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
    printf("Failed to open playback device.\n");
    /* ma_waveform_uninit(&wave); */
    return -3;
  }

  if (ma_device_start(&device) != MA_SUCCESS) {
    printf("Failed to start playback device.\n");
    ma_device_uninit(&device);
    /* ma_waveform_uninit(&wave); */
    return -4;
  }

  printf("Press Enter to Quit.\n");
  getchar();

  track_list_uninit(track_list);
  ma_device_uninit(&device);
  /* ma_waveform_uninit(&wave); */

  return 0;
}
