#ifndef DISPLAY_ENGINE_H
#define DISPLAY_ENGINE_H

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "config.h"
#include "types.h"

// ============================================================================
// DISPLAY ENGINE: MOTOR GRÁFICO PROCEDURAL PARA SSD1306/SSD1309 OLED (U8g2)
// Gerencia expressões dos olhos (piscar, olhar lados, corações, dormir)
// e renderização das tarefas com prioridades P1, P2 e P3.
// ============================================================================

class DisplayEngine {
public:
    DisplayEngine();
    void init();
    void update(uint32_t now);

    // Controle de Modo
    // [FIX-4] getMode() retorna o modo EFETIVO (não o alvo da transição)
    // Use isTransitioning() para saber se a troca ainda não completou.
    void setMode(RobotMode mode);
    RobotMode getMode() const { return _currentMode; }
    RobotMode getTargetMode() const { return _targetModeAfterTransition; }
    bool isTransitioning() const { return _inTransition; }
    void triggerTransition(RobotMode targetMode);

    // Controles do Modo Face
    void triggerHearts();
    void triggerRandomAnimation();
    void resetSleepTimer();
    bool isSleeping() const { return _faceState == FACE_SLEEPING; }

    // Telas e Animações de Inicialização e Pareamento
    void showBootSplash(uint32_t durationMs = 3150);
    void renderConnectingWifiFrame(uint32_t now, const char* ssid);
    void renderWifiApConfigFrame(uint32_t now, const char* apName, const char* ipStr = "192.168.4.1");
    void showWifiConnectedAnimation(const char* ipStr, uint32_t durationMs = 1500);
    void showWifiFailedScreen(uint32_t durationMs = 1600);
    void renderAuthStatusFrame(uint32_t now, const char* statusMsg);
    void showAlreadyPaired(uint32_t durationMs = 1500);
    void showPairingSuccessAnimation(uint32_t durationMs = 2000);
    void showPairingSplash(const char* code, uint32_t durationMs = 3500);

    // Controles do Modo de Pareamento e Wi-Fi Config
    void setPairingCode(const char* code);
    const char* getPairingCode() const { return _pairingCode; }
    void setApName(const char* name);
    const char* getApName() const { return _apName; }

    // Controles do Modo Task
    void setTaskDisplay(const TaskItem* task, uint8_t currentIndex, uint8_t totalTasks);
    void triggerTaskCompletedEffect();

    // Controles do Modo Alarme
    void setAlarmTask(const TaskItem* task);
    bool hasAlarmTask() const { return _hasAlarmTask; }

    // Feedback de Conectividade e Nuvem (Prioridade 2.2)
    void setNetworkStatus(bool wifiConnected, bool cloudConnected, bool isPaired);
    void showFactoryResetScreen(uint32_t durationMs = 2500);

    // Alerta de Limite de Rotinas (Prioridade 2.4)
    void setRoutineLimitWarning(bool exceeded, uint8_t cloudTotal);

private:
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C _u8g2;

    RobotMode _currentMode;
    RobotMode _targetModeAfterTransition;
    bool _inTransition;
    uint32_t _transitionStartTime;
    uint32_t _lastRenderTime;

    // Estado do Pareamento e Wi-Fi
    char _pairingCode[12];
    char _apName[32];

    // Estado de Conectividade (OLED Status Icon)
    bool _wifiConnected;
    bool _cloudConnected;
    bool _isPaired;

    // Estado do Modo Face
    FaceState _faceState;
    uint32_t _lastInteractionTime;
    uint32_t _nextBlinkTime;
    uint32_t _blinkStartTime;
    bool _isBlinking;
    uint32_t _heartStartTime;
    int8_t _lookOffsetCurrent;
    int8_t _lookOffsetTarget;
    uint32_t _nextLookTime;
    uint32_t _lookAnimStartTime;
    uint32_t _randomAnimStartTime;
    uint32_t _nextRandomAnimTime;

    // Estado do Modo Task
    TaskItem _activeTask;
    uint8_t _taskIndex;
    uint8_t _taskTotal;
    bool _hasActiveTask;
    uint32_t _taskCompleteAnimStart;
    bool _taskLimitExceeded;
    uint8_t _cloudRoutinesTotal;

    // [M4] Estado do Dim/Power-Save do OLED
    bool _oledDimmed;      // true quando contraste reduzido
    bool _oledPowerSaved;  // true quando display apagado (setPowerSave(1))

    // Estado do Modo Alarme
    TaskItem _alarmTask;
    bool _hasAlarmTask;
    uint32_t _alarmAnimStart;

    // Métodos Internos de Desenho
    void renderFace(uint32_t now);
    void renderTask(uint32_t now);
    void renderPairing(uint32_t now);
    void renderWifiApConfig(uint32_t now);
    void renderAlarm(uint32_t now);
    void renderTransition(uint32_t now);

    // Primitivas Visuais
    void drawNetworkStatus(uint32_t now);
    void drawEye(int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t pupilOffsetX);
    void drawHeart(int16_t cx, int16_t cy, float scale);
    void drawSleepingEye(int16_t cx, int16_t cy, int16_t w);
    void drawZzz(uint32_t now);
    void drawWifiWaves(int16_t cx, int16_t cy, uint8_t stage);
    void drawBell(int16_t cx, int16_t cy, float angle);
};

#endif // DISPLAY_ENGINE_H
