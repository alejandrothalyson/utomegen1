#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// DEFINIÇÃO DE TIPOS E ESTRUTURAS DO UTOME 1
// ============================================================================

// --- Eventos do Sensor de Toque (Input Manager) ---
enum TouchEvent {
    TOUCH_NONE = 0,
    TOUCH_SINGLE_CLICK,    // Toque curto: Acariciar (Face) / Próxima tarefa (Task)
    TOUCH_DOUBLE_CLICK,    // Duplo toque: Concluir tarefa (Task)
    TOUCH_LONG_PRESS       // Toque longo (>3.0s): Alternar entre Modo Face e Task
};

// --- Modos Principais do Robô ---
enum RobotMode {
    MODE_FACE = 0,         // Modo 1: O Companheiro (expressões, carinho, dormindo)
    MODE_TASK,             // Modo 2: O Organizador (lista de tarefas P1, P2, P3)
    MODE_PAIRING,          // Modo 3: Exibição do código de vinculação (6 dígitos)
    MODE_WIFI_CONFIG,      // Modo 4: Portal de Configuração Wi-Fi (Ponto de Acesso no celular)
    MODE_ALARM,            // Modo 5: Alarme/Lembrete Ativo no Horário Agendado (NTP Scheduler)
    MODE_TRANSITION        // Animação procedural de transição entre os modos
};

// --- Estados Emocionais do Modo Face ---
enum FaceState {
    FACE_NORMAL = 0,       // Olhos abertos atentos
    FACE_BLINKING,         // Piscada suave procedural/bitmap
    FACE_LOOK_LEFT,        // Olhando para a esquerda
    FACE_LOOK_RIGHT,       // Olhando para a direita
    FACE_HEARTS,           // Reação de carinho (corações nos olhos)
    FACE_SLEEPING,         // Modo dormindo suave com "Z z z"
    FACE_LOOK_ANIM,        // Animação de olhar para o lado (bitmaps)
    FACE_RANDOM_BEE,       // Animação aleatória da abelha (bitmaps)
    FACE_RANDOM_PAC        // Animação aleatória do pacman (bitmaps)
};

// --- Prioridades das Tarefas ---
enum TaskPriority {
    PRIORITY_P1 = 1,       // Urgente / Principal (ex: Tomar remédio, Aula)
    PRIORITY_P2 = 2,       // Importante (ex: Almoçar, Dever de casa)
    PRIORITY_P3 = 3        // Relax / Rotina flexível (ex: Guardar brinquedos)
};

// --- Estrutura de uma Tarefa ---
struct TaskItem {
    char id[32];           // Expandido para 32 bytes (suporta IDs de 20 chars do Firestore + null terminator)
    char title[MAX_TASK_TITLE_LEN];
    TaskPriority priority;
    bool completed;
    char time[8];          // Horário agendado da rotina (ex: "08:00")
};

#endif // TYPES_H
