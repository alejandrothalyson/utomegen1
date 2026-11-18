#include "task_scheduler.h"

TaskScheduler::TaskScheduler() :
    _tasks(nullptr),
    _maxTasks(0),
    _activeAlarmIndex(-1),
    _alarmStartTime(0),
    _lastCheckTime(0),
    _lastCheckedDay(-1),
    _triggeredCount(0)
{
    memset(_triggeredTodayIds, 0, sizeof(_triggeredTodayIds));
}

void TaskScheduler::init(TaskItem* tasksList, uint8_t maxTasks) {
    _tasks = tasksList;
    _maxTasks = maxTasks;
    _activeAlarmIndex = -1;
    _alarmStartTime = 0;
    _lastCheckTime = 0;
    _lastCheckedDay = -1;
    _triggeredCount = 0;
    memset(_triggeredTodayIds, 0, sizeof(_triggeredTodayIds));
    Serial.println("[SCHEDULER] Agendador de rotinas UTOME inicializado com sucesso.");
}

bool TaskScheduler::parseTime(const char* timeStr, int& outHour, int& outMin) {
    if (!timeStr || strlen(timeStr) < 3) return false;
    int h = 0, m = 0;
    if (sscanf(timeStr, "%d:%d", &h, &m) == 2) {
        if (h >= 0 && h < 24 && m >= 0 && m < 60) {
            outHour = h;
            outMin = m;
            return true;
        }
    }
    return false;
}

int8_t TaskScheduler::update(uint32_t now, const UtomeNetwork& network, uint8_t totalTasks) {
    // 1. Se o relógio NTP ainda não sincronizou, não há como comparar horário com precisão
    if (!network.isTimeSynced()) {
        return -1;
    }

    // 2. Se já existe um alarme ativo tocando agora, aguarda interação ou timeout
    if (_activeAlarmIndex >= 0) {
        return -1;
    }

    // 3. Executa verificação a cada 1 segundo para economia de processamento
    if (now - _lastCheckTime < 1000) {
        return -1;
    }
    _lastCheckTime = now;

    int currentHour = network.getHours();
    int currentMin = network.getMinutes();
    if (currentHour < 0 || currentMin < 0) {
        return -1;
    }

    // 4. Detecção de virada de dia: reseta os disparos diários à meia-noite
    int currentDay = network.getDayOfWeek();
    if (currentDay != _lastCheckedDay) {
        if (_lastCheckedDay != -1) {
            Serial.println("[SCHEDULER] Novo dia detectado. Resetando disparadores diarios de rotina.");
            resetDailyTriggers();
        }
        _lastCheckedDay = currentDay;
    }

    // 5. Varre as tarefas em busca de alguma que coincida com o horário atual
    if (!_tasks || totalTasks == 0) {
        return -1;
    }

    for (uint8_t i = 0; i < totalTasks; i++) {
        // Ignora tarefas já concluídas
        if (_tasks[i].completed) {
            continue;
        }

        // Verifica se esta rotina específica já tocou hoje através do seu ID único
        bool alreadyTriggered = false;
        for (uint8_t k = 0; k < _triggeredCount; k++) {
            if (strlen(_tasks[i].id) > 0 && strcmp(_triggeredTodayIds[k], _tasks[i].id) == 0) {
                alreadyTriggered = true;
                break;
            }
        }
        if (alreadyTriggered) {
            continue;
        }

        int taskHour = 0, taskMin = 0;
        if (parseTime(_tasks[i].time, taskHour, taskMin)) {
            if (taskHour == currentHour && taskMin == currentMin) {
                // Registra o ID único da rotina para não repetir no mesmo dia
                if (_triggeredCount < MAX_TASKS) {
                    if (strlen(_tasks[i].id) > 0) {
                        strncpy(_triggeredTodayIds[_triggeredCount++], _tasks[i].id, 31);
                        _triggeredTodayIds[_triggeredCount - 1][31] = '\0';
                    } else {
                        // Fallback para tarefas sem ID (ex: criadas localmente)
                        char fallbackId[32];
                        snprintf(fallbackId, sizeof(fallbackId), "idx_%u", i);
                        strncpy(_triggeredTodayIds[_triggeredCount++], fallbackId, 31);
                        _triggeredTodayIds[_triggeredCount - 1][31] = '\0';
                    }
                }

                _activeAlarmIndex = i;
                _alarmStartTime = now;

                Serial.println();
                Serial.println("==========================================================");
                Serial.printf("  [SCHEDULER] >>> HORA DA ROTINA ATINGIDA! <<<\n");
                Serial.printf("  Tarefa: [%s] | Horario: %02d:%02d\n", _tasks[i].title, taskHour, taskMin);
                Serial.println("==========================================================");
                Serial.println();

                return i;
            }
        }
    }

    return -1;
}

bool TaskScheduler::isAlarmTimedOut(uint32_t now) const {
    if (_activeAlarmIndex < 0) return false;
    return (now - _alarmStartTime >= ALARM_TIMEOUT_MS);
}

void TaskScheduler::triggerAlarm(uint8_t taskIndex, uint32_t now) {
    if (!_tasks || taskIndex >= _maxTasks) return;
    _activeAlarmIndex = taskIndex;
    _alarmStartTime = now;
    if (_triggeredCount < MAX_TASKS) {
        if (strlen(_tasks[taskIndex].id) > 0) {
            strncpy(_triggeredTodayIds[_triggeredCount++], _tasks[taskIndex].id, 31);
            _triggeredTodayIds[_triggeredCount - 1][31] = '\0';
        }
    }
}

void TaskScheduler::dismissActiveAlarm() {
    _activeAlarmIndex = -1;
    _alarmStartTime = 0;
}

void TaskScheduler::resetDailyTriggers() {
    _triggeredCount = 0;
    memset(_triggeredTodayIds, 0, sizeof(_triggeredTodayIds));
}

void TaskScheduler::notifyTasksChanged(uint8_t totalTasks) {
    // Se o alarme ativo foi removido ou alterado
    if (_activeAlarmIndex >= totalTasks) {
        dismissActiveAlarm();
    }
}

int8_t TaskScheduler::triggerTestAlarm(uint8_t totalTasks, uint32_t now) {
    if (!_tasks || totalTasks == 0) {
        Serial.println("[SCHEDULER] Impossivel disparar teste: nenhuma tarefa carregada.");
        return -1;
    }

    // Prioriza primeira tarefa não concluída
    uint8_t targetIdx = 0;
    for (uint8_t i = 0; i < totalTasks; i++) {
        if (!_tasks[i].completed) {
            targetIdx = i;
            break;
        }
    }

    triggerAlarm(targetIdx, now);
    Serial.printf("[SCHEDULER] Alarme de teste disparado manualmente para tarefa %d: [%s]!\n",
                  targetIdx + 1, _tasks[targetIdx].title);
    return targetIdx;
}
