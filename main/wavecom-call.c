#include "wavecom-call.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "tcpip_adapter.h"
#include "lwip/api.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "esp_system.h"
#include "esp_log.h"

#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"

static const char *TAG = "CALL";

static char *peer_ip;
static short peer_port;
static uint16_t peer_nat;

static struct sockaddr_in servaddr;
static struct sockaddr stream_addr;
static int stream_fd;

static char *i2s_out_buff = NULL;
static char *i2s_in_buff = NULL;

void wavecom_connect(void)
{
    char *rcvbuf = malloc(1024);
    char *sndbuf = malloc(1024);

    char *turn_pool = "abcdefgh";
    char *turn_ip = "10.150.20.255";
    short turn_port = 7000; //6000 to 16000 incremented by 1k

    ESP_LOGI(TAG, "Connection Request Initaited");
    ESP_LOGI(TAG, "IP: %s PORT: %d POOL: %s", turn_ip, turn_port, turn_pool);

    //setting up socket for TURN connection
    int resp_len;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, turn_ip, &servaddr.sin_addr); //EX: "123.456.789.123"
    servaddr.sin_port = htons(turn_port);

    stream_addr = *(struct sockaddr *)&servaddr;
    stream_fd = socket(AF_INET, SOCK_DGRAM, 0);

    BaseType_t res = xTaskCreate(&wavecom_send,"call send task",4096,NULL,10,NULL);

    if (res != pdPASS)
    {
        ESP_LOGE(TAG,"error %d when creating task wavecom_send",(int)res);
    }

    res = xTaskCreate(&wavecom_recieve,"call recieve task",4096,NULL,10,NULL);

    if (res != pdPASS)
    {
        ESP_LOGE(TAG,"error %d when creating task wavecom_recieve",(int)res);
    }

    vTaskDelete(NULL);
}

void wavecom_send()
{
    int res = 0;
    double curr_time;
    bool speaker_muted = false;

    if (i2s_out_buff == NULL)
    {
        i2s_out_buff = calloc(sizeof(int8_t),AUDIO_FRAME_SIZE);
    }

    while (true)
    {
        timer_get_counter_time_sec(TIMER_GROUP_0, TIMER_0, &curr_time);

        res = _g711_encode(i2s_out_buff, AUDIO_FRAME_SIZE);

        if (curr_time > 0.1)
        {
            if (i2s_in_buff && !speaker_muted)
            {
                //ESP_LOGI(TAG,"speaker muted");
                memset(i2s_in_buff, 0, 1024);
                _g711_decode(i2s_in_buff, 1024);
                speaker_muted = true;

            }
        }
        else
        {
            speaker_muted = false;
            //ESP_LOGI(TAG,"microphone muted");
            memset(i2s_out_buff, 0, AUDIO_FRAME_SIZE);
        }

        if(res == 0)
        {
            ESP_LOGI(TAG,"_g711_encode returned 0");
            continue;
        }

        res = sendto(stream_fd, i2s_out_buff, AUDIO_FRAME_SIZE, 0, &stream_addr, sizeof(stream_addr));

        if (res != AUDIO_FRAME_SIZE)
        {
            ESP_LOGE(TAG, "Voice call sent %d bytes instead of %d", res, AUDIO_FRAME_SIZE);
        }
    }
    vTaskDelete(NULL);
}

void wavecom_recieve()
{
    int res = 0;

    if (i2s_in_buff == NULL)
    {
        i2s_in_buff = calloc(1024, sizeof(int8_t));
        _g711_decode(i2s_in_buff, res);
    }

    timer_config_t config;
    config.divider = 16;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_START;
    config.alarm_en = TIMER_ALARM_DIS;

    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    while (true)
    {
        res = recvfrom(stream_fd, i2s_in_buff, 1024, 0, NULL, 0);

        if (res < 0)
        {
            ESP_LOGE(TAG, "Voice Call Socket Error errno:%d len:%d", errno, res);
            continue;
        }
        else if (res > 8)
        {
            timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0);
            _g711_decode(i2s_in_buff, res);
        }
    }
    vTaskDelete(NULL);
}