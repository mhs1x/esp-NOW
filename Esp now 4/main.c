#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h" // biblioteca q ativa a memoria nvs para Wi-Fi/ESP-NOW

// Pinos
#define BTN_1 13
#define BTN_2 12
#define BTN_3 14
#define BTN_4 27
#define LED_LIGADO 2
#define LED_LINK   18
#define BUZZER     5

typedef struct __attribute__((packed)) {
    uint8_t botoes[4];
} pacote_t;

void visual() {
    // leds e buzzer
    gpio_set_direction(LED_LIGADO, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_LINK, GPIO_MODE_OUTPUT);
    gpio_set_direction(BUZZER, GPIO_MODE_OUTPUT);

    // pull up dos botões
    uint64_t mask = (1ULL<<BTN_1) | (1ULL<<BTN_2) | (1ULL<<BTN_3) | (1ULL<<BTN_4);
    gpio_config_t io = { .pin_bit_mask = mask, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&io);

    // som inicial
    gpio_set_level(LED_LIGADO, 1);
    gpio_set_level(BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(BUZZER, 0);
}

void app_main(void) {
    // Inicialização do NVS 
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    visual();
    
    // Inicialização ESP-NOW
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_now_init();

    uint8_t peer_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t peer = { .peer_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} };
    esp_now_add_peer(&peer);

    pacote_t dados = {0};

    while (1) {
        bool mudou = false;
        bool apertou = false;   // clique do led link
        
        uint8_t estados_atuais[4] = {
            !gpio_get_level(BTN_1), !gpio_get_level(BTN_2), 
            !gpio_get_level(BTN_3), !gpio_get_level(BTN_4)
        };

        for(int i=0; i<4; i++) {
            if(dados.botoes[i] != estados_atuais[i]) {
                // Se o estado atual for 1 significa que o botão foi APERTADO
                if(estados_atuais[i] == 1) {
                    apertou = true;
                }
                dados.botoes[i] = estados_atuais[i];
                mudou = true;
            }
        }

        if (apertou) { // Pisca o LED imediatamente se qualquer botão foi apertado
            gpio_set_level(LED_LINK, 1);
            vTaskDelay(pdMS_TO_TICKS(50)); // Pisca rápido
            gpio_set_level(LED_LINK, 0);
        }

        // Envia os dados se houve mudança (seja apertando ou soltando)
        if (mudou) {
            esp_now_send(peer_addr, (uint8_t*)&dados, sizeof(dados));
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}