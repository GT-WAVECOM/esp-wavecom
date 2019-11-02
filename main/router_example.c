// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "i2s_stream.h"
#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"
#include "input_key_service.h"

#include "audio_mem.h"
#include "raw_stream.h"
#include "filter_resample.h"
#include "esp_sip.h"
#include "g711.h"

#include "wavecom-call.h"

#include "mdf_common.h"
#include "mwifi.h"
#include "driver/uart.h"

// #define MEMORY_DEBUG

#define BUF_SIZE (1024)

static const char *TAG = "router_example";

/**
 * @brief Create a tcp client
 */
static int socket_tcp_client_create(const char *ip, uint16_t port)
{
    MDF_PARAM_CHECK(ip);

    MDF_LOGI("Create a tcp client, ip: %s, port: %d", ip, port);

    mdf_err_t ret = ESP_OK;
    int sockfd    = -1;
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr(ip),
    };

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    MDF_ERROR_GOTO(sockfd < 0, ERR_EXIT, "socket create, sockfd: %d", sockfd);

    ret = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in));
    MDF_ERROR_GOTO(ret < 0, ERR_EXIT, "socket connect, ret: %d, ip: %s, port: %d",
                   ret, ip, port);
    return sockfd;

ERR_EXIT:

    if (sockfd != -1) {
        close(sockfd);
    }

    return -1;
}

void tcp_client_read_task(void *arg)
{
    mdf_err_t ret                     = MDF_OK;
    char *data                        = MDF_MALLOC(AUDIO_FRAME_SIZE);
    size_t size                       = AUDIO_FRAME_SIZE;
    uint8_t dest_addr[MWIFI_ADDR_LEN] = {0x0};
    mwifi_data_type_t data_type       = {0x0};
    cJSON *json_root                  = NULL;
    cJSON *json_addr                  = NULL;
    cJSON *json_group                 = NULL;
    cJSON *json_data                  = NULL;
    cJSON *json_dest_addr             = NULL;

    MDF_LOGI("TCP client read task is running");

    while (mwifi_is_connected()) {

        memset(data, 0, AUDIO_FRAME_SIZE);
        //ret = read(g_sockfd, data, size);
        int res = recvfrom(stream_fd, data, AUDIO_FRAME_SIZE, 0, NULL, 0);

        //MDF_LOGD("TCP read, %d, size: %d, data: %s", g_sockfd, size, data);

        // json_root = cJSON_Parse(data);
        // MDF_ERROR_CONTINUE(!json_root, "cJSON_Parse, data format error");

        // /**
        //  * @brief Check if it is a group address. If it is a group address, data_type.group = true.
        //  */
        // json_addr = cJSON_GetObjectItem(json_root, "dest_addr");
        // json_group = cJSON_GetObjectItem(json_root, "group");

        // if (json_addr) {
        //     data_type.group = false;
        //     json_dest_addr = json_addr;
        // } else if (json_group) {
        //     data_type.group = true;
        //     json_dest_addr = json_group;
        // } else {
        //     MDF_LOGW("Address not found");
        //     cJSON_Delete(json_root);
        //     continue;
        // }

        // /**
        //  * @brief  Convert mac from string format to binary
        //  */
        // do {
        //     uint32_t mac_data[MWIFI_ADDR_LEN] = {0};
        //     sscanf(json_dest_addr->valuestring, MACSTR,
        //            mac_data, mac_data + 1, mac_data + 2,
        //            mac_data + 3, mac_data + 4, mac_data + 5);

        //     for (int i = 0; i < MWIFI_ADDR_LEN; i++) {
        //         dest_addr[i] = mac_data[i];
        //     }
        // } while (0);

        // json_data = cJSON_GetObjectItem(json_root, "data");
        // char *send_data = cJSON_PrintUnformatted(json_data);
        if(res>0)
        {
            printf("recieved %d bytes\n",res);

            ret = mwifi_write(NULL, &data_type, data, res, true);
            MDF_ERROR_GOTO(ret != MDF_OK, FREE_MEM, "<%s> mwifi_root_write", mdf_err_to_name(ret));
        } 
    }

FREE_MEM:
        printf("mwifi_write err\n");
        //MDF_FREE(send_data);
        //cJSON_Delete(json_root);

    MDF_LOGI("TCP client read task is exit");

    MDF_FREE(data);
    vTaskDelete(NULL);
}

//forwards messages from mesh to internet
void tcp_client_write_task(void *arg)
{
    int res = 0;
    mdf_err_t ret = MDF_OK;
    char *data    = MDF_CALLOC(1, AUDIO_FRAME_SIZE);
    size_t size   = AUDIO_FRAME_SIZE;
    uint8_t src_addr[MWIFI_ADDR_LEN] = {0x0};
    mwifi_data_type_t data_type      = {0x0};

    MDF_LOGI("TCP client write task is running");

    while (mwifi_is_connected()) {
        size = AUDIO_FRAME_SIZE;
        memset(data, 0, AUDIO_FRAME_SIZE);
        ret = mwifi_root_read(src_addr, &data_type, data, &size, portMAX_DELAY);
        MDF_ERROR_CONTINUE(ret != MDF_OK, "<%s> mwifi_root_read", mdf_err_to_name(ret));

        //MDF_LOGD("TCP write, size: %d, data: %s", size, data);
        //ret = write(g_sockfd, data, size);

        res = sendto(stream_fd, data, size, 0, &stream_addr, sizeof(stream_addr));
        
        printf("forwarding %d bytes\n",res);
    }

    MDF_LOGI("TCP client write task is exit");

    MDF_FREE(data);
    vTaskDelete(NULL);
}

/**
 * @brief Timed printing system information
 */
static void print_system_info_timercb(void *timer)
{
    uint8_t primary                 = 0;
    wifi_second_chan_t second       = 0;
    mesh_addr_t parent_bssid        = {0};
    uint8_t sta_mac[MWIFI_ADDR_LEN] = {0};
    mesh_assoc_t mesh_assoc         = {0x0};
    wifi_sta_list_t wifi_sta_list   = {0x0};

    esp_wifi_get_mac(ESP_IF_WIFI_STA, sta_mac);
    esp_wifi_ap_get_sta_list(&wifi_sta_list);
    esp_wifi_get_channel(&primary, &second);
    esp_wifi_vnd_mesh_get(&mesh_assoc);
    esp_mesh_get_parent_bssid(&parent_bssid);

    MDF_LOGI("System information, channel: %d, layer: %d, self mac: " MACSTR ", parent bssid: " MACSTR
             ", parent rssi: %d, node num: %d, free heap: %u", primary,
             esp_mesh_get_layer(), MAC2STR(sta_mac), MAC2STR(parent_bssid.addr),
             mesh_assoc.rssi, esp_mesh_get_total_node_num(), esp_get_free_heap_size());

    for (int i = 0; i < wifi_sta_list.num; i++) {
        MDF_LOGI("Child mac: " MACSTR, MAC2STR(wifi_sta_list.sta[i].mac));
    }

#ifdef MEMORY_DEBUG

    if (!heap_caps_check_integrity_all(true)) {
        MDF_LOGE("At least one heap is corrupt");
    }

    mdf_mem_print_heap();
    mdf_mem_print_record();
#endif /**< MEMORY_DEBUG */
}

static mdf_err_t wifi_init()
{
    mdf_err_t ret          = nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        MDF_ERROR_ASSERT(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    MDF_ERROR_ASSERT(ret);

    tcpip_adapter_init();
    MDF_ERROR_ASSERT(esp_event_loop_init(NULL, NULL));
    MDF_ERROR_ASSERT(esp_wifi_init(&cfg));
    MDF_ERROR_ASSERT(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    MDF_ERROR_ASSERT(esp_wifi_set_mode(WIFI_MODE_STA));
    MDF_ERROR_ASSERT(esp_wifi_set_ps(WIFI_PS_NONE));
    MDF_ERROR_ASSERT(esp_mesh_set_6m_rate(false));
    MDF_ERROR_ASSERT(esp_wifi_start());

    return MDF_OK;
}

/**
 * @brief All module events will be sent to this task in esp-mdf
 *
 * @Note:
 *     1. Do not block or lengthy operations in the callback function.
 *     2. Do not consume a lot of memory in the callback function.
 *        The task memory of the callback function is only 4KB.
 */
static mdf_err_t event_loop_cb(mdf_event_loop_t event, void *ctx)
{
    MDF_LOGI("event_loop_cb, event: %d", event);

    switch (event) {
        case MDF_EVENT_MWIFI_STARTED:
            MDF_LOGI("MESH is started");
            break;

        case MDF_EVENT_MWIFI_PARENT_CONNECTED:
            MDF_LOGI("Parent is connected on station interface");
            break;

        case MDF_EVENT_MWIFI_PARENT_DISCONNECTED:
            MDF_LOGI("Parent is disconnected on station interface");
            break;

        case MDF_EVENT_MWIFI_ROUTING_TABLE_ADD:
        case MDF_EVENT_MWIFI_ROUTING_TABLE_REMOVE:
            MDF_LOGI("total_num: %d", esp_mesh_get_total_node_num());
            break;

        case MDF_EVENT_MWIFI_ROOT_GOT_IP: {
            MDF_LOGI("Root obtains the IP address. It is posted by LwIP stack automatically");
            // root forward functions
            xTaskCreate(tcp_client_write_task, "tcp_client_write_task", 6 * 1024,
                        NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
            // xTaskCreate(tcp_client_read_task, "tcp_server_read", 4 * 1024,
            //             NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
            break;
        }

        default:
            break;
    }

    return MDF_OK;
}

static audio_element_handle_t raw_read;
static audio_element_handle_t raw_write;

static esp_err_t g711enc_pipeline_open()
{
    audio_element_handle_t i2s_stream_reader;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t recorder = audio_pipeline_init(&pipeline_cfg);
    if (NULL == recorder) {
        return ESP_FAIL;
    }
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_READER;
#if defined CONFIG_ESP_LYRAT_MINI_V1_1_BOARD
    i2s_cfg.i2s_port = 1;
#endif
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);
    audio_element_info_t i2s_info = {0};
    audio_element_getinfo(i2s_stream_reader, &i2s_info);
    i2s_info.bits = I2S_BITS;
    i2s_info.channels = I2S_CHANNEL;
    i2s_info.sample_rates = I2S_SAMPLE_RATE;
    audio_element_setinfo(i2s_stream_reader, &i2s_info);

    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = I2S_SAMPLE_RATE;
    rsp_cfg.src_ch = I2S_CHANNEL;
    rsp_cfg.dest_rate = G711_SAMPLE_RATE;
    rsp_cfg.dest_ch = G711_CHANNEL;
    rsp_cfg.type = AUDIO_CODEC_TYPE_ENCODER;
    audio_element_handle_t filter = rsp_filter_init(&rsp_cfg);

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_read = raw_stream_init(&raw_cfg);
    audio_element_set_output_timeout(raw_read, portMAX_DELAY);

    audio_pipeline_register(recorder, i2s_stream_reader, "i2s");
    audio_pipeline_register(recorder, filter, "filter");
    audio_pipeline_register(recorder, raw_read, "raw");
    audio_pipeline_link(recorder, (const char *[]) {"i2s", "filter", "raw"}, 3);
    audio_pipeline_run(recorder);
    ESP_LOGI(TAG, "Recorder has been created");
    return ESP_OK;
}

int _g711_encode(char *data, int len)
{
    int out_len_bytes;

    char *enc_buffer = (char *)audio_malloc(2 * AUDIO_FRAME_SIZE);
    out_len_bytes = raw_stream_read(raw_read, enc_buffer, 2 * AUDIO_FRAME_SIZE);
    if (out_len_bytes > 0) {
        int16_t *enc_buffer_16 = (int16_t *)(enc_buffer);
        for (int i = 0; i < AUDIO_FRAME_SIZE; i++) {
#ifdef CONFIG_SIP_CODEC_G711A
            data[i] = esp_g711a_encode(enc_buffer_16[i]);
#else
            data[i] = esp_g711u_encode(enc_buffer_16[i]);
#endif
        }
        free(enc_buffer);
        return AUDIO_FRAME_SIZE;
    } else {
        free(enc_buffer);
        return 0;
    }
}

static esp_err_t g711dec_pipeline_open()
{
    audio_element_handle_t i2s_stream_writer;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    audio_pipeline_handle_t speaker = audio_pipeline_init(&pipeline_cfg);
    if (NULL == speaker) {
        return ESP_FAIL;
    }

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_WRITER;
    raw_write = raw_stream_init(&raw_cfg);

    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = G711_SAMPLE_RATE;
    rsp_cfg.src_ch = G711_CHANNEL;
    rsp_cfg.dest_rate = I2S_SAMPLE_RATE;
    rsp_cfg.dest_ch = I2S_CHANNEL;
    rsp_cfg.complexity = 5;
    rsp_cfg.type = AUDIO_CODEC_TYPE_DECODER;
    audio_element_handle_t filter = rsp_filter_init(&rsp_cfg);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);
    audio_element_info_t i2s_info = {0};
    audio_element_getinfo(i2s_stream_writer, &i2s_info);
    i2s_info.bits = I2S_BITS;
    i2s_info.channels = I2S_CHANNEL;
    i2s_info.sample_rates = I2S_SAMPLE_RATE;
    audio_element_setinfo(i2s_stream_writer, &i2s_info);

    audio_pipeline_register(speaker, raw_write, "raw");
    audio_pipeline_register(speaker, filter, "filter");
    audio_pipeline_register(speaker, i2s_stream_writer, "i2s");
    audio_pipeline_link(speaker, (const char *[]) {"raw", "filter", "i2s"}, 3);
    audio_pipeline_run(speaker);
    ESP_LOGI(TAG, "Speaker has been created");
    return ESP_OK;
}

int _g711_decode(char *data, int len)
{
    int16_t *dec_buffer = (int16_t *)audio_malloc(2 * (len - RTP_HEADER_LEN));

    for (int i = 0; i < (len - RTP_HEADER_LEN); i++) {
#ifdef CONFIG_SIP_CODEC_G711A
        dec_buffer[i] = esp_g711a_decode((unsigned char)data[RTP_HEADER_LEN + i]);
#else
        dec_buffer[i] = esp_g711u_decode((unsigned char)data[RTP_HEADER_LEN + i]);
#endif
    }

    raw_stream_write(raw_write, (char *)dec_buffer, 2 * (len - RTP_HEADER_LEN));
    free(dec_buffer);
    return 2 * (len - RTP_HEADER_LEN);
}

static esp_err_t input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    audio_board_handle_t board_handle = (audio_board_handle_t) ctx;
    int player_volume;
    audio_hal_get_volume(board_handle->audio_hal, &player_volume);
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        ESP_LOGI(TAG, "[ * ] input key id is %d", (int)evt->data);
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_REC:
            case INPUT_KEY_USER_ID_PLAY:
                ESP_LOGI(TAG, "[ * ] [Play] input key event");

                BaseType_t res = xTaskCreate(&wavecom_connect,"call connection task",4096,NULL,10,NULL);

                if (res != pdPASS)
                {
                    ESP_LOGE(TAG,"error %d when creating task wavecom_connect",(int)res);
                }

                break;
            case INPUT_KEY_USER_ID_MODE:
            case INPUT_KEY_USER_ID_SET:
                ESP_LOGI(TAG, "[ * ] [Set] input key event");
                // if (sip_state & SIP_STATE_RINGING) {
                //     esp_sip_uas_answer(sip, false);
                // } else if (sip_state & SIP_STATE_ON_CALL) {
                //     esp_sip_uac_bye(sip);
                // } else if ((sip_state & SIP_STATE_CALLING) || (sip_state & SIP_STATE_SESS_PROGRESS)) {
                //     esp_sip_uac_cancel(sip);
                // }
                break;
            case INPUT_KEY_USER_ID_VOLUP:
                ESP_LOGI(TAG, "[ * ] [Vol+] input key event");
                player_volume += 10;
                if (player_volume > 100) {
                    player_volume = 100;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
                break;
            case INPUT_KEY_USER_ID_VOLDOWN:
                ESP_LOGI(TAG, "[ * ] [Vol-] input key event");
                player_volume -= 10;
                if (player_volume < 0) {
                    player_volume = 0;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
                break;
        }
    }

    return ESP_OK;
}

void app_main()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "[1.0] Initialize peripherals management");
    esp_periph_config_t periph_cfg = {
        .task_stack = DEFAULT_ESP_PERIPH_STACK_SIZE,
        .task_prio = CONFIG_MDF_TASK_DEFAULT_PRIOTY,
        .task_core = DEFAULT_ESP_PERIPH_TASK_CORE,
    };

    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[1.2] Initialize and start peripherals");
    audio_board_key_init(set);

    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_BOTH, AUDIO_HAL_CTRL_START);

    ESP_LOGI(TAG, "[ 3 ] Create and start input key service");
    input_key_service_info_t input_key_info[] = INPUT_KEY_DEFAULT_INFO();
    periph_service_handle_t input_ser = input_key_service_create(set);
    input_key_service_add_key(input_ser, input_key_info, INPUT_KEY_NUM);
    periph_service_set_callback(input_ser, input_key_service_cb, (void *)board_handle);

    mwifi_init_config_t cfg   = MWIFI_INIT_CONFIG_DEFAULT();
    mwifi_config_t config     = {
        .router_ssid     = CONFIG_ROUTER_SSID,
        .router_password = CONFIG_ROUTER_PASSWORD,
        .mesh_id         = CONFIG_MESH_ID,
        .mesh_password   = CONFIG_MESH_PASSWORD,
    };

    /**
     * @brief Set the log level for serial port printing.
     */
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    /**
     * @brief Initialize wifi mesh.
     */
    MDF_ERROR_ASSERT(mdf_event_loop_init(event_loop_cb));
    MDF_ERROR_ASSERT(wifi_init());
    MDF_ERROR_ASSERT(mwifi_init(&cfg));
    MDF_ERROR_ASSERT(mwifi_set_config(&config));
    MDF_ERROR_ASSERT(mwifi_start());

    /**
     * @brief select/extend a group memebership here
     *      group id can be a custom address
     */
    const uint8_t group_id_list[2][6] = {{0x01, 0x00, 0x5e, 0xae, 0xae, 0xae},
                                        {0x01, 0x00, 0x5e, 0xae, 0xae, 0xaf}};

    MDF_ERROR_ASSERT(esp_mesh_set_group_id((mesh_addr_t *)group_id_list,
                                           sizeof(group_id_list) / sizeof(group_id_list[0])));

    /**
     * @breif Create handler
     */
    // xTaskCreate(node_write_task, "node_write_task", 4 * 1024,
    //             NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);
    // xTaskCreate(node_read_task, "node_read_task", 4 * 1024,
    //             NULL, CONFIG_MDF_TASK_DEFAULT_PRIOTY, NULL);

    TimerHandle_t timer = xTimerCreate("print_system_info", 10000 / portTICK_RATE_MS,
                                       true, NULL, print_system_info_timercb);
    xTimerStart(timer, 0);

    g711enc_pipeline_open();
    g711dec_pipeline_open();
}
