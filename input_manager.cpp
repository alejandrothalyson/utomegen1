#include "input_manager.h"

InputManager::InputManager() :
    _isPressed(false),
    _lastRawState(false),
    _lastDebounceTime(0),
    _pressStartTime(0),
    _releaseTime(0),
    _windowStartTime(0),
    _clickCount(0),
    _waitingForDecision(false),
    _longPressTriggered(false)
{
}

void InputManager::init() {
    // Configura como INPUT: o TTP223 possui saída Push-Pull CMOS ativa (LOW em repouso / HIGH no toque)
    pinMode(PIN_TOUCH, INPUT);

    _isPressed          = false;
    _lastRawState       = (digitalRead(PIN_TOUCH) == HIGH);
    _lastDebounceTime   = millis();
    _pressStartTime     = 0;
    _releaseTime        = 0;
    _windowStartTime    = 0;
    _clickCount         = 0;
    _waitingForDecision = false;
    _longPressTriggered = false;

    Serial.printf("[INPUT] TTP223 inicializado no pino %d (Debounce: %dms, Anti-repique: %dms)\n",
                  PIN_TOUCH, TOUCH_DEBOUNCE_MS, TOUCH_MIN_INTER_CLICK_MS);
}

TouchEvent InputManager::update(uint32_t thinkingTime) {
    uint32_t now = millis();
    bool raw = (digitalRead(PIN_TOUCH) == HIGH);

    // -------------------------------------------------------------------------
    // 1. FILTRO DE DEBOUNCE TEMPORAL (Estabilidade de nível físico)
    // -------------------------------------------------------------------------
    if (raw != _lastRawState) {
        _lastDebounceTime = now;
        _lastRawState     = raw;
    }

    if ((now - _lastDebounceTime) >= TOUCH_DEBOUNCE_MS) {
        if (raw != _isPressed) {
            _isPressed = raw;

            if (_isPressed) {
                // =============================================================
                // BORDA DE SUBIDA CONFIRMADA (Dedo colocado no sensor)
                // =============================================================
                // Proteção contra repique de soltura capacitiva:
                // Um novo toque só é aceito se o sensor ficou solto por pelo
                // menos TOUCH_MIN_INTER_CLICK_MS (80ms). Se ligar mais rápido
                // que isso, é apenas tremor/jitter de soltura do dedo.
                if (_releaseTime == 0 || (now - _releaseTime) >= TOUCH_MIN_INTER_CLICK_MS) {
                    _pressStartTime     = now;
                    _longPressTriggered = false;
                    Serial.printf("[SENSOR-FISICO] Dedo encostado no GPIO %d (HIGH)\n", PIN_TOUCH);
                } else {
                    // Jitter elétrico capacitivo: descarta subida espúria (<35ms após soltura)
                    _isPressed = false;
                    Serial.printf("[SENSOR-FISICO] Jitter filtrado (<%dms apos soltura)\n", TOUCH_MIN_INTER_CLICK_MS);
                }
            } else {
                // =============================================================
                // BORDA DE DESCIDA CONFIRMADA (Dedo retirado do sensor)
                // =============================================================
                _releaseTime = now;
                uint32_t holdTime = now - _pressStartTime;
                Serial.printf("[SENSOR-FISICO] Dedo solto do GPIO %d (LOW, duracao: %ums)\n", PIN_TOUCH, holdTime);

                if (_longPressTriggered) {
                    // O toque longo já foi emitido; ao soltar o dedo, não conta clique.
                    _longPressTriggered = false;
                } else {
                    // Foi um toque curto válido (< 3.0s)!
                    _clickCount++;

                    if (_clickCount >= 2) {
                        // DUPLO CLIQUE CONFIRMADO (dois toques físicos reais)
                        _clickCount         = 0;
                        _waitingForDecision = false;
                        Serial.println("[INPUT] DUPLO CLIQUE confirmado!");
                        return TOUCH_DOUBLE_CLICK;
                    } else {
                        // 1º CLIQUE: inicia janela de "pensar" para aguardar 2º toque
                        _windowStartTime    = now;
                        _waitingForDecision = true;
                        Serial.printf("[INPUT] Clique #1 registrado. Janela: %u ms\n", thinkingTime);
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // 2. DETECÇÃO DE TOQUE LONGO (> 3.0s)
    // Só é avaliado enquanto o sensor estiver ativamente pressionado
    // -------------------------------------------------------------------------
    if (_isPressed && !_longPressTriggered) {
        if (now - _pressStartTime >= TOUCH_LONG_PRESS_MS) {
            _longPressTriggered = true;
            _waitingForDecision = false;
            _clickCount         = 0;
            Serial.printf("[INPUT] TOQUE LONGO (>%dms) confirmado!\n", TOUCH_LONG_PRESS_MS);
            return TOUCH_LONG_PRESS;
        }
    }

    // -------------------------------------------------------------------------
    // 3. RESOLUÇÃO DA JANELA DE "PENSAR" (Decisão de 1 clique único)
    // SÓ pode resolver quando o sensor está CONFIRMADO SOLTO (!_isPressed).
    // O tempo NUNCA interrompe um Toque Longo!
    // -------------------------------------------------------------------------
    if (_waitingForDecision && !_isPressed) {
        if (now - _windowStartTime >= thinkingTime) {
            _waitingForDecision = false;
            uint8_t count       = _clickCount;
            _clickCount         = 0;

            if (count == 1) {
                Serial.println("[INPUT] TOQUE UNICO confirmado (Janela expirada).");
                return TOUCH_SINGLE_CLICK;
            } else if (count >= 2) {
                Serial.printf("[INPUT] DUPLO CLIQUE (%u toques).\n", count);
                return TOUCH_DOUBLE_CLICK;
            }
        }
    }

    return TOUCH_NONE;
}
