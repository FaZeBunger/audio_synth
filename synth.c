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

enum WaveTypes {
  SINE = ma_waveform_type_sine,
  SAW = ma_waveform_type_sawtooth,
  SQUARE = ma_waveform_type_square,
  TRIANGLE = ma_waveform_type_triangle,
};


/* Data **********************************************************************/
struct my_data_source {
  ma_data_source_base base;
};

/* Callback ******************************************************************/
void data_callback(ma_device* pDevice, void *pOutput, const void * pInput, ma_uint32 frameCount) {
    // In playback mode copy data to pOutput. In capture mode read data from pInput. In full-duplex mode, both
    // pOutput and pInput will be valid and you can move data from pInput into pOutput. Never process more than
    // frameCount frames.
    ma_waveform* pDecoder = (ma_waveform*)(pDevice->pUserData);
    ma_waveform_read_pcm_frames(pDecoder, pOutput, frameCount, NULL);

    (void)pInput;
}

/* Init **********************************************************************/
int main(int argc, char* argv[]) {
  ma_result result;
  ma_device_config deviceConfig;
  ma_device device;
  ma_waveform wave;
  ma_waveform_config wave_config;

  /* if (argc < 2) { */
  /*   printf("No Input File.\n"); */
  /*   return -1; */
  /* } */

  wave_config = ma_waveform_config_init(MY_FORMAT, MY_CHANNEL_COUNT, MY_SAMPLE_RATE, SINE, MY_AMPLITUDE, MY_FREQUENCY);
  result = ma_waveform_init(&wave_config, &wave);
  if (result != MA_SUCCESS) {
    printf("Unable to generate waveform.\n");
    return -2;
  }

  deviceConfig = ma_device_config_init(ma_device_type_playback);
  deviceConfig.playback.format = wave_config.format;
  deviceConfig.playback.channels = wave_config.channels;
  deviceConfig.sampleRate = wave_config.sampleRate;
  deviceConfig.dataCallback = data_callback;
  deviceConfig.pUserData = &wave;

  if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS){
    printf("Failed to open playback device.\n");
    ma_waveform_uninit(&wave);
    return -3;
  }

  if (ma_device_start(&device) != MA_SUCCESS) {
    printf("Failed to start playback device.\n");
    ma_device_uninit(&device);
    ma_waveform_uninit(&wave);
    return -4;
  }

  printf("Press Enter to Quit.\n");
  getchar();

  ma_device_uninit(&device);
  ma_waveform_uninit(&wave);

  return 0;
}
