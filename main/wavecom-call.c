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

static int16_t *i2s_out_buff = NULL;
static int16_t *i2s_in_buff = NULL;

void wavecom_connect(void)
{
    char *rcvbuf = malloc(1024);
    char *sndbuf = malloc(1024);

    char *turn_pool = "abcdefgh";
    char *turn_ip = "54.83.79.129";
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

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 2;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(stream_fd, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                   sizeof(receiving_timeout)) < 0)
    {
        ESP_LOGE(TAG, "FAILED TO SET SOCKET TIMEOUT");
    }
    else
    {
        ESP_LOGI(TAG, "SET SOCKET TIMEOUT!");
    }

    sprintf(sndbuf, "%s 1", turn_pool);

    ESP_LOGI(TAG, "Sending: %s", sndbuf);

    int attempt_count = 0;
    bool handshake_success = false;

    do
    {
        resp_len = sendto(stream_fd, sndbuf, strlen(sndbuf), 0, (struct sockaddr *)&servaddr, sizeof(servaddr));

        ESP_LOGI(TAG, "Sent: %d bytes", resp_len);

        resp_len = recvfrom(stream_fd, rcvbuf, sizeof(rcvbuf), 0, NULL, 0);

        if (resp_len < 0)
        {
            ESP_LOGE(TAG, "RECIEVE ERROR");
        }
        else if (resp_len == 0)
        {
            ESP_LOGI(TAG, "BLANK RESPONSE");
        }
        else
        {
            if (rcvbuf[resp_len - 1] != '\0')
                rcvbuf[resp_len] = '\0';

            ESP_LOGI(TAG, "RECIEVED %s", rcvbuf);

            if (strlen(rcvbuf) >= 2)
            {
                handshake_success = true;
                break;
            }
        }
        vTaskDelay(300);
    } while (++attempt_count <= 30);

    if (handshake_success)
    {
        peer_ip = inet_ntoa(*(int *)rcvbuf);
        peer_port = *(short *)(rcvbuf + 4);

        ESP_LOGI(TAG, "Partner Information Recieved: %s %d", peer_ip, peer_port);

        ESP_LOGI(TAG, "Connecting to Peer");

        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        inet_pton(AF_INET, peer_ip, &servaddr.sin_addr); //EX: "123.456.789.123"
        servaddr.sin_port = htons(peer_port);

        stream_addr = *(struct sockaddr *)&servaddr;
        
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
    }

    vTaskDelete(NULL);
}

void wavecom_send()
{
    int res = 0;
    double curr_time;

    if (i2s_out_buff == NULL)
    {
        i2s_out_buff = (int16_t *)malloc(AUDIO_FRAME_SIZE);
    }

    while (true)
    {
        timer_get_counter_time_sec(TIMER_GROUP_0, TIMER_0, &curr_time);

        res = _g711_encode(i2s_out_buff, AUDIO_FRAME_SIZE);

        if (curr_time > 0.1)
        {
            if (i2s_in_buff)
            {
                memset(i2s_in_buff, 0, 2048);
                _g711_decode(i2s_in_buff, 2048);
            }
        }
        else
        {
            memset(i2s_out_buff, 0, AUDIO_FRAME_SIZE);
        }

        res = sendto(stream_fd, i2s_out_buff, AUDIO_FRAME_SIZE*2, 0, &stream_addr, sizeof(stream_addr));

        if (res != AUDIO_FRAME_SIZE*2)
        {
            ESP_LOGE(TAG, "Voice call sent %d bytes instead of %d", res, AUDIO_FRAME_SIZE*2);
        }
    }
    vTaskDelete(NULL);
}

void wavecom_recieve()
{
    int res;

    if (i2s_in_buff == NULL)
    {
        i2s_in_buff = calloc(1024, sizeof(int16_t));
    }

    timer_config_t config;
    config.divider = 16;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_START;
    config.alarm_en = TIMER_ALARM_DIS;

    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    while (true)
    {
        res = recvfrom(stream_fd, i2s_in_buff, 2048, 0, NULL, 0);

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