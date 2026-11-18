#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "types.h"
#include "input_manager.h"
#include "display_engine.h"
#include "network_manager.h"
#include "firebase_sync.h"
#include "task_scheduler.h"

// ============================================================================
// UTOME 1 - FIRMWARE PRINCIPAL (Versão Auditada)
// Robô Companheiro Assistivo e Organizador de Rotinas
//
// CORREÇÕES DESTA VERSÃO:
// [FIX-1/2/3] InputManager: ISR com flags de borda atômicas + supressão de bouncing
// [FIX-4]     Loop principal: ignora eventos durante transição de modo
// [FIX-5]     displayEngine.getMode() só retorna modo quando transição completa
// [FIX-6]     randomSeed com esp_random() para variação real dos intervalos de piscada
// [FIX-7]     resetSleepTimer() chamado apenas por interação real do usuário
// [FEATURE]   TaskScheduler: disparo automático de rotinas por horário NTP
// ============================================================================

InputManager inputManager;
DisplayEngine displayEngine;
UtomeNetwork networkManager;
FirebaseSync firebaseSync;
TaskScheduler taskScheduler;

// ============================================================================
// JANELA DE "PENSAR" PARA CÁLCULO DE CLIQUES
// 2000 ms: tempo máximo para acumular toques antes de decidir 1x ou 2x.
// ============================================================================
uint32_t actionTime = DEFAULT_TOUCH_ACTION_TIME;
uint32_t pairingModeEnterTime = 0;

// --- Banco de Tarefas (sincronizadas da Nuvem / Flash) ---
TaskItem tasks[MAX_TASKS] = {};
uint8_t totalTasks = 0;
uint8_t currentTaskIndex = 0;

// ============================================================================
// executeAction() - Executado SOMENTE após a janela de pensar confirmar o evento.
// [FIX-4] Ignora completamente qualquer evento durante transição de modo.
// ============================================================================
void executeAction(TouchEvent eventToExecute) {
    // [FIX-4] Proteção crítica: bloqueia qualquer ação durante transição
    // Sem isso, um toque acidental durante a animação de 380ms podia trocar
    // o modo de volta imediatamente após mudar.
    if (displayEngine.isTransitioning()) {
        Serial.println("[GUARD] Acao bloqueada: transicao em andamento.");
        return;
    }

    RobotMode currentMode = displayEngine.getMode();

    // Se estiver na tela do Alarme Ativo de Rotina:
    if (currentMode == MODE_ALARM) {
        int8_t alarmIdx = taskScheduler.getActiveAlarmIndex();
        if (eventToExecute == TOUCH_SINGLE_CLICK) {
            Serial.println("[ALARM] 1 toque -> Alarme reconhecido! Exibindo detalhes da rotina.");
            taskScheduler.dismissActiveAlarm();
            if (alarmIdx >= 0 && alarmIdx < totalTasks) {
                currentTaskIndex = (uint8_t)alarmIdx;
                displayEngine.setTaskDisplay(&tasks[currentTaskIndex], currentTaskIndex, totalTasks);
            }
            displayEngine.triggerTransition(MODE_TASK);
        } else if (eventToExecute == TOUCH_DOUBLE_CLICK) {
            Serial.println("[ALARM] 2 toques -> Rotina concluida com sucesso direto pelo alarme!");
            if (alarmIdx >= 0 && alarmIdx < totalTasks) {
                tasks[alarmIdx].completed = true;
                firebaseSync.saveTasksToFlash(tasks, totalTasks);
                firebaseSync.reportTaskCompleted(&tasks[alarmIdx]);
            }
            taskScheduler.dismissActiveAlarm();
            displayEngine.triggerHearts(); // Celebração visual de afeto com corações nos olhos
            displayEngine.triggerTransition(MODE_FACE);
        } else if (eventToExecute == TOUCH_LONG_PRESS) {
            Serial.println("[ALARM] Toque longo -> Silenciando alarme e voltando ao Modo Face.");
            taskScheduler.dismissActiveAlarm();
            displayEngine.triggerTransition(MODE_FACE);
        }
        return;
    }

    // Se estiver na tela do Portal de Configuração Wi-Fi:
    if (currentMode == MODE_WIFI_CONFIG) {
        Serial.println("[WIFI-AP] Toque detectado. Encerrando Ponto de Acesso e avancando para Modo Face...");
        networkManager.stopConfigPortal();
        displayEngine.triggerTransition(MODE_FACE);
        return;
    }

    // Se estiver na tela de exibição do código de pareamento:
    if (currentMode == MODE_PAIRING) {
        if (firebaseSync.isPaired()) {
            Serial.println("[PAIRING] Robo ja pareado. Avancando para Modo Face...");
            displayEngine.triggerTransition(MODE_FACE);
        } else if (eventToExecute == TOUCH_LONG_PRESS) {
            // [A13] Toque longo na tela de pareamento → entra no Modo Face temporariamente.
            // O robô continua aguardando pareamento em background (cloudTaskWorker ainda roda).
            // Qualquer novo toque longo retorna ao código de pareamento.
            Serial.println("[PAIRING] [A13] Toque longo: entrando no Modo Face temporario (pareamento continua em background).");
            displayEngine.triggerTransition(MODE_FACE);
        } else {
            String _dbgCode = firebaseSync.getPairingCode();
            Serial.printf("[PAIRING] Toque recebido, mas robo aguarda vinculacao no site (Codigo: %s).\n",
                          _dbgCode.c_str());
        }
        return;
    }

    // 1. Toque Longo (> 3.0s): Alterna entre Modos Face e Task
    if (eventToExecute == TOUCH_LONG_PRESS) {
        if (currentMode == MODE_FACE) {
            Serial.println("[MODO] Transicao -> TASK (Organizador)");
            displayEngine.setTaskDisplay(&tasks[currentTaskIndex], currentTaskIndex, totalTasks);
            displayEngine.triggerTransition(MODE_TASK);
        } else if (currentMode == MODE_TASK) {
            Serial.println("[MODO] Transicao -> FACE (Companheiro)");
            displayEngine.triggerTransition(MODE_FACE);
        }
        return;
    }

    // 2. Cliques curtos: comportamento específico por modo
    if (currentMode == MODE_FACE) {
        if (eventToExecute == TOUCH_SINGLE_CLICK) {
            Serial.println("[FACE] 1 toque -> Reacao de carinho <3");
            displayEngine.resetSleepTimer(); // [FIX-7] Chamado apenas por toque real
            displayEngine.triggerHearts();
        } else if (eventToExecute == TOUCH_DOUBLE_CLICK) {
            if (displayEngine.isSleeping()) {
                Serial.println("[FACE] 2 toques -> Acordando do sono!");
                displayEngine.resetSleepTimer();
            } else {
                Serial.println("[FACE] 2 toques -> Disparando animacao surpresa (Abelha / Pacman)!");
                displayEngine.triggerRandomAnimation();
            }
        }
    }
    else if (currentMode == MODE_TASK) {
        if (eventToExecute == TOUCH_SINGLE_CLICK) {
            if (totalTasks > 0) {
                currentTaskIndex = (currentTaskIndex + 1) % totalTasks;
                Serial.printf("[TASK] 1 toque -> Proxima tarefa %d/%d: %s\n",
                              currentTaskIndex + 1, totalTasks, tasks[currentTaskIndex].title);
                displayEngine.setTaskDisplay(&tasks[currentTaskIndex], currentTaskIndex, totalTasks);
            }
        }
        else if (eventToExecute == TOUCH_DOUBLE_CLICK) {
            if (totalTasks > 0 && !tasks[currentTaskIndex].completed) {
                Serial.printf("[TASK] 2 toques -> Tarefa CONCLUIDA: [%s]\n", tasks[currentTaskIndex].title);
                tasks[currentTaskIndex].completed = true;
                firebaseSync.saveTasksToFlash(tasks, totalTasks);
                displayEngine.triggerTaskCompletedEffect();
                firebaseSync.reportTaskCompleted(&tasks[currentTaskIndex]);
            }
        }
    }
}

// ============================================================================
// PROCESSAMENTO VIA MONITOR SERIAL (Comandos e Telemetria)
// Permite controlar e depurar o robô diretamente pelo terminal serial da IDE
// ============================================================================

void printSerialMenu() {
    Serial.println();
    Serial.println("==========================================================");
    Serial.println("    UTOME 1 - CONSOLE DE CONTROLE E PROCESSAMENTO SERIAL   ");
    Serial.println("==========================================================");
    Serial.println(" Comandos que você pode digitar e enviar no Monitor Serial:");
    Serial.println("  [1]        -> Disparar 1 Toque (Carinho no Face / Prox. Tarefa)");
    Serial.println("  [2]        -> Disparar 2 Toques (Acordar / Concluir Tarefa)");
    Serial.println("  [3] ou [L] -> Disparar Toque Longo (Alternar Face <-> Task)");
    Serial.println("  [F]        -> Forcar Modo Face");
    Serial.println("  [T]        -> Forcar Modo Task");
    Serial.println("  [I]        -> Diagnostico em tempo real (Pinos, Modo, Tarefa)");
    Serial.println("  [W]        -> Diagnostico e status da conexao Wi-Fi e NTP");
    Serial.println("  [S]        -> Sincronizar rotinas e redes Wi-Fi com o Firebase agora");
    Serial.println("  [N]        -> Sincronizar e listar redes Wi-Fi do dashboard agora");
    Serial.println("  [P]        -> Exibir codigo de vinculacao no visor OLED");
    Serial.println("  [U]        -> Desvincular robo da conta e gerar novo codigo");
    Serial.println("  [C]        -> Abrir Portal de Configuracao Wi-Fi (Ponto de Acesso)");
    Serial.println("  [X]        -> Limpar credenciais Wi-Fi salvas na Flash");
    Serial.println("  [A]        -> Testar disparo de Alarme para rotina pendente");
    Serial.println("  [R]        -> Reset de Fabrica (apaga UID salvo e gera nova identidade)");
    Serial.println("  [?]        -> Exibir este menu de comandos");
    Serial.println("==========================================================");
    Serial.println();
}

void printSystemStatus() {
    RobotMode currentMode = displayEngine.getMode();
    const char* modeStr = "FACE (Companheiro)";
    if (currentMode == MODE_TASK) modeStr = "TASK (Organizador)";
    else if (currentMode == MODE_PAIRING) modeStr = "PAIRING (Aguardando Vinculo no Site)";
    else if (currentMode == MODE_WIFI_CONFIG) modeStr = "WIFI_CONFIG (Portal Aberto no Celular)";
    else if (currentMode == MODE_ALARM) modeStr = "ALARM (Alerta Ativo de Rotina Agendada)";

    Serial.println();
    Serial.println("------------- DIAGNOSTICO EM TEMPO REAL -------------");
    Serial.printf(" Modo Atual:        %s\n", modeStr);
    Serial.printf(" Transicao:         %s\n", displayEngine.isTransitioning() ? "EM ANDAMENTO" : "ESTAVEL");
    Serial.printf(" Pino Touch (GPIO %d): Leitura Bruta = %s | Debounced = %s\n",
                  PIN_TOUCH,
                  inputManager.readRaw() ? "HIGH (3.3V - Ativado)" : "LOW (0V - Repouso)",
                  inputManager.isPressed() ? "PRESSIONADO" : "LIVRE");
    Serial.printf(" Janela Decisao:    %s\n", inputManager.isThinking() ? "AGUARDANDO 2o CLIQUE" : "PRONTA");
    Serial.printf(" Conexao Wi-Fi:     %s (IP: %s | Sinal: %d dBm)\n",
                  networkManager.isConnected() ? "CONECTADO" : "DESCONECTADO",
                  networkManager.getLocalIP().c_str(), networkManager.getRssi());
    Serial.printf(" Redes Dashboard:   %u cadastrada(s) para roaming\n",
                  networkManager.getCloudNetworkCount());
    Serial.printf(" Horario NTP:       %s (Data: %s)\n",
                  networkManager.getFormattedTime().c_str(), networkManager.getFormattedDate().c_str());
    Serial.printf(" Firebase Sync:     %s (Pareado: %s | UID: %s)\n",
                  firebaseSync.isAuthenticated() ? "ONLINE" : "PENDENTE",
                  firebaseSync.isPaired() ? "SIM" : "NAO",
                  firebaseSync.getDeviceUid().length() > 0 ? firebaseSync.getDeviceUid().c_str() : "Nao gerado");
    if (totalTasks > 0) {
        Serial.printf(" Tarefa Atual:      [%d/%d] %s (%s)\n",
                       currentTaskIndex + 1, totalTasks,
                       tasks[currentTaskIndex].title,
                       tasks[currentTaskIndex].completed ? "CONCLUIDA" : "PENDENTE");
    }
    Serial.println("-----------------------------------------------------");
    Serial.println();
}

void processSerialCommands() {
    while (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == '\r' || cmd == '\n' || cmd == ' ') continue;

        switch (cmd) {
            case '1':
                Serial.println("\n[SERIAL-CMD] Recebido '1' -> Disparando TOQUE SIMPLES");
                executeAction(TOUCH_SINGLE_CLICK);
                break;

            case '2':
                Serial.println("\n[SERIAL-CMD] Recebido '2' -> Disparando DUPLO TOQUE");
                executeAction(TOUCH_DOUBLE_CLICK);
                break;

            case '3':
            case 'l':
            case 'L':
                Serial.println("\n[SERIAL-CMD] Recebido '3/L' -> Disparando TOQUE LONGO");
                executeAction(TOUCH_LONG_PRESS);
                break;

            case 'f':
            case 'F':
                Serial.println("\n[SERIAL-CMD] Forcando alternancia para Modo FACE");
                displayEngine.triggerTransition(MODE_FACE);
                break;

            case 't':
            case 'T':
                Serial.println("\n[SERIAL-CMD] Forcando alternancia para Modo TASK");
                displayEngine.setTaskDisplay(&tasks[currentTaskIndex], currentTaskIndex, totalTasks);
                displayEngine.triggerTransition(MODE_TASK);
                break;

            case 'i':
            case 'I':
                printSystemStatus();
                break;

            case 'w':
            case 'W':
                networkManager.printNetworkStatus();
                break;

            case 's':
            case 'S':
                Serial.println("\n[SERIAL-CMD] Forcando sincronizacao imediata com o Firebase...");
                firebaseSync.triggerManualSync();
                break;

            case 'n':
            case 'N':
                Serial.println("\n[SERIAL-CMD] Sincronizando redes Wi-Fi salvas no dashboard...");
                firebaseSync.fetchWifiNetworks();
                networkManager.printNetworkStatus();
                break;

            case 'p':
            case 'P':
                firebaseSync.printSyncStatus();
                {
                    String codeStr = firebaseSync.getPairingCode();
                    displayEngine.showPairingSplash(codeStr.c_str(), 4000);
                    if (!firebaseSync.isPaired()) {
                        displayEngine.setPairingCode(codeStr.c_str());
                        displayEngine.setMode(MODE_PAIRING);
                        pairingModeEnterTime = millis();
                    }
                }
                break;

            case 'u':
            case 'U':
                Serial.println("\n[SERIAL-CMD] Desvinculando robo da conta e gerando novo codigo...");
                firebaseSync.clearPairing();
                {
                    String codeStr = firebaseSync.getPairingCode();
                    displayEngine.showPairingSplash(codeStr.c_str(), 4000);
                    displayEngine.setPairingCode(codeStr.c_str());
                    displayEngine.setMode(MODE_PAIRING);
                    pairingModeEnterTime = millis();
                }
                break;

            case 'c':
            case 'C':
                Serial.println("\n[SERIAL-CMD] Abrindo Portal de Configuracao Wi-Fi (Ponto de Acesso)...");
                networkManager.startConfigPortal();
                displayEngine.setApName(networkManager.getApSSID().c_str());
                displayEngine.setMode(MODE_WIFI_CONFIG);
                break;

            case 'x':
            case 'X':
                Serial.println("\n[SERIAL-CMD] Apagando credenciais de Wi-Fi da memoria Flash...");
                networkManager.clearSavedCredentials();
                break;

            case 'a':
            case 'A':
                Serial.println("\n[SERIAL-CMD] Disparando teste imediato de Alarme para rotina...");
                {
                    int8_t testIdx = taskScheduler.triggerTestAlarm(totalTasks, millis());
                    if (testIdx >= 0) {
                        displayEngine.resetSleepTimer();
                        displayEngine.setAlarmTask(&tasks[testIdx]);
                        displayEngine.triggerTransition(MODE_ALARM);
                    }
                }
                break;

            case 'r':
            case 'R':
                Serial.println("\n[SERIAL-CMD] Reset de Fabrica solicitado! Limpando NVS e reiniciando...");
                firebaseSync.resetDeviceIdentity();
                networkManager.clearSavedCredentials();
                delay(1500);
                ESP.restart();
                break;

            case '?':
            case 'h':
            case 'H':
                printSerialMenu();
                break;

            default:
                Serial.printf("\n[SERIAL-CMD] Comando '%c' nao reconhecido. Digite '?' para ver os comandos disponiveis.\n", cmd);
                break;
        }
    }
}

void setup() {
    // Inicia Serial USB-CDC do ESP32-C3 sem bloquear quando não há PC conectado.
    Serial.begin(115200);

    Serial.println();
    Serial.println("==========================================");
    Serial.println("    UTOME 1 - Firmware v2.0 Auditado     ");
    Serial.println("==========================================");

    inputManager.init();
    displayEngine.init();
    firebaseSync.init();

    // [C5] Restaura rotinas salvas no cache NVS flash imediatamente no boot
    totalTasks = firebaseSync.loadTasksFromFlash(tasks, MAX_TASKS);
    if (totalTasks > 0) {
        Serial.printf("[BOOT] %u rotinas offline carregadas da memoria flash.\n", totalTasks);
        displayEngine.setTaskDisplay(&tasks[currentTaskIndex], currentTaskIndex, totalTasks);
    }

    // 1. TELA DE BOOT (42 frames de animação fluida a 75ms/frame)
    Serial.println("[BOOT] Exibindo tela de boot inicial no display OLED...");
    displayEngine.showBootSplash(3150);

    // [PRIORIDADE 2.3] RESET DE FÁBRICA / WI-FI VIA 3 BOOTS RÁPIDOS (<5s cada)
    // Permite que qualquer usuário final resete a rede sem precisar de computador ou Monitor Serial
    Preferences bootPrefs;
    bootPrefs.begin("utome_boot", false);
    uint8_t bootCount = bootPrefs.getUChar("count", 0);
    bootCount++;
    bootPrefs.putUChar("count", bootCount);
    bootPrefs.end();

    Serial.printf("[BOOT] Ciclo de reinicializacao detectado: %d/3\n", bootCount);

    bool forceConfig = false;

    if (bootCount >= 3) {
        Serial.println();
        Serial.println("******************************************************************");
        Serial.println(" [RESET] 3 BOOTS RAPIDOS DETECTADOS (<5s)! INICIANDO RESET GERAL");
        Serial.println(" Limpando Wi-Fi e Pareamento para novo cadastro...");
        Serial.println("******************************************************************");
        Serial.println();

        // Zera o contador para não repetir o reset no próximo boot
        bootPrefs.begin("utome_boot", false);
        bootPrefs.putUChar("count", 0);
        bootPrefs.end();

        // Apaga credenciais Wi-Fi e pareamento salvos
        networkManager.clearSavedCredentials();
        firebaseSync.clearPairing();

        // Feedback visual no display OLED
        displayEngine.showFactoryResetScreen(2500);
        forceConfig = true;
    }

    // 1.5 VERIFICA SE O SENSOR DE TOQUE ESTÁ PRESSIONADO NO BOOT (MÉTODO 2: TOQUE DE 3s)
    if (!forceConfig && inputManager.readRaw()) {
        Serial.println("[BOOT] Sensor de toque ativado no boot! Aguardando 3s para Reset Total...");
        uint32_t pressStart = millis();
        while (inputManager.readRaw() && (millis() - pressStart < 3000)) {
            displayEngine.renderAuthStatusFrame(millis(), "SEGURE 3s P/ RESET");
            delay(30);
        }
        if (millis() - pressStart >= 3000) {
            Serial.println("[BOOT] Toque mantido por 3s! Resetando Wi-Fi e Pareamento...");
            networkManager.clearSavedCredentials();
            firebaseSync.clearPairing();
            displayEngine.showFactoryResetScreen(2500);
            forceConfig = true;
        }
    }

    // 2. CONECTANDO À INTERNET OU ABRINDO PORTAL WI-FI
    Serial.println("[NETWORK] Inicializando subsistema de rede...");
    networkManager.init();

    if (!forceConfig && networkManager.hasSavedCredentials()) {
        Serial.printf("[NETWORK] Conectando a rede: %s...\n", networkManager.getSavedSSID().c_str());
        uint32_t startWifiAttempt = millis();
        while (!networkManager.isConnected() && (millis() - startWifiAttempt < WIFI_CONNECT_TIMEOUT_MS)) {
            networkManager.update(millis());
            displayEngine.renderConnectingWifiFrame(millis(), networkManager.getSavedSSID().c_str());
            delay(30);
        }
    }

    // 2.1 Se NÃO conectou e NÃO há credenciais salvas (ou foi forçado pelo toque de 3s no boot)
    if (!networkManager.isConnected()) {
        if (forceConfig || !networkManager.hasSavedCredentials()) {
            Serial.println("[WIFI] Sem credenciais salvas ou solicitado no boot. Abrindo Portal de Configuracao...");
            networkManager.startConfigPortal();
            displayEngine.setApName(networkManager.getApSSID().c_str());
            displayEngine.setMode(MODE_WIFI_CONFIG);

            uint32_t portalWaitStart = millis();
            while (networkManager.isApMode() && (millis() - portalWaitStart < WIFI_AP_PORTAL_TIMEOUT_MS)) {
                uint32_t now = millis();
                networkManager.update(now);
                displayEngine.renderWifiApConfigFrame(now, networkManager.getApSSID().c_str(), networkManager.getApIP().c_str());

                // Permite sair do portal tocando no sensor de toque
                if (inputManager.update(actionTime) == TOUCH_SINGLE_CLICK) {
                    Serial.println("[WIFI-AP] Toque no sensor detectado. Saindo do portal de configuracao...");
                    break;
                }
                delay(25);
            }

            // Encerra AP se ainda estiver ativo
            if (networkManager.isApMode()) {
                networkManager.stopConfigPortal();
            }

            // Tenta conectar se o usuário acabou de preencher as credenciais no celular
            if (networkManager.hasSavedCredentials() && !networkManager.isConnected()) {
                uint32_t retryStart = millis();
                while (!networkManager.isConnected() && (millis() - retryStart < WIFI_CONNECT_TIMEOUT_MS)) {
                    networkManager.update(millis());
                    displayEngine.renderConnectingWifiFrame(millis(), networkManager.getSavedSSID().c_str());
                    delay(30);
                }
            }
        } else {
            Serial.println("[WIFI] Roteador nao respondeu de imediato. Robo iniciando em Modo Offline (tentativas continuam em background)...");
        }
    }

    // 3. SE CONECTADO -> ANIMAÇÃO DE SUCESSO NO DISPLAY E FIREBASE
    if (networkManager.isConnected()) {
        Serial.printf("[WIFI] Conectado! IP: %s\n", networkManager.getLocalIP().c_str());
        displayEngine.showWifiConnectedAnimation(networkManager.getLocalIP().c_str(), 1600);

        // 4. AUTENTICAÇÃO E CÓDIGO DE VINCULAÇÃO COM O SITE
        displayEngine.renderAuthStatusFrame(millis(), "Conectando nuvem...");

        // Injeta referência ao NetworkManager para que o FirebaseSync possa
        // entregar redes Wi-Fi baixadas do Firestore diretamente ao gerenciador de rede
        firebaseSync.setNetworkManager(&networkManager);

        firebaseSync.ensureReady(tasks, MAX_TASKS, totalTasks);

        if (firebaseSync.isPaired()) {
            Serial.printf("[FIREBASE] Dispositivo ja vinculado ao responsavel: %s\n", firebaseSync.getOwnerUid().c_str());
            displayEngine.showAlreadyPaired(1600);
            displayEngine.setMode(MODE_FACE);
        } else {
            String pairingCodeStr = firebaseSync.getPairingCode();
            const char* pCode = pairingCodeStr.c_str();

            Serial.println();
            Serial.println("==========================================================");
            Serial.printf("  [DISPLAY] EXIBINDO CODIGO DE VINCULACAO NO OLED: %s\n", pCode);
            Serial.println("  (O display permanecera fixo no codigo ate a vinculacao no site)");
            Serial.println("==========================================================");
            Serial.println();

            displayEngine.setPairingCode(pCode);
            displayEngine.setMode(MODE_PAIRING);
            pairingModeEnterTime = millis();
        }
    } else {
        Serial.println("[WIFI] Conexao nao estabelecida. Operando em Modo Offline com cache local...");
        displayEngine.showWifiFailedScreen(1800);
        // Injeta NetworkManager mesmo no modo offline para que o FirebaseSync possa
        // acionar o roaming cloud assim que a conexao for restabelecida no loop()
        firebaseSync.setNetworkManager(&networkManager);
        // Carrega redes cloud salvas na NVS para tentar reconexao automatica em background
        networkManager.loadCloudNetworksFromFlash();
        // Inicia worker FreeRTOS em background para autenticar e sincronizar assim que a rede conectar
        firebaseSync.startBackgroundTask(tasks, MAX_TASKS, totalTasks);
        displayEngine.setMode(MODE_FACE);
    }

    displayEngine.setTaskDisplay(&tasks[currentTaskIndex], currentTaskIndex, totalTasks);
    taskScheduler.init(tasks, MAX_TASKS);
    displayEngine.update(millis());

    Serial.println("[OK] Sistema pronto!");
    Serial.printf("[CONFIG] Janela de clique: %u ms | Toque Longo: %u ms\n", actionTime, TOUCH_LONG_PRESS_MS);

    // Apresenta o menu de processamento pelo monitor serial
    printSerialMenu();
}

void loop() {
    uint32_t now = millis();

    // 1. Processa subsistema de rede (Wi-Fi e sincronização de horário NTP)
    networkManager.update(now);

    // 1.1 Se estava no Portal Wi-Fi e o portal foi encerrado (salvo com sucesso ou timeout)
    if (displayEngine.getMode() == MODE_WIFI_CONFIG && !networkManager.isApMode()) {
        displayEngine.triggerTransition(MODE_FACE);
    }

    // 1.2 Agendador de Rotinas: verifica se atingiu o horário de alguma rotina pendente
    // Só dispara se o robô estiver no Modo Face ou Modo Task (evita interromper telas de onboarding/configuração)
    RobotMode activeRobotMode = displayEngine.getMode();
    if (activeRobotMode == MODE_FACE || activeRobotMode == MODE_TASK) {
        int8_t triggeredAlarmIdx = taskScheduler.update(now, networkManager, totalTasks);
        if (triggeredAlarmIdx >= 0) {
            Serial.printf("[SCHEDULER] Despertando o robo e disparando alarme para a tarefa %d: [%s]\n",
                          triggeredAlarmIdx + 1, tasks[triggeredAlarmIdx].title);
            displayEngine.resetSleepTimer();
            displayEngine.setAlarmTask(&tasks[triggeredAlarmIdx]);
            displayEngine.triggerTransition(MODE_ALARM);
        }
    }

    // 1.3 Timeout de segurança do Alarme (após 60s sem interação, retorna para o Modo Face)
    if (displayEngine.getMode() == MODE_ALARM && taskScheduler.isAlarmTimedOut(now)) {
        Serial.println("[SCHEDULER] Timeout do alarme atingido (60s sem toque). Retornando ao Modo Face.");
        taskScheduler.dismissActiveAlarm();
        displayEngine.triggerTransition(MODE_FACE);
    }

    // [PRIORIDADE 2.3] Zerar contador de boot rápido após 5 segundos de operação estável
    static bool bootCounterStabilized = false;
    if (!bootCounterStabilized && now > 5000) {
        bootCounterStabilized = true;
        Preferences bp;
        bp.begin("utome_boot", false);
        if (bp.getUChar("count", 0) > 0) {
            bp.putUChar("count", 0);
            Serial.println("[BOOT] Operacao estavel (>5s). Contador de boot rapido zerado.");
        }
        bp.end();
    }

    // [PRIORIDADE 2.2 e 2.4] Atualiza feedback visual de conectividade e alertas no display
    displayEngine.setNetworkStatus(networkManager.isConnected(), firebaseSync.isAuthenticated(), firebaseSync.isPaired());
    displayEngine.setRoutineLimitWarning(firebaseSync.isRoutineLimitExceeded(), firebaseSync.getTotalCloudRoutines());

    // 2. Sincroniza rotinas com o Firebase Firestore (polling a cada 60s ou checagem a cada 3s se não pareado)
    bool tasksChanged = false;
    firebaseSync.update(now, networkManager.isConnected(), tasks, MAX_TASKS, totalTasks, tasksChanged);
    if (tasksChanged) {
        currentTaskIndex = 0;
        displayEngine.setTaskDisplay(&tasks[currentTaskIndex], currentTaskIndex, totalTasks);
        taskScheduler.notifyTasksChanged(totalTasks);
        Serial.printf("[TASK] Lista de rotinas atualizada com sucesso (%d tarefas ativas)!\n", totalTasks);
    }

    // 2.1 Celebração visual imediata quando o robô for vinculado no site
    if (firebaseSync.checkJustPaired()) {
        Serial.println("[FIREBASE] Robo vinculado com sucesso pela plataforma web!");
        displayEngine.showPairingSuccessAnimation(2200);
        displayEngine.triggerTransition(MODE_FACE);
    }

    // [PRIORIDADE 2.1] Sincronização do código de pareamento no display quando não pareado
    if (!firebaseSync.isPaired()) {
        // [FIX-RACE] Captura o código em variável local antes de usar o ponteiro.
        // getPairingCode() retorna const String& (membro); guardar em String local
        // evita race condition se cloudTaskWorker atualizar _pairingCode entre
        // .c_str() e strlen() com FreeRTOS rodando em paralelo.
        String localPairingCode = firebaseSync.getPairingCode();
        if (localPairingCode.length() == 6) {
            displayEngine.setPairingCode(localPairingCode.c_str());
        }

        // Robô não pareado: mantém a tela com o código de 6 dígitos no OLED fixo.
        // [A13] Exceção: se o usuário entrou no Modo Face via toque longo, respeita a escolha.
        // Retorna ao código de pareamento após 60s de inatividade no Modo Face.
        bool inFaceByChoice = (displayEngine.getMode() == MODE_FACE);
        bool faceIdleExpired = inFaceByChoice && (now - pairingModeEnterTime >= 60000UL);
        if (!inFaceByChoice || faceIdleExpired) {
            if (displayEngine.getMode() != MODE_PAIRING &&
                displayEngine.getMode() != MODE_ALARM &&
                displayEngine.getMode() != MODE_WIFI_CONFIG) {
                displayEngine.setMode(MODE_PAIRING);
                pairingModeEnterTime = now;
            }
        }
    }

    // 3. Processa comandos recebidos pelo Monitor Serial (emulação e comandos remotos)
    processSerialCommands();

    // 4. Processa o sensor de toque físico com janela de cálculo de cliques
    TouchEvent event = inputManager.update(actionTime);

    // 5. Se um evento foi resolvido (via hardware ou timeout), executa a ação correspondente
    if (event != TOUCH_NONE) {
        executeAction(event);
    }

    // 6. Atualiza o motor gráfico a ~30 FPS (sem bloqueio)
    displayEngine.update(now);
}
