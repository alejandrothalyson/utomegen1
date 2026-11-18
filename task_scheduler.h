#ifndef TASK_SCHEDULER_H
#define TASK_SCHEDULER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "network_manager.h"

// ============================================================================
// TASK SCHEDULER: AGENDADOR DE ROTINAS E DISPARO AUTOMÁTICO DE ALARMES
// 
// Compara o relógio NTP em tempo real com os horários definidos nas tarefas
// sincronizadas do Firestore (ex: "08:00", "14:30"). Ao atingir o horário
// de uma rotina pendente, desperta o robô e dispara o alerta visual no OLED.
// ============================================================================

class TaskScheduler {
public:
    TaskScheduler();

    void init(TaskItem* tasksList, uint8_t maxTasks);

    // Avalia o horário atual em relação às tarefas pendentes
    // Retorna o índice da tarefa disparada (0 a totalTasks-1), ou -1 se nenhuma
    int8_t update(uint32_t now, const UtomeNetwork& network, uint8_t totalTasks);

    // Estado do alarme ativo
    bool isAlarmActive() const { return _activeAlarmIndex >= 0; }
    int8_t getActiveAlarmIndex() const { return _activeAlarmIndex; }
    uint32_t getAlarmStartTime() const { return _alarmStartTime; }
    bool isAlarmTimedOut(uint32_t now) const;

    // Ações de controle
    void triggerAlarm(uint8_t taskIndex, uint32_t now);
    void dismissActiveAlarm();
    void resetDailyTriggers();
    void notifyTasksChanged(uint8_t totalTasks);

    // Teste imediato (via Monitor Serial)
    int8_t triggerTestAlarm(uint8_t totalTasks, uint32_t now);

private:
    TaskItem* _tasks;
    uint8_t   _maxTasks;
    int8_t    _activeAlarmIndex;
    uint32_t  _alarmStartTime;
    uint32_t  _lastCheckTime;
    int       _lastCheckedDay;

    // Rastreia IDs únicos das rotinas que já tocaram hoje (imune a reordenação)
    char      _triggeredTodayIds[MAX_TASKS][32];
    uint8_t   _triggeredCount;

    // Parser robusto para strings de horário (ex: "08:00", "8:30")
    static bool parseTime(const char* timeStr, int& outHour, int& outMin);
};

#endif // TASK_SCHEDULER_H
