#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "status_led.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "status_led.h"
#include "esp_now.h"
#include "string.h"
#include "protocol.h"
#include "freertos/semphr.h"
#include "esp_adc/adc_oneshot.h"

#define TELEM_PERIOD_MS 500
#define MAX_DEVICES 16
#define UPDATE_PERIOD_MS 250
#define ESP_NOW_CHANNEL 6
#define STALENESS 1500000
#define ENABLE_MISLEADING_STATE_TEST 0 // STEP 12: Controlled misleading-state test. // Enable ONLY when building firmware for the designated test board
#define TEST_MISLEADING_STATE 0.950f

const uint8_t broadcast_addr[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t g_my_mac[6];
static uint8_t g_my_mac_valid = 0;
static adc_oneshot_unit_handle_t adc_handle;
static volatile float g_lambda = 1.0f;
static volatile float g_sensor;
static volatile int g_def_count = 0;

static volatile float g_x;
static SemaphoreHandle_t g_x_mtx;

static volatile uint32_t g_received_count = 0;
static volatile uint32_t g_forged_count = 0;
static volatile uint32_t g_bad_length_count = 0;
static volatile uint32_t g_bad_magic_count = 0;
static volatile uint32_t g_src_mismatch_count = 0;
static volatile uint32_t g_baseline_seq = 0;
static volatile uint32_t g_last_seq = 0;
static volatile bool g_have_baseline = false;
static volatile uint32_t g_csi_count = 0;
static void csi_probe_cb(void *ctx, wifi_csi_info_t *info);

typedef struct
{
    uint8_t id[6];
    float state;
    float alpha; 
    int64_t last_seen_us;
    uint32_t pkt_count;
    uint8_t flags;
    bool valid;
} neighbor_t;

    static neighbor_t g_nb[MAX_DEVICES];
static SemaphoreHandle_t g_mtx;

static int slot_of_locked(const uint8_t id[6])
{
    for (int i = 0; i < MAX_DEVICES; i++)
    {
        if (!g_nb[i].valid)
        {
            continue;
        }
        if (memcmp(g_nb[i].id, id, 6) == 0)
        {
            return i;
        }
    }
    return -1;
}

static int alloc_slot_locked(const uint8_t id[6])
{
    int index = slot_of_locked(id);
    if (index != -1)
    {
        return index;
    }
    for (int i = 0; i < MAX_DEVICES; i++)
    {
        if (!g_nb[i].valid){
            memcpy(g_nb[i].id, id, 6);
            g_nb[i].valid = true;
            g_nb[i].state = 0.0f;
            g_nb[i].alpha = 1.0f;
            g_nb[i].last_seen_us = 0;
            g_nb[i].pkt_count = 0;
            return i;
        }
    }
    return -1;
}

static void wmsr_on_packet(const swarm_pkt_t *pkt){
    xSemaphoreTake(g_mtx, portMAX_DELAY);
    int idx = alloc_slot_locked(pkt->src_id);

    if(idx >= 0){
        g_nb[idx].state = pkt->state;
        g_nb[idx].flags = pkt->flags;
        g_nb[idx].last_seen_us = esp_timer_get_time();
        g_nb[idx].pkt_count++;
    }

    xSemaphoreGive(g_mtx);


}

static void helper_g_x_update(float new_x){
    xSemaphoreTake(g_x_mtx, portMAX_DELAY);
    g_x=new_x;
    xSemaphoreGive(g_x_mtx);
}

static float helper_g_x_read(void){
    xSemaphoreTake(g_x_mtx, portMAX_DELAY);
    float v=g_x;
    xSemaphoreGive(g_x_mtx);
    return v;

}

static void on_sent(const uint8_t *mac_addr,
                    esp_now_send_status_t status)
{
    
        if (status != ESP_NOW_SEND_SUCCESS){
            //printf("ESP-NOW send: failed\n");
        }

}

static void conf_adc(void){
    adc_oneshot_unit_init_cfg_t init_config={
        .unit_id=ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config,&adc_handle));
    adc_oneshot_chan_cfg_t channel_config={
        .bitwidth= ADC_BITWIDTH_12,
        .atten= ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_6, &channel_config));
}
float read_sensor(void){
    int sum=0;
    for (int i=0; i<16; i++){
        int raw_value;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL_6, &raw_value));
        sum+=raw_value; }
    
    float average= sum / 16.0f;
    float normalized_value= average / 4095.0f;
    return normalized_value;
}

static void sender_task(void *arg)
{
    swarm_pkt_t pkt = {0};
    pkt.magic = PKT_MAGIC;
    memcpy(pkt.src_id, g_my_mac, 6);

    for (;;)
    {
        float real_state = helper_g_x_read();

#if ENABLE_MISLEADING_STATE_TEST
    pkt.state = TEST_MISLEADING_STATE;
    printf("TEST MODE: real=%.3f advertised=%.3f\n",
           real_state, pkt.state);
#else
    pkt.state = real_state;
#endif
        pkt.sensor = g_sensor;     
        esp_err_t err = esp_now_send(
            broadcast_addr,
            (const uint8_t *)&pkt,
            sizeof(pkt));

        if (err != ESP_OK)
        {
          // Suppress non-CSV output during telemetry capture.
          //  printf("ESP-NOW send request failed: %s\n", esp_err_to_name(err));
        }
        pkt.seq++;
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    swarm_pkt_t pkt;
    if (len != sizeof(pkt))
    {
        g_bad_length_count++;
        return;
    }

    memcpy(&pkt, data, sizeof(pkt));

    if (pkt.magic != PKT_MAGIC)
    {
        g_bad_magic_count++;
        return;
    }

    if (memcmp(pkt.src_id, g_my_mac, 6) == 0)
    {
        return;
    }

    if (info == NULL || info->src_addr == NULL ||
        memcmp(info->src_addr, pkt.src_id, 6) != 0)
    {
        g_src_mismatch_count++;
        return;
    }

    wmsr_on_packet(&pkt);
    if (pkt.flags & FLAG_FORGED)
{
    g_forged_count++;
}

    if (!g_have_baseline)
    {
        g_baseline_seq = pkt.seq;
        g_have_baseline = true;
    }
    g_last_seq = pkt.seq;
    g_received_count++;
}


static void example_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_csi_config_t csi_cfg = {
        .lltf_en = true,
        .htltf_en = false,
        .stbc_htltf2_en = false,
        .ltf_merge_en = true,
        .channel_filter_en = false, /* raw, no hardware smoothing */
        .manu_scale = false,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(csi_probe_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));

    ESP_ERROR_CHECK(esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_6M));

#if CONFIG_ESPNOW_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
#endif
}
static void print_station_mac(void)
{
    if (g_my_mac_valid)
    {
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, g_my_mac));
    g_my_mac_valid = 1;
    //printf("Station MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
         //  (unsigned)g_my_mac[0], (unsigned)g_my_mac[1], (unsigned)g_my_mac[2],
         //  (unsigned)g_my_mac[3], (unsigned)g_my_mac[4], (unsigned)g_my_mac[5]);
}
static void print_wifi_channel(void)
{
    uint8_t primary;
    wifi_second_chan_t second;
    ESP_ERROR_CHECK(esp_wifi_get_channel(&primary, &second));
    printf("Wi-Fi primary channel: %u\n", (unsigned)primary);
}
static void init_esp_now_broadcast(void)
{
    ESP_ERROR_CHECK(esp_now_init());
    esp_now_peer_info_t peer = {0};

    ESP_ERROR_CHECK(esp_now_register_send_cb(on_sent));

    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    memcpy(peer.peer_addr, broadcast_addr, sizeof(broadcast_addr));
    peer.channel = ESP_NOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
    //printf("ESP-NOW ready: broadcast peer registered\n");
}
static void csi_probe_cb(void *ctx, wifi_csi_info_t *info)
{
    g_csi_count++;
}


static void print_neighbours_task(void *arg)
{
    for (;;)
    {
        if (xSemaphoreTake(g_mtx, portMAX_DELAY) == pdTRUE)
        {
            // Diagnostic output disabled for CSV telemetry.

            for (int i = 0; i < MAX_DEVICES; i++)
            {
                if (g_nb[i].valid && ((esp_timer_get_time()-g_nb[i].last_seen_us)> STALENESS)){
                    g_nb[i].valid= false;
                    continue;
                }              
            }

           

            xSemaphoreGive(g_mtx);
        }
        #if 0
        printf("my_x=%.3f, sensor=%.3f\n", (double)helper_g_x_read(), (double)read_sensor());
        #endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



static void update_task(void *arg)
{
    float vals[MAX_DEVICES];

    for (;;)
    {
        // Read the local sensor and capture our current estimate.
        float s = read_sensor();
        g_sensor = s;

        float own_x = helper_g_x_read();
        int n = 0;
        int64_t now = esp_timer_get_time();

        // Collect fresh neighbour estimates.
        xSemaphoreTake(g_mtx, portMAX_DELAY);

        for (int i = 0; i < MAX_DEVICES; i++)
        {
            if (g_nb[i].valid &&
                (now - g_nb[i].last_seen_us) <= STALENESS)
            {
                vals[n++] = g_nb[i].state;
            }
        }

        xSemaphoreGive(g_mtx);

                // W-MSR: mark up to F neighbours on each side of own_x.
        uint8_t trimmed[MAX_DEVICES] = {0};

        for (int f = 0; f < F_PARAM; f++)
        {
            int low_idx = -1;
            int high_idx = -1;

            float low_value = own_x;
            float high_value = own_x;

            for (int i = 0; i < n; i++)
            {
                if (trimmed[i])
                    continue;

                if (vals[i] < low_value)
                {
                    low_value = vals[i];
                    low_idx = i;
                }

                if (vals[i] > high_value)
                {
                    high_value = vals[i];
                    high_idx = i;
                }
            }

            if (low_idx >= 0)
                trimmed[low_idx] = 1;

            if (high_idx >= 0 && high_idx != low_idx)
                trimmed[high_idx] = 1;
        }

        int def_count = 0;
        for (int i = 0; i < n; i++)
        {
            if (trimmed[i])
                def_count++;
        }
        g_def_count = def_count;

        // Average the local estimate, sensor anchor and untrimmed neighbours.
        float num = g_lambda * s + own_x;
        float denom = g_lambda + 1.0f;

        for (int i = 0; i < n; i++)
        {
            if (!trimmed[i])
            {
                num += vals[i];
                denom += 1.0f;
            }
        }

        float sum = num / denom;

#if 0
printf("W-MSR: own=%.3f sensor=%.3f | ", own_x, s);

for (int i = 0; i < n; i++)
{
    printf("%.3f%s ",
           vals[i],
           trimmed[i] ? "[TRIMMED]" : "[KEPT]");
}
#endif


helper_g_x_update(sum);

        vTaskDelay(pdMS_TO_TICKS(UPDATE_PERIOD_MS));
    }
}

static void telemetry_task(void *arg)
{
    printf("NODE,TS_MS,STATE,SENSOR,NNB,DEF,RX,FORGED,BADLEN,BADMAGIC,SRCMISMATCH,NEIGHBOURS\n");

    for (;;)
    {
        const int64_t now_us = esp_timer_get_time();
        const uint32_t ts_ms = (uint32_t)(now_us / 1000);
        const float state = helper_g_x_read();
        const float sensor = g_sensor;

        neighbor_t snapshot[MAX_DEVICES];
        int nnb = 0;

        xSemaphoreTake(g_mtx, portMAX_DELAY);

        for (int i = 0; i < MAX_DEVICES; i++)
        {
            if (g_nb[i].valid &&
                (now_us - g_nb[i].last_seen_us) <= STALENESS)
            {
                snapshot[nnb++] = g_nb[i];
            }
        }

        xSemaphoreGive(g_mtx);

        printf("%02X%02X,%lu,%.4f,%.4f,%d,%d,%lu,%lu,%lu,%lu,%lu",
       g_my_mac[4], g_my_mac[5],
       (unsigned long)ts_ms,
       (double)state, (double)sensor, nnb, g_def_count,
       (unsigned long)g_received_count,
       (unsigned long)g_forged_count,
       (unsigned long)g_bad_length_count,
       (unsigned long)g_bad_magic_count,
       (unsigned long)g_src_mismatch_count);

        for (int i = 0; i < nnb; i++)
{
    printf(",%02X%02X:%.3f:%u",
           snapshot[i].id[4],
           snapshot[i].id[5],
           (double)snapshot[i].state,
           (unsigned)snapshot[i].flags);
}

printf("\n");
        vTaskDelay(pdMS_TO_TICKS(TELEM_PERIOD_MS));
    }
}


void app_main(void)
{
    //printf("honest_node: skeleton. Begin at Step 4.\n");
    //printf("sizeof(swarm_pkt_t) = %d\n", (int)sizeof(swarm_pkt_t));
    status_init();
    status_rgb(false, true, false);
    ESP_ERROR_CHECK(nvs_flash_init());
    g_mtx = xSemaphoreCreateMutex();
    g_x_mtx = xSemaphoreCreateMutex();
    if(g_mtx == NULL){
        printf("Failed to create Mutex\n");
        return;
    }

    example_wifi_init();
    conf_adc();
    g_sensor = read_sensor();
    // helper_g_x_update((float)esp_random() / (float)UINT32_MAX);
    helper_g_x_update(read_sensor());
    //printf("initial x = %.3f\n", (double)helper_g_x_read());
    //print_wifi_channel();
    print_station_mac();
    init_esp_now_broadcast();

    xTaskCreate(update_task, "update", 4096, NULL, 5, NULL);
    xTaskCreate(sender_task, "sender", 4096, NULL, 5, NULL);
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 5, NULL);

    xTaskCreate(print_neighbours_task,
                "print_neighbours",
                4096,
                NULL,
                5,
                NULL);

    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
