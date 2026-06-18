//////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                                                                                      _              //
//               _    _       _      _        _     _   _   _    _   _   _        _   _  _   _          //
//           |  | |  |_| |\| |_| |\ |_|    |\ |_|   |_| |_| | |  |   |_| |_| |\/| |_| |  |_| | |         //    
//          |_|  |_|  |\  | | | | |/ | |    |/ | |   |   |\  |_|  |_| |\  | | |  | | | |_ | | |_|         //
//                                                                                                      /              //
//////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_version.h"
#include "esp_wifi.h"       // Adicionado para controle direto do Wi-Fi
#include "esp_now.h"
#include "esp_random.h"
#include "nvs_flash.h"      // Adicionado para inicializar a memória interna exigida pelo rádio

#include "HCF_IOTEC.h"
#include "HCF_LCD.h"

// =============================================================================
// ESTRUTURAS DO ESP-NOW E CONSOLE
// =============================================================================
typedef struct __attribute__((packed)) {
    uint8_t botoes[4];
} pacote_t;

QueueHandle_t fila_recebidos = NULL;

// =============================================================================
// ESTRUTURAS DO JOGO DA FORCA
// =============================================================================
typedef enum {
    EVENT_NEXT,
    EVENT_PREV,
    EVENT_CONFIRM
} button_event_t;

typedef enum {
    STATE_MENU,
    STATE_SELECT,
    STATE_CHECK,
    STATE_WIN,
    STATE_LOSE
} game_state_t;

QueueHandle_t button_queue;

char current_word[32];
char hidden_word[32];
char selected_letter = 'A';
int lives = 6;
game_state_t state_jogo;
int last_state = -1;
uint8_t saidas = 0; 

const char *words[] = {
    "AMOR", "COMPUTADOR", "ELETROELETRONICA", "PROGRAMADOR", 
    "COQUEIRO", "ROBO", "SENAI", "PIETRO", "LAPA", 
    "ENZO", "ZARATIM", "LUCAS", "MARIANA", "JULIANO", "MICROCONTROLADOR"
};
#define WORD_COUNT (sizeof(words)/sizeof(words[0]))

// =============================================================================
// CALLBACK ESP-NOW (RECEPÇÃO)
// =============================================================================
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
void ao_receber_dados(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
#else
void ao_receber_dados(const uint8_t *mac_addr, const uint8_t *data, int len) {
#endif
    if (len == sizeof(pacote_t)) {
        pacote_t pacote_temporario;
        memcpy(&pacote_temporario, data, sizeof(pacote_t));
        xQueueSendFromISR(fila_recebidos, &pacote_temporario, NULL);
    }
}

// =============================================================================
// LÓGICA INTERNA DO JOGO
// =============================================================================
static void leds_update(void) {
    saidas = (1 << lives) - 1; 
    io_le_escreve(saidas);
}

static void generate_hidden(void) {
    int len = strlen(current_word);
    for(int i=0; i<len; i++) hidden_word[i] = '_';
    hidden_word[len] = 0;
}

static int check_win(void) {
    return strcmp(current_word, hidden_word) == 0;
}

static void reset_game(void) {
    strcpy(current_word, words[esp_random() % WORD_COUNT]);
    generate_hidden();
    selected_letter = 'A';
    lives = 6;
    leds_update();
}

// =============================================================================
// INTERFACE GRÁFICA (VIA HCF_LCD)
// =============================================================================
static void lcd_render_menu(void) {
    limpar_lcd();
    escreve_lcd(1, 0, " JOGO DA FORCA  ");
    escreve_lcd(2, 0, "B3 -> START     ");
}

static void lcd_render_select(void) {
    char line1[17];
    char line2[17];
    
    snprintf(line1, sizeof(line1), "%-16s", hidden_word);
    snprintf(line2, sizeof(line2), "LETRA: [%c]      ", selected_letter);

    escreve_lcd(1, 0, line1);
    escreve_lcd(2, 0, line2);
}

static void lcd_render_win(void) {
    limpar_lcd();
    escreve_lcd(1, 0, "  VOCE GANHOU!  ");
    
    char line2[17];
    snprintf(line2, sizeof(line2), "%-16s", current_word);
    escreve_lcd(2, 0, line2);
}

static void lcd_render_lose(void) {
    limpar_lcd();
    escreve_lcd(1, 0, "  VOCE PERDEU!  ");
    
    char line2[17];
    snprintf(line2, sizeof(line2), "%-16s", current_word);
    escreve_lcd(2, 0, line2);
}

// =============================================================================
// TASK DO JOGO DA FORCA
// =============================================================================
static void game_task(void *arg) {
    button_event_t ev;
    reset_game();
    state_jogo = STATE_MENU;

    while(1) {
        if(state_jogo != last_state) {
            last_state = state_jogo;
            if(state_jogo == STATE_MENU) lcd_render_menu();
            if(state_jogo == STATE_SELECT) lcd_render_select();
            if(state_jogo == STATE_WIN) lcd_render_win();
            if(state_jogo == STATE_LOSE) lcd_render_lose();
        }

        switch(state_jogo) {
            case STATE_MENU:
                if(xQueueReceive(button_queue, &ev, pdMS_TO_TICKS(100))) {
                    if(ev == EVENT_CONFIRM) {
                        last_state = -1;
                        state_jogo = STATE_SELECT;
                    }
                }
                break;

            case STATE_SELECT:
                if(xQueueReceive(button_queue, &ev, pdMS_TO_TICKS(100))) {
                    if(ev == EVENT_NEXT) {
                        selected_letter++;
                        if(selected_letter > 'Z') selected_letter = 'A';
                        lcd_render_select();
                    }
                    if(ev == EVENT_PREV) {
                        selected_letter--;
                        if(selected_letter < 'A') selected_letter = 'Z';
                        lcd_render_select();
                    }
                    if(ev == EVENT_CONFIRM) {
                        state_jogo = STATE_CHECK;
                    }
                }
                break;

            case STATE_CHECK: {
                int hit = 0;
                for(int i=0; i<strlen(current_word); i++) {
                    if(current_word[i] == selected_letter) {
                        hidden_word[i] = selected_letter;
                        hit = 1;
                    }
                }

                if(!hit) {
                    lives--;
                    leds_update();
                }

                if(check_win()) state_jogo = STATE_WIN;
                else if(lives <= 0) state_jogo = STATE_LOSE;
                else {
                    last_state = -1;
                    state_jogo = STATE_SELECT;
                }
                break;
            }

            case STATE_WIN:
            case STATE_LOSE:
                vTaskDelay(pdMS_TO_TICKS(3000));
                reset_game();
                last_state = -1;
                state_jogo = STATE_MENU;
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =============================================================================
// TASK DO RÁDIO (TRADUTOR DE CONTROLE)
// =============================================================================
void vTask_ProcessarConsole(void *pvParameters) {
    pacote_t pacote_atual;
    button_event_t acao;

    while (1) {
        if (xQueueReceive(fila_recebidos, &pacote_atual, portMAX_DELAY) == pdTRUE) {
            if (pacote_atual.botoes[0] == 1) { 
                acao = EVENT_PREV;
                xQueueSend(button_queue, &acao, 0);
            } 
            else if (pacote_atual.botoes[1] == 1) { 
                acao = EVENT_NEXT;
                xQueueSend(button_queue, &acao, 0);
            } 
            else if (pacote_atual.botoes[2] == 1) { 
                acao = EVENT_CONFIRM;
                xQueueSend(button_queue, &acao, 0);
            }
        }
    }
}

// =============================================================================
// MAIN
// =============================================================================
void app_main(void) {
    // Inicialização da memória NVS (Obrigatório para ligar o rádio Wi-Fi interno)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Inicialização do Hardware
    iniciar_iotec();
    iniciar_lcd();
    
    // Inicializa filas do sistema
    fila_recebidos = xQueueCreate(8, sizeof(pacote_t));
    button_queue = xQueueCreate(10, sizeof(button_event_t));

    // Inicialização Isolada do Wi-Fi para o ESP-NOW (Idêntica ao controle)
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    // Força o console a ficar no canal 1 (Padrão inicial do modo STA)
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    // Inicializa o protocolo ESP-NOW e registra a função de recebimento
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(ao_receber_dados));

    // Inicia as Tarefas
    if (fila_recebidos != NULL && button_queue != NULL) {
        xTaskCreate(vTask_ProcessarConsole, "ProcessarRadio", 2048, NULL, 4, NULL);
        xTaskCreate(game_task, "GameTask", 4096, NULL, 5, NULL);
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}