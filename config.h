#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// UTOME 1 - CONFIGURAÇÃO GERAL DE HARDWARE E TIMINGS
// ============================================================================

// --- Pinagem ESP32-C3 Supermini ---
#define PIN_OLED_SDA            6      // I2C SDA
#define PIN_OLED_SCL            7      // I2C SCL
#define PIN_TOUCH               3      // TTP223 Sensor Capacitivo (saída digital HIGH no toque)

// --- Parâmetros do Display OLED ---
#define SCREEN_WIDTH            128
#define SCREEN_HEIGHT           64
#define I2C_CLOCK_SPEED         200000 // 200 kHz Fast Mode (elimina ruído I2C e pixels chuviscados no ESP32-C3)
#define TARGET_FRAME_MS         33     // ~30 FPS para animações suaves

// --- Mecânica de Input (TTP223) ---
#define TOUCH_DEBOUNCE_MS           25     // Filtro anti-ruído: estabilidade do sinal (elimina pulsos falsos)
#define TOUCH_MIN_INTER_CLICK_MS    35     // Intervalo mínimo entre soltura e novo toque (35ms filtra só jitter elétrico, não cliques humanos)
#define TOUCH_LONG_PRESS_MS         3000   // Toque Longo (> 3.0s): alterna Modo Face/Task
#define DEFAULT_TOUCH_ACTION_TIME   600    // Janela de "pensar" para distinguir 1 ou 2 cliques (600ms é confortável para duplo toque)

// --- Parâmetros de Comportamento e UX ---
#define HEART_ANIM_DURATION_MS  4160   // Duração da animação de carinho (64 frames @ 65ms)
#define SLEEP_TIMEOUT_MS        45000  // Inatividade no Modo Face para dormir (Zzz)
#define BLINK_INTERVAL_MIN_MS   4500   // Intervalo mínimo entre piscadas naturais (4.5s)
#define BLINK_INTERVAL_MAX_MS   8500   // Intervalo máximo entre piscadas (8.5s)

// [M4] Proteção do painel OLED por inatividade (preserva vida útil + não incomoda no quarto)
#define OLED_DIM_TIMEOUT_MS    120000  // 2 min inativo → dim (contraste 30/255)
#define OLED_SLEEP_TIMEOUT_MS  300000  // 5 min inativo → power-save (pixels off)

// --- Limites do Gerenciador de Tarefas ---
#define MAX_TASKS               10
#define MAX_TASK_TITLE_LEN      48

// --- Transição de Modo ---
#define TRANSITION_DURATION_MS  1625   // Duração da animação da bruxa (25 frames @ 65ms)
#define TRANSITION_TIMEOUT_MS   2200   // Watchdog: força conclusão se exceder este tempo

// --- Agendador de Rotinas e Alarme Automático ---
#define ALARM_TIMEOUT_MS        60000  // Duração máxima do alarme visual pulsante no OLED (60s)

// --- Conectividade Wi-Fi e Firebase ---
// ⚠️  SEGREDOS (WIFI_SSID, WIFI_PASSWORD, FIREBASE_API_KEY) ficam em config_secrets.h
//     que NÃO é versionado no Git. Copie config_secrets.example.h e preencha.
#if __has_include("config_secrets.h")
    #include "config_secrets.h"
#elif __has_include("config_secrets.example.h")
    #include "config_secrets.example.h"
#else
    #define WIFI_SSID           "sua_rede_wifi"
    #define WIFI_PASSWORD       "sua_senha_wifi"
    #define FIREBASE_API_KEY    "AIzaSy_SUA_CHAVE_AQUI"
#endif

#define FIREBASE_PROJECT_ID         "utomesite"   // ID do projeto Firebase (público, OK no git)

// --- Configuração de Fuso Horário e NTP (Horário de Brasília/Recife) ---
#define NTP_SERVER_1                "pool.ntp.br"
#define NTP_SERVER_2                "time.google.com"
#define GMT_OFFSET_SEC              (-3 * 3600)            // UTC-3 (Recife / Brasília)
#define DAYLIGHT_OFFSET_SEC         0                      // Sem horário de verão no Brasil
#define WIFI_CONNECT_TIMEOUT_MS     20000                  // Timeout na tentativa inicial de Wi-Fi (20s: cobre associação + margem DHCP)
#define WIFI_RECONNECT_INTERVAL_MS  20000                  // Intervalo de retentativa de conexão em background (20s)
#define FIREBASE_POLL_INTERVAL_MS   60000                  // Consulta periódica do Firestore a cada 60s
#define WIFI_AP_SSID                "UTOME WIFI"           // Nome exato da rede Ponto de Acesso criada pelo robô
#define WIFI_AP_CHANNEL             1                      // Canal 1: 100% compatível e visível em todos os celulares e PCs
#define WIFI_AP_MAX_CONN            4                      // Limite de conexões simultâneas no AP
#define WIFI_DEFAULT_TX_POWER       WIFI_POWER_15dBm       // 15 dBm: Estabiliza alimentação 3.3V do ESP32-C3 Supermini e elimina distorção EVM
#define WIFI_AP_PORTAL_TIMEOUT_MS   180000                 // Timeout do portal de configuração: 3 min

#endif // CONFIG_H

