#ifndef FIREBASE_SYNC_H
#define FIREBASE_SYNC_H

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "config.h"
#include "types.h"
#include "network_manager.h"  // Para WifiCredential e UtomeNetwork

// ============================================================================
// FIREBASE SYNC - CLIENTE REST PARA GOOGLE CLOUD FIRESTORE & AUTH
// Com isolamento em tarefa FreeRTOS em background e validação CA oficial
// ============================================================================

// Item enfileirado para envio assíncrono de conclusão de rotina
struct TaskCompletionEvent {
    char id[32];
    char title[MAX_TASK_TITLE_LEN];
    time_t timestamp;
};

class FirebaseSync {
public:
    FirebaseSync();
    void init();
    void update(uint32_t now, bool isNetworkReady, TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded, bool& tasksChanged);

    // Injeta o ponteiro para o NetworkManager (chamado no setup do utome1.ino)
    void setNetworkManager(UtomeNetwork* net) { _network = net; }

    bool isAuthenticated() const { return _authenticated; }
    bool isPaired() const { return _isPaired; }
    bool isCodePublished() const { return _codePublished; }
    String getDeviceUid() const { return _deviceUid; }
    const String& getPairingCode() const { return _pairingCode; }
    String getOwnerUid() const { return _ownerUid; }
    bool isRoutineLimitExceeded() const { return _routineLimitExceeded; }
    uint8_t getTotalCloudRoutines() const { return _totalCloudRoutines; }

    bool syncRoutinesNow(TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded);
    bool reportTaskCompleted(const TaskItem* task);

    bool ensureReady(TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded);
    bool checkJustPaired();
    bool generatePairingCode();
    void clearPairing();

    void triggerManualSync() { _forceSync = true; }
    void printSyncStatus() const;
    void resetDeviceIdentity();
    bool fetchWifiNetworks();          // Baixa redes Wi-Fi do Firestore e injeta no NetworkManager

    // Cache local NVS (Offline Fail-Safe) — Público para carregar no boot offline (C5)
    uint8_t loadTasksFromFlash(TaskItem* tasksList, uint8_t maxTasks);
    void saveTasksToFlash(const TaskItem* tasksList, uint8_t count);

    // Inicialização da tarefa FreeRTOS em background (C1 Definitivo)
    void startBackgroundTask(TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded);

private:
    bool     _authenticated;
    bool     _isPaired;
    bool     _justPaired;
    bool     _codePublished;
    bool     _forceSync;
    bool     _routineLimitExceeded;
    uint8_t  _totalCloudRoutines;
    uint32_t _lastPollTime;
    uint32_t _lastPairCheckTime;
    uint32_t _tokenExpireTime;
    uint32_t _lastAuthRetryTime;
    uint32_t _lastPairCodeRetryTime;
    uint32_t _lastWifiNetworksPoll;
    uint8_t  _authFailCount;      // Contador de falhas consecutivas de refreshToken (auto-cura após 3)

    String   _deviceUid;
    String   _idToken;
    String   _refreshToken;
    String   _ownerUid;
    String   _pairingCode;

    UtomeNetwork* _network;

    // Recursos FreeRTOS para isolamento de thread e fila assíncrona
    QueueHandle_t     _completionQueue;
    SemaphoreHandle_t _dataMutex;
    TaskHandle_t      _cloudTaskHandle;
    bool              _taskRunning;

    TaskItem*         _sharedTasks;
    uint8_t           _sharedMaxTasks;
    uint8_t*          _sharedTotalLoaded;
    bool              _tasksChangedPending;

    // Configuração de cliente HTTPS com validação CA oficial (A1)
    void configureClient(WiFiClientSecure& client);

    // Métodos Internos de Autenticação e Firestore
    bool authenticateAnonymous();
    bool refreshAuthToken();
    bool checkDeviceRegistration();
    bool fetchRoutinesFromCloud(TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded);
    uint32_t getAdaptivePollIntervalMs() const;

    // Processamento da fila de tarefas concluídas (A9)
    void processCompletionQueue();
    bool sendCompletionToCloud(const TaskCompletionEvent& evt);

    // Worker da tarefa FreeRTOS em background
    static void cloudTaskTrampoline(void* arg);
    void cloudTaskWorker();
    bool syncRoutinesInternal();
};

#endif // FIREBASE_SYNC_H
