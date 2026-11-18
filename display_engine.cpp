#include "display_engine.h"
#include "bitmaps.h"
#include <math.h>

DisplayEngine::DisplayEngine() :
    _u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ PIN_OLED_SCL, /* data=*/ PIN_OLED_SDA),
    _currentMode(MODE_FACE),
    _targetModeAfterTransition(MODE_FACE),
    _inTransition(false),
    _transitionStartTime(0),
    _faceState(FACE_NORMAL),
    _lastInteractionTime(0),
    _nextBlinkTime(0),
    _blinkStartTime(0),
    _isBlinking(false),
    _heartStartTime(0),
    _lookOffsetCurrent(0),
    _lookOffsetTarget(0),
    _nextLookTime(0),
    _lookAnimStartTime(0),
    _randomAnimStartTime(0),
    _nextRandomAnimTime(0),
    _taskIndex(0),
    _taskTotal(0),
    _hasActiveTask(false),
    _taskCompleteAnimStart(0),
    _hasAlarmTask(false),
    _alarmAnimStart(0),
    _lastRenderTime(0),
    _wifiConnected(false),
    _cloudConnected(false),
    _isPaired(false),
    _taskLimitExceeded(false),
    _cloudRoutinesTotal(0),
    _oledDimmed(false),
    _oledPowerSaved(false)
{
    memset(&_activeTask, 0, sizeof(TaskItem));
    memset(&_alarmTask, 0, sizeof(TaskItem));
    memset(_pairingCode, 0, sizeof(_pairingCode));
    memset(_apName, 0, sizeof(_apName));
}

void DisplayEngine::setAlarmTask(const TaskItem* task) {
    if (task) {
        memcpy(&_alarmTask, task, sizeof(TaskItem));
        _hasAlarmTask = true;
    } else {
        memset(&_alarmTask, 0, sizeof(TaskItem));
        _hasAlarmTask = false;
    }
    _alarmAnimStart = millis();
    _lastInteractionTime = millis();
}

void DisplayEngine::setPairingCode(const char* code) {
    if (code) {
        strncpy(_pairingCode, code, sizeof(_pairingCode) - 1);
        _pairingCode[sizeof(_pairingCode) - 1] = '\0';
    } else {
        _pairingCode[0] = '\0';
    }
}

void DisplayEngine::setApName(const char* name) {
    if (name) {
        strncpy(_apName, name, sizeof(_apName) - 1);
        _apName[sizeof(_apName) - 1] = '\0';
    } else {
        _apName[0] = '\0';
    }
}

void DisplayEngine::setNetworkStatus(bool wifiConnected, bool cloudConnected, bool isPaired) {
    _wifiConnected = wifiConnected;
    _cloudConnected = cloudConnected;
    _isPaired = isPaired;
}

void DisplayEngine::setRoutineLimitWarning(bool exceeded, uint8_t cloudTotal) {
    _taskLimitExceeded = exceeded;
    _cloudRoutinesTotal = cloudTotal;
}

void DisplayEngine::showFactoryResetScreen(uint32_t durationMs) {
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        _u8g2.clearBuffer();
        _u8g2.setFont(u8g2_font_7x14B_tf);
        int16_t w1 = _u8g2.getStrWidth("RESET WI-FI");
        _u8g2.drawStr((128 - w1) / 2, 22, "RESET WI-FI");

        _u8g2.drawHLine(14, 28, 100);

        _u8g2.setFont(u8g2_font_helvB08_tf);
        int16_t w2 = _u8g2.getStrWidth("3x boots detectados");
        _u8g2.drawStr((128 - w2) / 2, 42, "3x boots detectados");

        int16_t w3 = _u8g2.getStrWidth("Abrindo Portal AP...");
        _u8g2.drawStr((128 - w3) / 2, 56, "Abrindo Portal AP...");

        _u8g2.sendBuffer();
        delay(30);
    }
}

void DisplayEngine::drawNetworkStatus(uint32_t now) {
    // Canto superior esquerdo: x=2, y=2 (área 10x8 pixels totalmente livre dos olhos)
    int16_t x = 2;
    int16_t y = 2;

    if (!_wifiConnected) {
        // [SEM WI-FI] Pisca antena com corte diagonal a cada 1000ms
        if ((now % 1000) < 650) {
            _u8g2.drawPixel(x + 4, y + 6);
            _u8g2.drawLine(x + 2, y + 4, x + 6, y + 4);
            _u8g2.drawLine(x + 1, y + 2, x + 7, y + 2);
            // Barra diagonal riscando o símbolo
            _u8g2.drawLine(x, y + 7, x + 8, y + 1);
        }
    } else if (!_cloudConnected) {
        // [WI-FI OK, MAS SEM NUVEM / CONECTANDO] Antena sólida + ponto de exclamação pulsando
        _u8g2.drawPixel(x + 4, y + 6);
        _u8g2.drawLine(x + 2, y + 4, x + 6, y + 4);
        _u8g2.drawLine(x + 1, y + 2, x + 7, y + 2);

        if ((now % 800) < 500) {
            _u8g2.drawLine(x + 10, y + 2, x + 10, y + 4);
            _u8g2.drawPixel(x + 10, y + 6);
        }
    } else if (!_isPaired) {
        // [ONLINE, AGUARDANDO VÍNCULO NO SITE] Antena sólida + 'P' pequenino
        _u8g2.drawPixel(x + 4, y + 6);
        _u8g2.drawLine(x + 2, y + 4, x + 6, y + 4);
        _u8g2.drawLine(x + 1, y + 2, x + 7, y + 2);

        _u8g2.setFont(u8g2_font_4x6_tf);
        _u8g2.drawStr(x + 10, y + 7, "P");
    } else {
        // [TOTALMENTE CONECTADO E PAREADO] Ícone elegante de sinal Wi-Fi
        _u8g2.drawPixel(x + 4, y + 6);
        _u8g2.drawLine(x + 2, y + 4, x + 6, y + 4);
        _u8g2.drawLine(x + 1, y + 2, x + 7, y + 2);
    }
}

void DisplayEngine::init() {
    pinMode(PIN_OLED_SDA, INPUT_PULLUP);
    pinMode(PIN_OLED_SCL, INPUT_PULLUP);
    Wire.setPins(PIN_OLED_SDA, PIN_OLED_SCL);
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    Wire.setClock(I2C_CLOCK_SPEED);
    _u8g2.setBusClock(I2C_CLOCK_SPEED);
    _u8g2.begin();
    _u8g2.setContrast(140); // 140: elimina pixel blooming (sangramento de luz) e garante preto puro com bordas nítidas

    // [FIX-6] Seed único usando esp_random() — TRNG do ESP32-C3
    uint32_t now = millis();
    randomSeed(esp_random());

    _lastInteractionTime = now;
    _nextBlinkTime = now + random(BLINK_INTERVAL_MIN_MS, BLINK_INTERVAL_MAX_MS);
    _nextLookTime = now + random(15000, 25000);
    _nextRandomAnimTime = now + random(45000, 90000);
}

void DisplayEngine::setMode(RobotMode mode) {
    _currentMode = mode;
    _inTransition = false;
    _lastInteractionTime = millis();
    if (_currentMode == MODE_FACE) {
        _faceState = FACE_NORMAL;
    }
}

void DisplayEngine::triggerTransition(RobotMode targetMode) {
    if (_inTransition) return;
    _inTransition = true;
    _transitionStartTime = millis();
    _targetModeAfterTransition = targetMode;
}

void DisplayEngine::triggerHearts() {
    uint32_t now = millis();
    _faceState           = FACE_HEARTS;
    _heartStartTime      = now;
    _lastInteractionTime = now;
}

void DisplayEngine::triggerRandomAnimation() {
    uint32_t now = millis();
    _randomAnimStartTime = now;
    _lastInteractionTime = now;
    // Sorteia 50% Abelha (82 frames) ou 50% Pac-man (62 frames)
    if (random(0, 2) == 0) {
        _faceState = FACE_RANDOM_BEE;
    } else {
        _faceState = FACE_RANDOM_PAC;
    }
}

void DisplayEngine::resetSleepTimer() {
    _lastInteractionTime = millis();
    if (_faceState == FACE_SLEEPING) {
        _faceState = FACE_NORMAL;
    }
}

void DisplayEngine::setTaskDisplay(const TaskItem* task, uint8_t currentIndex, uint8_t totalTasks) {
    if (task && totalTasks > 0) {
        memcpy(&_activeTask, task, sizeof(TaskItem));
        _hasActiveTask = true;
    } else {
        _hasActiveTask = false;
        memset(&_activeTask, 0, sizeof(TaskItem));
    }
    _taskIndex = currentIndex;
    _taskTotal = totalTasks;
    _lastInteractionTime = millis();
}

void DisplayEngine::triggerTaskCompletedEffect() {
    // [B2] _taskCompleteAnimActive removido (não era lido em nenhum lugar); estado
    // de conclusão é controlado diretamente por _activeTask.completed
    uint32_t now = millis();
    _taskCompleteAnimStart  = now;
    _activeTask.completed   = true;
    _lastInteractionTime    = now;
}

void DisplayEngine::update(uint32_t now) {
    if (now - _lastRenderTime < TARGET_FRAME_MS) {
        return; // Mantém taxa de FPS constante (~30 FPS)
    }
    _lastRenderTime = now;

    // [M4] Dim/Sleep OLED progressivo após inatividade prolongada
    // — Protege a vida útil do painel e não incomoda no quarto à noite.
    // Não aplica durante alarme ativo (precisa chamar atenção).
    if (_currentMode != MODE_ALARM && _currentMode != MODE_WIFI_CONFIG) {
        uint32_t idle = now - _lastInteractionTime;
        if (idle >= OLED_SLEEP_TIMEOUT_MS) {
            // Power-save total: apaga o display (pixels off, controlador ligado)
            if (!_oledPowerSaved) {
                _u8g2.setPowerSave(1);
                _oledPowerSaved = true;
                _oledDimmed     = false;
            }
            return; // Não renderiza nada quando em power-save
        } else if (idle >= OLED_DIM_TIMEOUT_MS) {
            // Dim suave: reduz brilho para 20% (valor 30/255)
            if (!_oledDimmed) {
                _u8g2.setContrast(30);
                _oledDimmed     = true;
                _oledPowerSaved = false;
            }
        } else {
            // Ativo: contraste normal (140)
            if (_oledDimmed || _oledPowerSaved) {
                _u8g2.setPowerSave(0);
                _u8g2.setContrast(140);
                _oledDimmed     = false;
                _oledPowerSaved = false;
            }
        }
    } else if (_oledDimmed || _oledPowerSaved) {
        // Alarme ativo → restaura brilho imediatamente
        _u8g2.setPowerSave(0);
        _u8g2.setContrast(140);
        _oledDimmed     = false;
        _oledPowerSaved = false;
    }

    _u8g2.clearBuffer();

    if (_inTransition) {
        renderTransition(now);
    } else if (_currentMode == MODE_PAIRING) {
        renderPairing(now);
    } else if (_currentMode == MODE_WIFI_CONFIG) {
        renderWifiApConfig(now);
    } else if (_currentMode == MODE_ALARM) {
        renderAlarm(now);
    } else if (_currentMode == MODE_FACE) {
        renderFace(now);
    } else if (_currentMode == MODE_TASK) {
        renderTask(now);
    }

    _u8g2.sendBuffer();
}

// ============================================================================
// RENDERIZAÇÃO PROCEDURAL DO MODO FACE
// ============================================================================

void DisplayEngine::renderFace(uint32_t now) {
    // [FIX-5] Proteção defensiva: só renderiza se estiver no Modo Face
    if (_currentMode != MODE_FACE) return;

    // 1. Verifica tempo de inatividade para dormir (exceto durante animações ativas)
    bool inSpecialAnim = (_faceState == FACE_HEARTS || 
                          _faceState == FACE_RANDOM_BEE || 
                          _faceState == FACE_RANDOM_PAC ||
                          _faceState == FACE_LOOK_ANIM);

    if (!inSpecialAnim) {
        if (now - _lastInteractionTime > SLEEP_TIMEOUT_MS) {
            _faceState = FACE_SLEEPING;
        } else if (_faceState == FACE_SLEEPING) {
            _faceState = FACE_NORMAL;
        }
    }

    // 2. Animação de Carinho (64 frames de corações via bitmap)
    if (_faceState == FACE_HEARTS) {
        uint32_t elapsed = now - _heartStartTime;
        if (elapsed >= HEART_ANIM_DURATION_MS) {
            _faceState = FACE_NORMAL;
            _lastInteractionTime = now;
        } else {
            uint8_t frame = (elapsed * HEART_FRAME_COUNT) / HEART_ANIM_DURATION_MS;
            if (frame >= HEART_FRAME_COUNT) frame = HEART_FRAME_COUNT - 1;
            _u8g2.setDrawColor(1);
            _u8g2.drawBitmap(0, 0, 16, 64, epd_bitmap_heart_allArray[frame]);
            drawNetworkStatus(now);
            return;
        }
    }

    // 3. Animação Aleatória: Abelha (82 frames @ 65ms = 5330ms)
    if (_faceState == FACE_RANDOM_BEE) {
        const uint32_t beeDuration = 5330;
        uint32_t elapsed = now - _randomAnimStartTime;
        if (elapsed >= beeDuration) {
            _faceState = FACE_NORMAL;
            _nextRandomAnimTime = now + random(60000, 120000);
        } else {
            uint8_t frame = (elapsed * BEE_FRAME_COUNT) / beeDuration;
            if (frame >= BEE_FRAME_COUNT) frame = BEE_FRAME_COUNT - 1;
            _u8g2.setDrawColor(1);
            _u8g2.drawBitmap(0, 0, 16, 64, epd_bitmap_bee_allArray[frame]);
            drawNetworkStatus(now);
            return;
        }
    }

    // 4. Animação Aleatória: Pacman (62 frames @ 65ms = 4030ms)
    if (_faceState == FACE_RANDOM_PAC) {
        const uint32_t pacDuration = 4030;
        uint32_t elapsed = now - _randomAnimStartTime;
        if (elapsed >= pacDuration) {
            _faceState = FACE_NORMAL;
            _nextRandomAnimTime = now + random(60000, 120000);
        } else {
            uint8_t frame = (elapsed * PAC_FRAME_COUNT) / pacDuration;
            if (frame >= PAC_FRAME_COUNT) frame = PAC_FRAME_COUNT - 1;
            _u8g2.setDrawColor(1);
            _u8g2.drawBitmap(0, 0, 16, 64, epd_bitmap_pac_allArray[frame]);
            drawNetworkStatus(now);
            return;
        }
    }

    // 5. Animação de Olhar para o Lado (25 frames @ 75ms = 1875ms)
    if (_faceState == FACE_LOOK_ANIM) {
        const uint32_t lookDuration = 1875;
        uint32_t elapsed = now - _lookAnimStartTime;
        if (elapsed >= lookDuration) {
            _faceState = FACE_NORMAL;
            _nextLookTime = now + random(18000, 35000);
        } else {
            uint8_t frame = (elapsed * FACE_LOOK_FRAME_COUNT) / lookDuration;
            if (frame >= FACE_LOOK_FRAME_COUNT) frame = FACE_LOOK_FRAME_COUNT - 1;
            _u8g2.setDrawColor(1);
            _u8g2.drawBitmap(0, 0, 16, 64, epd_bitmap_face_allArray[frame]);
            drawNetworkStatus(now);
            return;
        }
    }

    // 6. Modo Dormindo (Olhos fechados serenos com Zzz procedural)
    if (_faceState == FACE_SLEEPING) {
        drawSleepingEye(42, 34, 26);
        drawSleepingEye(86, 34, 26);
        drawZzz(now);
        drawNetworkStatus(now);
        return;
    }

    // 7. Gatilho autônomo para Animação Aleatória (Abelha ou Pacman a cada 1-2 minutos)
    if (now >= _nextRandomAnimTime && !_isBlinking) {
        triggerRandomAnimation();
        return;
    }

    // 8. Gatilho autônomo para Olhar para o Lado (a cada 18-35s)
    if (now >= _nextLookTime && !_isBlinking) {
        _faceState = FACE_LOOK_ANIM;
        _lookAnimStartTime = now;
        return;
    }

    // 9. Lógica de Piscada Natural (5 frames de normal_1 a normal_5 via bitmap)
    if (!_isBlinking && now >= _nextBlinkTime) {
        _isBlinking = true;
        _blinkStartTime = now;
    }

    uint8_t normalFrame = 0; // normal_1 = índice 0 (olhos abertos normais)

    if (_isBlinking) {
        uint32_t blinkProgress = now - _blinkStartTime;
        const uint32_t blinkDuration = 275; // 5 frames @ 55ms = 275ms para piscada completa

        if (blinkProgress >= blinkDuration) {
            _isBlinking = false;
            _nextBlinkTime = now + random(BLINK_INTERVAL_MIN_MS, BLINK_INTERVAL_MAX_MS);
            normalFrame = 0;
        } else {
            // normal_1 a normal_5 é a sequência natural de piscada completa (aberto -> meio -> fechado -> meio -> aberto)
            normalFrame = (blinkProgress * NORMAL_FRAME_COUNT) / blinkDuration;
            if (normalFrame >= NORMAL_FRAME_COUNT) normalFrame = NORMAL_FRAME_COUNT - 1;
        }
    }

    // Desenha a expressão principal (olhos normais / piscada suave em bitmap)
    _u8g2.setDrawColor(1);
    _u8g2.drawBitmap(0, 0, 16, 64, epd_bitmap_normal_allArray[normalFrame]);

    // [PRIORIDADE 2.2] Ícone de conectividade no canto superior esquerdo
    drawNetworkStatus(now);
}

void DisplayEngine::drawEye(int16_t cx, int16_t cy, int16_t w, int16_t h, int16_t pupilOffsetX) {
    int16_t x0 = cx - (w / 2);
    int16_t y0 = cy - (h / 2);
    int16_t r = (h > 14) ? 8 : (h / 2);
    if (r < 1) r = 1;

    // Contorno arredondado do olho
    _u8g2.drawRBox(x0, y0, w, h, r);

    // Reflexo / pupila expressiva interna (dá vida e fofura ao robô)
    if (h >= 20) {
        int16_t pupilX = cx + pupilOffsetX;
        int16_t pupilY = cy - 3;
        _u8g2.setColorIndex(0); // Desenha em preto vazado dentro do olho branco
        _u8g2.drawDisc(pupilX + 2, pupilY, 4);
        _u8g2.setColorIndex(1); // Brilho especular branco
        _u8g2.drawDisc(pupilX + 4, pupilY - 1, 1);
    }
}

void DisplayEngine::drawHeart(int16_t cx, int16_t cy, float scale) {
    int16_t r = (int16_t)(7.0f * scale);
    int16_t offset = (int16_t)(5.5f * scale);
    int16_t tipY = cy + (int16_t)(12.0f * scale);

    // Lóbulo esquerdo e direito
    _u8g2.drawDisc(cx - offset, cy - offset / 2, r);
    _u8g2.drawDisc(cx + offset, cy - offset / 2, r);

    // Preenchimento triangular inferior para fechar o coração perfeito
    _u8g2.drawTriangle(cx - offset - r + 1, cy - offset / 2,
                       cx + offset + r - 1, cy - offset / 2,
                       cx, tipY);
    _u8g2.drawBox(cx - offset, cy - offset / 2, offset * 2, offset);
}

void DisplayEngine::drawSleepingEye(int16_t cx, int16_t cy, int16_t w) {
    int16_t x0 = cx - (w / 2);
    int16_t x1 = cx + (w / 2);

    // Arco de olho fechado suave e tranquilo (^_^)
    _u8g2.drawLine(x0, cy + 2, x0 + 4, cy);
    _u8g2.drawLine(x0 + 4, cy, x1 - 4, cy);
    _u8g2.drawLine(x1 - 4, cy, x1, cy + 2);

    _u8g2.drawLine(x0, cy + 3, x0 + 4, cy + 1);
    _u8g2.drawLine(x0 + 4, cy + 1, x1 - 4, cy + 1);
    _u8g2.drawLine(x1 - 4, cy + 1, x1, cy + 3);
}

void DisplayEngine::drawZzz(uint32_t now) {
    _u8g2.setFont(u8g2_font_helvB08_tf);
    // [FIX-7] Cast explícito uint32_t -> uint8_t após % para evitar narrowing
    uint8_t step = (uint8_t)((now / 350u) % 4u);

    if (step >= 1) _u8g2.drawStr(100, 24, "z");
    if (step >= 2) _u8g2.drawStr(108, 17, "z");
    if (step >= 3) _u8g2.drawStr(116, 10, "Z");
}

// ============================================================================
// RENDERIZAÇÃO DO MODO TASK (ORGANIZADOR DE TAREFAS)
// ============================================================================

void DisplayEngine::renderTask(uint32_t now) {
    // 1. Cabeçalho Superior
    _u8g2.setFont(u8g2_font_6x10_tf);
    _u8g2.drawStr(2, 10, "UTOME TAREFAS");

    // Paginação no canto superior direito (ex: 1/3 ou 1/10* se exceder limite)
    if (_taskTotal > 0) {
        char pageStr[10];
        if (_taskLimitExceeded) {
            snprintf(pageStr, sizeof(pageStr), "%d/%d*", _taskIndex + 1, _taskTotal);
        } else {
            snprintf(pageStr, sizeof(pageStr), "%d/%d", _taskIndex + 1, _taskTotal);
        }
        int16_t w = _u8g2.getStrWidth(pageStr);
        _u8g2.drawStr(126 - w, 10, pageStr);
    }
    _u8g2.drawHLine(0, 13, 128);

    if (!_hasActiveTask || _taskTotal == 0) {
        _u8g2.setFont(u8g2_font_7x14_tf);
        int16_t w1 = _u8g2.getStrWidth("Sem Tarefas!");
        _u8g2.drawStr((128 - w1) / 2, 33, "Sem Tarefas!");

        _u8g2.setFont(u8g2_font_5x8_tf);
        int16_t w2 = _u8g2.getStrWidth("Crie rotinas no site :)");
        _u8g2.drawStr((128 - w2) / 2, 47, "Crie rotinas no site :)");

        int16_t w3 = _u8g2.getStrWidth("Toque longo volta p/ Face");
        _u8g2.drawStr((128 - w3) / 2, 59, "Toque longo volta p/ Face");
        return;
    }

    // 2. Badge de Prioridade (P1, P2 ou P3)
    const char* pBadge = "[P2]";
    const char* pLabel = "IMPORTANTE";
    if (_activeTask.priority == PRIORITY_P1) {
        pBadge = "[P1]";
        pLabel = "URGENTE";
    } else if (_activeTask.priority == PRIORITY_P3) {
        pBadge = "[P3]";
        pLabel = "ROTINA";
    }

    _u8g2.setFont(u8g2_font_6x10_tf);
    _u8g2.drawStr(2, 26, pBadge);
    _u8g2.setFont(u8g2_font_5x8_tf);
    _u8g2.drawStr(30, 25, pLabel);

    // 3. Título / Descrição da Tarefa (com quebra suave de linha)
    // [A12] drawUTF8 em vez de drawStr para suporte a acentos (Remédio, Almoço, Natação)
    _u8g2.setFont(u8g2_font_7x14_tf);

    // [B4] Se tarefa concluída, reserva linha 53–64 para badge → título só ocupa até y=50
    bool taskDone = _activeTask.completed;
    uint8_t titleY1 = taskDone ? 38 : 42; // 1 linha
    uint8_t titleY2 = taskDone ? 37 : 40; // linha 1 de 2
    uint8_t titleY3 = taskDone ? 50 : 53; // linha 2 de 2

    size_t len = strlen(_activeTask.title);
    if (len <= 15) {
        _u8g2.drawUTF8(2, titleY1, _activeTask.title);
    } else {
        // Encontra o último espaço antes do limite de 15 caracteres
        int splitIdx = -1;
        for (int i = 15; i > 0; i--) {
            if (_activeTask.title[i] == ' ') {
                splitIdx = i;
                break;
            }
        }
        char line1[24] = {0};
        char line2[24] = {0};
        if (splitIdx > 0) {
            strncpy(line1, _activeTask.title, splitIdx);
            line1[splitIdx] = '\0';
            strncpy(line2, _activeTask.title + splitIdx + 1, sizeof(line2) - 1);
            line2[sizeof(line2) - 1] = '\0';
        } else {
            strncpy(line1, _activeTask.title, 15);
            line1[15] = '\0';
            strncpy(line2, _activeTask.title + 15, sizeof(line2) - 1);
            line2[sizeof(line2) - 1] = '\0';
        }
        _u8g2.drawUTF8(2, titleY2, line1);
        _u8g2.drawUTF8(2, titleY3, line2);
    }

    // 4. Rodapé / Status da Tarefa
    // [B4] Badge "CONCLUÍDA" com acento via UTF-8, posicionado ABAIXO do título
    if (taskDone) {
        _u8g2.drawRBox(0, 54, 128, 10, 2);
        _u8g2.setColorIndex(0);
        _u8g2.setFont(u8g2_font_5x8_tf);
        const char* badge = "* CONCLU\xC3\xADDA! *"; // UTF-8: Í = \xC3\xAD
        int16_t bw = _u8g2.getUTF8Width(badge);
        _u8g2.drawUTF8((128 - bw) / 2, 62, badge);
        _u8g2.setColorIndex(1);
    } else {
        _u8g2.setFont(u8g2_font_4x6_tf);
        if (_taskLimitExceeded && _taskIndex == _taskTotal - 1) {
            _u8g2.drawStr(2, 63, "1x: Prox | Limite max 10!");
        } else {
            _u8g2.drawStr(2, 63, "1x: Proxima  |  2x: Concluir");
        }
    }
}

// ============================================================================
// TRANSIÇÃO DE MODO VIA BITMAP (ANIMAÇÃO DA BRUXA - 25 FRAMES)
// Alterna magicamente entre Face e Task (e vice-versa)
// ============================================================================
void DisplayEngine::renderTransition(uint32_t now) {
    uint32_t progress = now - _transitionStartTime;

    // Watchdog: força conclusão se exceder tempo máximo
    if (progress >= TRANSITION_DURATION_MS || progress >= TRANSITION_TIMEOUT_MS) {
        _inTransition = false;
        _currentMode  = _targetModeAfterTransition;
        if (_currentMode == MODE_FACE && _faceState == FACE_HEARTS && (millis() - _heartStartTime >= HEART_ANIM_DURATION_MS)) {
            _faceState = FACE_NORMAL;
        }
        const char* mStr = "FACE";
        if (_currentMode == MODE_TASK) mStr = "TASK";
        else if (_currentMode == MODE_PAIRING) mStr = "PAIRING";
        else if (_currentMode == MODE_WIFI_CONFIG) mStr = "WIFI_CONFIG";
        else if (_currentMode == MODE_ALARM) mStr = "ALARM";
        Serial.printf("[DISPLAY] Transicao -> %s\n", mStr);
        return;
    }

    // Calcula o frame atual da bruxa (0 a 24)
    uint8_t frame = (progress * BRUXA_FRAME_COUNT) / TRANSITION_DURATION_MS;
    if (frame >= BRUXA_FRAME_COUNT) frame = BRUXA_FRAME_COUNT - 1;

    _u8g2.setDrawColor(1);
    _u8g2.drawBitmap(0, 0, 16, 64, epd_bitmap_bruxa_allArray[frame]);
}

// ============================================================================
// TELAS E ANIMAÇÕES DE INICIALIZAÇÃO, WI-FI E VINCULAÇÃO (BOOT SEQUENCE)
// ============================================================================

void DisplayEngine::showBootSplash(uint32_t durationMs) {
    uint16_t frameDelay = durationMs / BOOT_FRAME_COUNT;
    if (frameDelay < 40) frameDelay = 75;

    for (uint8_t f = 0; f < BOOT_FRAME_COUNT; f++) {
        _u8g2.clearBuffer();
        _u8g2.setDrawColor(1);
        if (f < 32) {
            // Frames 0..31: 124x70px (centralizado em x=2, y=0)
            _u8g2.drawBitmap(2, 0, 16, 64, epd_bitmap_boot_allArray[f]);
        } else {
            // Frames 32..41: 128x64px (x=0, y=0)
            _u8g2.drawBitmap(0, 0, 16, 64, epd_bitmap_boot_allArray[f]);
        }
        _u8g2.sendBuffer();
        delay(frameDelay);
    }
}

void DisplayEngine::drawWifiWaves(int16_t cx, int16_t cy, uint8_t stage) {
    // Ponto central da antena
    _u8g2.drawDisc(cx, cy, 2);

    // Arcos concêntricos superiores de sinal
    if (stage >= 1) {
        _u8g2.drawCircle(cx, cy, 6, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    }
    if (stage >= 2) {
        _u8g2.drawCircle(cx, cy, 11, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    }
    if (stage >= 3) {
        _u8g2.drawCircle(cx, cy, 16, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    }
}

void DisplayEngine::renderConnectingWifiFrame(uint32_t now, const char* ssid) {
    _u8g2.clearBuffer();

    // 1. Cabeçalho Invertido Nítido
    _u8g2.setColorIndex(1);
    _u8g2.drawRBox(0, 0, 128, 13, 2);
    _u8g2.setColorIndex(0);
    _u8g2.setFont(u8g2_font_helvB08_tf);
    const char* header = "CONECTANDO WI-FI";
    int16_t hw = _u8g2.getStrWidth(header);
    _u8g2.drawStr((128 - hw) / 2, 10, header);
    _u8g2.setColorIndex(1);

    // 2. Ondas do Wi-Fi animadas
    uint8_t stage = (uint8_t)((now / 250u) % 4u);
    drawWifiWaves(64, 30, stage);

    // 3. Nome da rede em fonte Helvetica Bold nítida
    _u8g2.setFont(u8g2_font_helvB08_tf);
    char redeStr[32];
    if (ssid && strlen(ssid) > 0) {
        snprintf(redeStr, sizeof(redeStr), "%s", ssid);
    } else {
        snprintf(redeStr, sizeof(redeStr), "Buscando rede...");
    }
    int16_t rw = _u8g2.getStrWidth(redeStr);
    _u8g2.drawStr((128 - rw) / 2, 47, redeStr);

    // 4. Pontos pulsantes animados
    _u8g2.setFont(u8g2_font_6x10_tf);
    uint8_t dots = (uint8_t)((now / 300u) % 4u);
    const char* dotStr = dots == 1 ? ".  " : (dots == 2 ? ". . " : (dots == 3 ? ". . ." : "   "));
    int16_t dw = _u8g2.getStrWidth(dotStr);
    _u8g2.drawStr((128 - dw) / 2, 59, dotStr);

    _u8g2.sendBuffer();
}

void DisplayEngine::showWifiConnectedAnimation(const char* ipStr, uint32_t durationMs) {
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        _u8g2.clearBuffer();

        // 1. Ícone de Sucesso com Checkmark
        _u8g2.drawDisc(64, 16, 11);
        _u8g2.setColorIndex(0);
        // Checkmark nítido encorpado
        _u8g2.drawLine(58, 16, 62, 20);
        _u8g2.drawLine(58, 17, 62, 21);
        _u8g2.drawLine(62, 20, 70, 12);
        _u8g2.drawLine(62, 21, 70, 13);
        _u8g2.setColorIndex(1);

        // 2. Texto "WI-FI CONECTADO!"
        _u8g2.setFont(u8g2_font_helvB08_tf);
        const char* msg = "WI-FI CONECTADO!";
        int16_t mw = _u8g2.getStrWidth(msg);
        _u8g2.drawStr((128 - mw) / 2, 40, msg);

        // 3. IP em fonte nítida
        char ipMsg[32];
        if (ipStr && strlen(ipStr) > 0) {
            snprintf(ipMsg, sizeof(ipMsg), "IP: %s", ipStr);
        } else {
            snprintf(ipMsg, sizeof(ipMsg), "Sinal Excelente");
        }
        int16_t iw = _u8g2.getStrWidth(ipMsg);
        _u8g2.drawStr((128 - iw) / 2, 56, ipMsg);

        _u8g2.sendBuffer();
        delay(30);
    }
}

void DisplayEngine::showWifiFailedScreen(uint32_t durationMs) {
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        _u8g2.clearBuffer();

        _u8g2.setFont(u8g2_font_6x10_tf);
        const char* msg = "SEM CONEXAO WI-FI";
        int16_t mw = _u8g2.getStrWidth(msg);
        _u8g2.drawStr((128 - mw) / 2, 22, msg);

        _u8g2.setFont(u8g2_font_5x8_tf);
        const char* sub1 = "Iniciando Modo Offline";
        int16_t s1w = _u8g2.getStrWidth(sub1);
        _u8g2.drawStr((128 - s1w) / 2, 38, sub1);

        const char* sub2 = "Rotinas salvas na memoria";
        int16_t s2w = _u8g2.getStrWidth(sub2);
        _u8g2.drawStr((128 - s2w) / 2, 52, sub2);

        _u8g2.sendBuffer();
        delay(30);
    }
}

void DisplayEngine::renderAuthStatusFrame(uint32_t now, const char* statusMsg) {
    _u8g2.clearBuffer();
    _u8g2.setFont(u8g2_font_6x10_tf);
    const char* h = "NUVEM FIREBASE";
    int16_t hw = _u8g2.getStrWidth(h);
    _u8g2.drawStr((128 - hw) / 2, 14, h);
    _u8g2.drawHLine(10, 18, 108);

    _u8g2.setFont(u8g2_font_6x10_tf);
    int16_t sw = _u8g2.getStrWidth(statusMsg);
    _u8g2.drawStr((128 - sw) / 2, 36, statusMsg);

    // Spinner pontilhado orbital
    uint8_t step = (uint8_t)((now / 120u) % 8u);
    int16_t cx = 64, cy = 52, r = 6;
    float angle = step * (3.14159f / 4.0f);
    int16_t px = cx + (int16_t)(r * cosf(angle));
    int16_t py = cy + (int16_t)(r * sinf(angle));
    _u8g2.drawDisc(px, py, 2);

    _u8g2.sendBuffer();
}

void DisplayEngine::showAlreadyPaired(uint32_t durationMs) {
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        _u8g2.clearBuffer();

        // Olhos sorridentes suaves
        drawSleepingEye(44, 20, 22);
        drawSleepingEye(84, 20, 22);

        _u8g2.setFont(u8g2_font_helvB08_tf);
        const char* msg = "ROBO VINCULADO!";
        int16_t mw = _u8g2.getStrWidth(msg);
        _u8g2.drawStr((128 - mw) / 2, 42, msg);

        const char* sub = "Iniciando modo companheiro";
        int16_t sw = _u8g2.getStrWidth(sub);
        _u8g2.drawStr((128 - sw) / 2, 56, sub);

        _u8g2.sendBuffer();
        delay(30);
    }
}

void DisplayEngine::renderPairing(uint32_t now) {
    // 1. Faixa Superior Invertida
    _u8g2.setColorIndex(1);
    _u8g2.drawRBox(0, 0, 128, 13, 2);
    _u8g2.setColorIndex(0);
    _u8g2.setFont(u8g2_font_helvB08_tf);
    const char* h = "VINCULAR NO SITE";
    int16_t hw = _u8g2.getStrWidth(h);
    _u8g2.drawStr((128 - hw) / 2, 10, h);
    _u8g2.setColorIndex(1);

    // 2. Caixa do Código de 6 Dígitos
    _u8g2.drawRFrame(8, 16, 112, 26, 4);

    // Formata o código: "123  456" com espaçamento central nítido
    char formattedCode[16] = "---  ---";
    if (strlen(_pairingCode) >= 6) {
        snprintf(formattedCode, sizeof(formattedCode), "%c%c%c  %c%c%c",
                 _pairingCode[0], _pairingCode[1], _pairingCode[2],
                 _pairingCode[3], _pairingCode[4], _pairingCode[5]);
    } else if (strlen(_pairingCode) > 0) {
        strncpy(formattedCode, _pairingCode, sizeof(formattedCode) - 1);
    }

    _u8g2.setFont(u8g2_font_helvB14_tf);
    int16_t cw = _u8g2.getStrWidth(formattedCode);
    _u8g2.drawStr((128 - cw) / 2, 35, formattedCode);

    // 3. Rodapé com instruções alternantes de alta visibilidade
    _u8g2.setFont(u8g2_font_helvB08_tf);
    bool blink = ((now / 700u) % 2u) == 0;
    const char* inst = blink ? "Acesse: utome.app" : "Digite o codigo";
    int16_t iw = _u8g2.getStrWidth(inst);
    _u8g2.drawStr((128 - iw) / 2, 57, inst);
}

void DisplayEngine::renderWifiApConfig(uint32_t now) {
    // 1. Faixa Superior Invertida com cantos arredondados
    _u8g2.setColorIndex(1);
    _u8g2.drawRBox(0, 0, 128, 13, 2);
    _u8g2.setColorIndex(0);
    _u8g2.setFont(u8g2_font_helvB08_tf);
    const char* h = "CONFIGURAR WI-FI";
    int16_t hw = _u8g2.getStrWidth(h);
    _u8g2.drawStr((128 - hw) / 2, 10, h);
    _u8g2.setColorIndex(1);

    // 2. Caixa Central com Nome da Rede Wi-Fi em Destaque
    _u8g2.drawRFrame(6, 16, 116, 26, 4);

    // Pequeno ícone de sinal Wi-Fi animado à esquerda dentro da caixa
    uint8_t stage = (uint8_t)((now / 300u) % 4u);
    _u8g2.drawDisc(20, 32, 1);
    if (stage >= 1) _u8g2.drawCircle(20, 32, 4, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    if (stage >= 2) _u8g2.drawCircle(20, 32, 7, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);
    if (stage >= 3) _u8g2.drawCircle(20, 32, 10, U8G2_DRAW_UPPER_RIGHT | U8G2_DRAW_UPPER_LEFT);

    // Nome da Rede AP (ex: UTOME WIFI)
    const char* ap = (_apName[0] != '\0') ? _apName : "UTOME WIFI";
    _u8g2.setFont(u8g2_font_helvB08_tf);
    _u8g2.drawStr(36, 32, ap);

    // 3. Rodapé com Instruções Claras Alternantes (2 segundos cada)
    _u8g2.setFont(u8g2_font_helvB08_tf);
    bool phase = ((now / 2000u) % 2u) == 0;
    const char* stepMsg = phase ? "1. Conecte no celular" : "2. Acesse 192.168.4.1";
    int16_t sw = _u8g2.getStrWidth(stepMsg);
    _u8g2.drawStr((128 - sw) / 2, 58, stepMsg);
}

void DisplayEngine::renderWifiApConfigFrame(uint32_t now, const char* apName, const char* ipStr) {
    if (apName) setApName(apName);
    _u8g2.clearBuffer();
    renderWifiApConfig(now);
    _u8g2.sendBuffer();
}

void DisplayEngine::showPairingSuccessAnimation(uint32_t durationMs) {
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        uint32_t elapsed = millis() - start;
        _u8g2.clearBuffer();

        // Corações pulsantes nos dois lados
        float pulse = 1.0f + 0.18f * sinf(elapsed * 0.012f);
        drawHeart(44, 22, pulse);
        drawHeart(84, 22, pulse);

        _u8g2.setFont(u8g2_font_6x10_tf);
        const char* msg = "VINCULADO COM SUCESSO!";
        int16_t mw = _u8g2.getStrWidth(msg);
        _u8g2.drawStr((128 - mw) / 2, 44, msg);

        _u8g2.setFont(u8g2_font_5x8_tf);
        const char* sub = "Bem-vindo ao Utome! <3";
        int16_t sw = _u8g2.getStrWidth(sub);
        _u8g2.drawStr((128 - sw) / 2, 58, sub);

        _u8g2.sendBuffer();
        delay(25);
    }
}

void DisplayEngine::showPairingSplash(const char* code, uint32_t durationMs) {
    setPairingCode(code);
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        uint32_t now = millis();
        _u8g2.clearBuffer();
        renderPairing(now);
        _u8g2.sendBuffer();
        delay(30);
    }
}

void DisplayEngine::drawBell(int16_t cx, int16_t cy, float angle) {
    int16_t ox = (int16_t)(sinf(angle) * 3.5f);

    // Argola superior
    _u8g2.drawCircle(cx, cy - 8, 2);

    // Corpo do sino
    _u8g2.drawLine(cx - 2, cy - 6, cx - 6 + ox, cy + 3);
    _u8g2.drawLine(cx + 2, cy - 6, cx + 6 + ox, cy + 3);
    _u8g2.drawLine(cx - 6 + ox, cy + 3, cx + 6 + ox, cy + 3);
    _u8g2.drawLine(cx - 8 + ox, cy + 5, cx + 8 + ox, cy + 5);

    // Badalo pendurado oscilando
    _u8g2.drawDisc(cx - ox, cy + 7, 2);

    // Ondas sonoras vibrantes nas laterais
    bool waveOn = ((millis() / 140u) % 2u) == 0;
    if (waveOn) {
        // Onda esquerda
        _u8g2.drawLine(cx - 11, cy - 3, cx - 13, cy);
        _u8g2.drawLine(cx - 13, cy, cx - 11, cy + 3);
        // Onda direita
        _u8g2.drawLine(cx + 11, cy - 3, cx + 13, cy);
        _u8g2.drawLine(cx + 13, cy, cx + 11, cy + 3);
    }
}

void DisplayEngine::renderAlarm(uint32_t now) {
    // 1. Faixa Superior Invertida e Piscante
    bool blinkHeader = ((now / 260u) % 2u) == 0;
    if (blinkHeader) {
        _u8g2.setColorIndex(1);
        _u8g2.drawBox(0, 0, 128, 14);
        _u8g2.setColorIndex(0);
        _u8g2.setFont(u8g2_font_6x10_tf);
        const char* h = "* HORA DA ROTINA! *";
        int16_t hw = _u8g2.getStrWidth(h);
        _u8g2.drawStr((128 - hw) / 2, 11, h);
        _u8g2.setColorIndex(1);
    } else {
        _u8g2.setColorIndex(1);
        _u8g2.drawFrame(0, 0, 128, 14);
        _u8g2.setFont(u8g2_font_6x10_tf);
        const char* h = "! HORA DA ROTINA !";
        int16_t hw = _u8g2.getStrWidth(h);
        _u8g2.drawStr((128 - hw) / 2, 11, h);
    }

    // 2. Sino animado e vibrante à esquerda
    float bellAngle = sinf((float)(now - _alarmAnimStart) * 0.016f);
    drawBell(18, 32, bellAngle);

    // 3. Horário agendado em destaque com moldura
    const char* timeStr = (_alarmTask.time[0] != '\0') ? _alarmTask.time : "--:--";
    _u8g2.setFont(u8g2_font_5x8_tf);
    _u8g2.drawRFrame(36, 17, 36, 11, 2);
    _u8g2.drawStr(39, 25, timeStr);

    // Prioridade (P1 Urgente / P2 Normal)
    if (_alarmTask.priority == PRIORITY_P1) {
        _u8g2.setColorIndex(1);
        _u8g2.drawRBox(76, 17, 50, 11, 2);
        _u8g2.setColorIndex(0);
        _u8g2.drawStr(80, 25, "URGENTE");
        _u8g2.setColorIndex(1);
    } else {
        _u8g2.drawRFrame(76, 17, 50, 11, 2);
        _u8g2.drawStr(84, 25, "Rotina");
    }

    // 4. Título da Tarefa (com quebra inteligente de linha + UTF-8 para acentos)
    _u8g2.setFont(u8g2_font_6x10_tf);
    const char* title = (_alarmTask.title[0] != '\0') ? _alarmTask.title : "Rotina Ativa";
    if (strlen(title) <= 14) {
        _u8g2.drawUTF8(36, 43, title);
    } else {
        char l1[16];
        char l2[24];
        int splitIdx = -1;
        for (int i = 13; i > 0; i--) {
            if (title[i] == ' ') {
                splitIdx = i;
                break;
            }
        }
        if (splitIdx > 0) {
            strncpy(l1, title, splitIdx);
            l1[splitIdx] = '\0';
            strncpy(l2, title + splitIdx + 1, sizeof(l2) - 1);
            l2[sizeof(l2) - 1] = '\0';
        } else {
            strncpy(l1, title, 13);
            l1[13] = '\0';
            strncpy(l2, title + 13, sizeof(l2) - 1);
            l2[sizeof(l2) - 1] = '\0';
        }
        _u8g2.drawUTF8(36, 38, l1);
        _u8g2.drawUTF8(36, 49, l2);
    }

    // 5. Linha divisória e rodapé de instruções com margens perfeitas
    _u8g2.drawHLine(0, 53, 128);
    _u8g2.setFont(u8g2_font_4x6_tf);
    const char* footer = "1x: Ver  |  2x: Concluir";
    int16_t fw = _u8g2.getStrWidth(footer);
    _u8g2.drawStr((128 - fw) / 2, 62, footer);
}



