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

#include "mdf_common.h"
#include "mwifi.h"

static const char *TAG = "CALL";

static char *peer_ip;
static short peer_port;
static uint16_t peer_nat;

static char *i2s_out_buff = NULL;
static char *i2s_in_buff = NULL;

void wavecom_connect(void)
{
    char *turn_pool = "abcdefgh";
    char *turn_ip = "10.3.141.1";
    short turn_port = 6666; //6000 to 16000 incremented by 1k

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

    mwifi_data_type_t data_type     = {0};
     uint8_t primary                 = 0;
    wifi_second_chan_t second       = 0;
    mesh_addr_t parent_bssid        = {0};
    uint8_t sta_mac[MWIFI_ADDR_LEN] = {0};
    mesh_assoc_t mesh_assoc         = {0x0};
    wifi_sta_list_t wifi_sta_list   = {0x0};



    if (i2s_out_buff == NULL)
    {
        i2s_out_buff = calloc(sizeof(int8_t),AUDIO_FRAME_SIZE);
    }

    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
    memcpy(i2s_out_buff,sta_mac,MWIFI_ADDR_LEN);
    

    while (true)
    {
        if (!mwifi_is_connected()) {
            vTaskDelay(500 / portTICK_RATE_MS);
            continue;
        }
        timer_get_counter_time_sec(TIMER_GROUP_0, TIMER_0, &curr_time);

        res = _g711_encode(i2s_out_buff, SAMPLES_NUM);
        if (curr_time > 0.1)
        {
            if (i2s_in_buff && !speaker_muted)
            {
                //ESP_LOGI(TAG,"speaker muted");
                memset(i2s_in_buff, 0, AUDIO_FRAME_SIZE);
                _g711_decode(i2s_in_buff, SAMPLES_NUM);
                speaker_muted = true;

            }
        }
        else
        {
            speaker_muted = false;
            // ESP_LOGI(TAG,"microphone muted");
            memset(i2s_out_buff + MWIFI_ADDR_LEN, 0, AUDIO_FRAME_SIZE - MWIFI_ADDR_LEN);
        }
        if(res == 0)
        {
            ESP_LOGI(TAG,"_g711_encode returned 0");
            continue;
        }
        res = mwifi_write(NULL, &data_type, i2s_out_buff, PACKET_SIZE, true);

        MDF_ERROR_CONTINUE(res != MDF_OK, "<%s> mwifi_write", mdf_err_to_name(res));
    }
    vTaskDelete(NULL);
}



void wavecom_recieve()
{
    size_t res = 0;
    mdf_err_t ret = MDF_OK;

    mwifi_data_type_t data_type      = {0x0};
    uint8_t src_addr[MWIFI_ADDR_LEN] = {0x0};


    if (i2s_in_buff == NULL)
    {
        i2s_in_buff = calloc(AUDIO_FRAME_SIZE, sizeof(int8_t));
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
        res = PACKET_SIZE;
        ret = mwifi_read(src_addr, &data_type, i2s_in_buff, &res, portMAX_DELAY);
        MDF_ERROR_CONTINUE(ret != MDF_OK, "<%s> mwifi_read", mdf_err_to_name(ret));

        printf("recv %d\n",res);

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

