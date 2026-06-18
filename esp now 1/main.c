#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "comunicador.h"

// Feito por Marcus
#define BOTAO 4

// Definições de Tempo 
#define UNIT_TIME_MS    200  // 1 ponto
#define TEMPO_PONTO_MS  UNIT_TIME_MS
#define TEMPO_TRACO_MS  (UNIT_TIME_MS * 3)
#define ESPACO_SINAL    UNIT_TIME_MS       // Espaço entre . e - da mesma letra
#define ESPACO_LETRA    (UNIT_TIME_MS * 3) // Espaço entre letras
#define ESPACO_PALAVRA  (UNIT_TIME_MS * 7) // Espaço entre palavras

// Tabelas de Tradução
const char* ALFABETO_MORSE[] = {
    ".-",   "-...", "-.-.", "-..",  ".",    "..-.", "--.",  "....", "..", 
    ".---", "-.-",  ".-..", "--",   "-.",   "---",  ".--.", "--.-", ".-.", 
    "...",   "-",    "..-",   "...-", ".--",  "-..-", "-.--", "--.."};

const char* NUMEROS_MORSE[] = {
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."};


void transmitir_mensagem(const char* mensagem) {
    for (int i = 0; i < strlen(mensagem); i++) {
        char c = toupper((unsigned char)mensagem[i]);
        
        if (c >= 'A' && c <= 'Z') {
            printf("Enviando letra: %c\n", c);
            pulsar_morse(ALFABETO_MORSE[c - 'A']);
            vTaskDelay(pdMS_TO_TICKS(ESPACO_LETRA)); 
        } 
        else if (c >= '0' && c <= '9') {
            printf("Enviando numero: %c\n", c);
            pulsar_morse(NUMEROS_MORSE[c - '0']);
            vTaskDelay(pdMS_TO_TICKS(ESPACO_LETRA));}}}

void comunicador() {
    printf("Sistema Transmissor Morse Pronto.\n");

    while (1) {
        // Se o botão for pressionado (Nível 0 devido ao Pull-up)
        if (gpio_get_level(BOTAO) == 0) {
            printf("\n--- Inicio da Transmissao ---\n");
            transmitir_mensagem("SOS 1");
            printf("--- Fim da Transmissao ---\n\n");
            
            // Aguarda o botão ser solto para não repetir sem parar
            while(gpio_get_level(BOTAO) == 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Pequena pausa para economizar CPU
    }
}