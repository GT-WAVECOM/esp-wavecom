#include "esp_system.h"

#define RTP_HEADER_LEN 0
#define AUDIO_FRAME_SIZE (512)

#define I2S_SAMPLE_RATE     48000
#define I2S_CHANNEL         2
#define I2S_BITS            16

#define G711_SAMPLE_RATE    16000
#define G711_CHANNEL        1

int _g711_encode(char *data, int len);
int _g711_decode(char *data, int len);

void wavecom_connect(void);
void wavecom_send();
void wavecom_recieve();