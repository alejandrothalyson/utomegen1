#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "types.h"

// ============================================================================
// INPUT MANAGER - GERENCIADOR DE TOQUE CAPACITIVO (TTP223)
//
// GARANTIAS:
// 1. Debounce temporal digital completo com leitura contínua (zero repique).
// 2. Intervalo mínimo entre cliques (TOUCH_MIN_INTER_CLICK_MS = 80ms) impede que
//    a soltura do dedo seja contada como um falso 2º clique.
// 3. Toque Longo (> 3.0s) NUNCA é interrompido pela janela de pensar, pois esta
//    só é processada quando o sensor está confirmado solto.
// 4. 1 Toque executa a ação de toque único (avançar tarefa / carinho).
// 5. 2 Toques só disparam com dois toques físicos humanos reais e distintos.
// ============================================================================

class InputManager {
public:
    InputManager();
    void init();

    // Retorna o evento detectado
    TouchEvent update(uint32_t thinkingTime = DEFAULT_TOUCH_ACTION_TIME);

    bool isThinking() const { return _waitingForDecision; }
    bool isPressed() const { return _isPressed; }
    bool readRaw() const { return digitalRead(PIN_TOUCH) == HIGH; }

private:
    bool     _isPressed;             // Nível debounced estável (true = pressionado)
    bool     _lastRawState;          // Última leitura física lida do GPIO
    uint32_t _lastDebounceTime;      // Timestamp da última alteração no nível físico
    uint32_t _pressStartTime;         // Início do pressionamento estável atual
    uint32_t _releaseTime;            // Timestamp da última soltura estável confirmada
    uint32_t _windowStartTime;        // Início da janela de espera após o 1º clique
    uint8_t  _clickCount;             // Quantidade de cliques computados
    bool     _waitingForDecision;     // Se está na janela de espera
    bool     _longPressTriggered;     // Flag de toque longo já disparado
};

#endif // INPUT_MANAGER_H
