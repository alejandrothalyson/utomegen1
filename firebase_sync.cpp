#include "firebase_sync.h"
#include "firebase_ca_cert.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <lwip/dns.h>

#define FIREBASE_HTTP_TIMEOUT_MS 4000
// Timeout específico para verificação de pareamento — menor para resposta rápida ao usuário
#define FIREBASE_PAIR_CHECK_TIMEOUT_MS 3000

Preferences preferences;

FirebaseSync::FirebaseSync() :
    _authenticated(false),
    _isPaired(false),
    _justPaired(false),
    _codePublished(false),
    _forceSync(false),
    _routineLimitExceeded(false),
    _totalCloudRoutines(0),
    _lastPollTime(0),
    _lastPairCheckTime(0),
    _tokenExpireTime(0),
    _lastAuthRetryTime(0),
    _lastPairCodeRetryTime(0),
    _lastWifiNetworksPoll(0),
    _authFailCount(0),
    _deviceUid(""),
    _idToken(""),
    _refreshToken(""),
    _ownerUid(""),
    _pairingCode(""),
    _network(nullptr),
    _completionQueue(nullptr),
    _dataMutex(nullptr),
    _cloudTaskHandle(nullptr),
    _taskRunning(false),
    _sharedTasks(nullptr),
    _sharedMaxTasks(0),
    _sharedTotalLoaded(nullptr),
    _tasksChangedPending(false)
{
}

void FirebaseSync::init() {
    Serial.println("[FIREBASE] Inicializando cliente Firebase Firestore...");

    if (!_completionQueue) {
        _completionQueue = xQueueCreate(10, sizeof(TaskCompletionEvent));
    }
    if (!_dataMutex) {
        _dataMutex = xSemaphoreCreateMutex();
    }

    preferences.begin("utome_auth", false);
    _deviceUid     = preferences.getString("deviceUid", "");
    _refreshToken  = preferences.getString("refreshToken", "");
    _ownerUid      = preferences.getString("ownerUid", "");
    preferences.end();

    if (_deviceUid.length() > 0) {
        Serial.printf("[FIREBASE] Identidade recuperada da memoria NVS: %s\n", _deviceUid.c_str());
        if (_ownerUid.length() > 0) {
            _isPaired = true;
            Serial.printf("[FIREBASE] Dispositivo ja pareado ao responsavel: %s\n", _ownerUid.c_str());
        }
    } else {
        Serial.println("[FIREBASE] Nenhum dispositivo registrado localmente. Aguardando Wi-Fi para autenticacao inicial.");
    }
}

void FirebaseSync::configureClient(WiFiClientSecure& client) {
    // Configuração padrão: sempre usado em TODA requisição
    client.setTimeout(FIREBASE_HTTP_TIMEOUT_MS);
    client.setInsecure(); // baseline seguro e compatível com todas as APIs Google
}

// Configura cliente para Firestore com CA oficial quando o relógio NTP estiver válido (A1)
// Uso interno apenas para chamadas ao firestore.googleapis.com
static void configureFirestoreClient(WiFiClientSecure& client) {
    client.setTimeout(FIREBASE_HTTP_TIMEOUT_MS);
    time_t nowSec = time(nullptr);
    if (nowSec > 1000000000) {
        // NTP sincronizado: valida certificado CA oficial GTS Root R1 (A1)
        client.setCACert(FIREBASE_ROOT_CA);
    } else {
        client.setInsecure();
    }
}

bool FirebaseSync::ensureReady(TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded) {
    if (_deviceUid.length() > 0 && _refreshToken.length() > 0) {
        // Dispositivo existente recuperado da NVS. Jamais recria a conta!
        if (!refreshAuthToken()) {
            Serial.println("[FIREBASE-AUTH] Aviso: renovacao inicial falhou. Tentando reconectar...");
        }
    } else {
        // Primeiro boot de fabrica com NVS virgem: cria a identidade unica inicial
        authenticateAnonymous();
    }

    if (_authenticated) {
        checkDeviceRegistration();
    }

    if (_isPaired) {
        fetchRoutinesFromCloud(tasksList, maxTasks, totalLoaded);
        // Busca imediata das redes Wi-Fi salvas no dashboard ao iniciar com pareamento ja ativo
        fetchWifiNetworks();
        _lastWifiNetworksPoll = millis();
    } else if (_authenticated) {
        if (!_codePublished || _pairingCode.length() != 6) {
            generatePairingCode();
        }
    }

    _lastPollTime = millis();
    _lastPairCheckTime = millis(); // Inicializa para dar margem de 4s apos o boot

    // Inicia a tarefa em background para cuidar de todas as operações subsequentes (C1 Definitivo)
    startBackgroundTask(tasksList, maxTasks, totalLoaded);

    return (_isPaired || _codePublished);
}

void FirebaseSync::clearPairing() {
    _ownerUid = "";
    _isPaired = false;
    _codePublished = false;
    preferences.begin("utome_auth", false);
    preferences.remove("ownerUid");
    preferences.end();

    _pairingCode = "";
    generatePairingCode();
    Serial.println("[FIREBASE] Pareamento resetado! Novo codigo gerado: " + _pairingCode);
}

bool FirebaseSync::checkJustPaired() {
    if (_justPaired) {
        _justPaired = false;
        return true;
    }
    return false;
}

void FirebaseSync::startBackgroundTask(TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded) {
    _sharedTasks = tasksList;
    _sharedMaxTasks = maxTasks;
    _sharedTotalLoaded = &totalLoaded;

    if (_cloudTaskHandle == nullptr) {
        _taskRunning = true;
        BaseType_t ret = xTaskCreate(
            cloudTaskTrampoline,
            "cloud_sync",
            8192,
            this,
            1,
            &_cloudTaskHandle
        );
        if (ret == pdPASS) {
            Serial.println("[FIREBASE-RTOS] Tarefa em background 'cloud_sync' iniciada (Stack 8KB, Prio 1)!");
        } else {
            Serial.println("[FIREBASE-RTOS] ERRO: Falha ao inicializar tarefa 'cloud_sync'!");
        }
    }
}

void FirebaseSync::cloudTaskTrampoline(void* arg) {
    FirebaseSync* self = static_cast<FirebaseSync*>(arg);
    self->cloudTaskWorker();
}

void FirebaseSync::cloudTaskWorker() {
    while (_taskRunning) {
        // 1. Processa eventos pendentes da fila de conclusões com prioridade imediata
        processCompletionQueue();

        bool isNetReady = (_network != nullptr && _network->isConnected());

        if (isNetReady) {
            uint32_t now = millis();

            // 2. Se ainda não autenticado, tenta reconectar
            if (!_authenticated) {
                if (now - _lastAuthRetryTime >= 10000) {
                    _lastAuthRetryTime = now;
                    bool authOk = false;

                    if (_deviceUid.length() > 0 && _refreshToken.length() > 0) {
                        Serial.println("[FIREBASE-AUTH] [RTOS] Reconectando sessao do dispositivo...");
                        authOk = refreshAuthToken();
                        if (!authOk) {
                            _authFailCount++;
                            Serial.printf("[FIREBASE-AUTH] [RTOS] Falha %d/3 ao renovar token.\n", _authFailCount);
                        } else {
                            _authFailCount = 0;
                        }
                    } else {
                        Serial.println("[FIREBASE-AUTH] [RTOS] Dispositivo virgem. Gerando identidade inicial...");
                        authOk = authenticateAnonymous();
                    }

                    // Após 3 falhas consecutivas de refreshToken, tenta nova identidade
                    // (proteção contra refreshToken permanentemente inválido sem código 400 explícito)
                    if (!authOk && _authFailCount >= 3) {
                        Serial.println("[FIREBASE-AUTH] [RTOS] 3 falhas consecutivas de renovacao. Forcando reautenticacao completa...");
                        _authFailCount = 0;
                        resetDeviceIdentity();
                        authOk = authenticateAnonymous();
                    }

                    if (_authenticated) {
                        checkDeviceRegistration();
                        if (!_isPaired) {
                            generatePairingCode();
                        }
                        syncRoutinesInternal();
                        _lastPollTime = now;
                    }
                }
            } else {
                // 3. Renova token a cada 50 minutos
                if (now >= _tokenExpireTime && _tokenExpireTime > 0) {
                    refreshAuthToken();
                }

                // 4. Retentativa de gerar/publicar código de pareamento se pendente.
                // Dispara se: (a) código nunca publicado, (b) código vazio/incompleto,
                // ou (c) intervalo de retry (30s) expirou. A primeira tentativa é imediata
                // (_lastPairCodeRetryTime == 0 no boot) e o código só é trocado se estiver vazio.
                bool needsCode = !_isPaired && (!_codePublished || _pairingCode.length() != 6);
                if (needsCode) {
                    bool firstAttempt = (_lastPairCodeRetryTime == 0);
                    if (firstAttempt || (now - _lastPairCodeRetryTime >= 30000)) {
                        _lastPairCodeRetryTime = now;
                        Serial.printf("[FIREBASE] [RTOS] %s codigo de pareamento no Firestore...\n",
                                      firstAttempt ? "Primeira publicacao do" : "Retentativa de publicar");
                        generatePairingCode();
                    }
                }

                // 5. Polling de rotinas (adaptativo diurno/noturno) ou forçado
                uint32_t pollInterval = getAdaptivePollIntervalMs();
                if (_forceSync || (now - _lastPollTime >= pollInterval)) {
                    bool wasForced = _forceSync;
                    _forceSync = false;
                    _lastPollTime = now;

                    syncRoutinesInternal();

                    if (wasForced) {
                        Serial.println("[FIREBASE-SYNC] [RTOS] Sincronizacao manual: buscando redes Wi-Fi...");
                        fetchWifiNetworks();
                        _lastWifiNetworksPoll = now;
                    }
                }

                // 6. Verificação periódica de pareamento (a cada 3s) se não pareado
                // Intervalo curto para detectar vinculação do site rapidamente
                if (!_isPaired && (now - _lastPairCheckTime >= 3000)) {
                    _lastPairCheckTime = millis();
                    bool regOk = checkDeviceRegistration();
                    // Backoff extra de 3s apenas em FALHA DE REDE (httpCode < 0 ou timeout)
                    // Resposta válida do Firestore (200 ou 404 ou código ainda pendente) não adiciona backoff
                    if (!regOk) {
                        _lastPairCheckTime = millis() + 3000; // falha de rede: aguarda 6s total
                    }
                    if (regOk && _isPaired) {
                        Serial.println("[FIREBASE] [RTOS] Robo vinculado com sucesso pelo site!");
                        _justPaired = true;
                        syncRoutinesInternal();
                        fetchWifiNetworks();
                        _lastWifiNetworksPoll = millis();
                    }
                }

                // 7. Polling de redes Wi-Fi do Firestore a cada 60s
                const uint32_t WIFI_POLL_INTERVAL_MS = 60000UL;
                if (_isPaired && _network != nullptr) {
                    if (now - _lastWifiNetworksPoll >= WIFI_POLL_INTERVAL_MS) {
                        _lastWifiNetworksPoll = now;
                        fetchWifiNetworks();
                    }
                }
            }
        }

        // Cede tempo para o loop principal da UI e demais tarefas
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelete(NULL);
}

bool FirebaseSync::syncRoutinesInternal() {
    if (!_sharedTasks || !_sharedTotalLoaded) return false;
    TaskItem tempTasks[MAX_TASKS];
    uint8_t tempLoaded = 0;
    bool ok = fetchRoutinesFromCloud(tempTasks, _sharedMaxTasks, tempLoaded);
    if (ok) {
        if (_dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            memcpy(_sharedTasks, tempTasks, sizeof(TaskItem) * tempLoaded);
            *_sharedTotalLoaded = tempLoaded;
            _tasksChangedPending = true;
            xSemaphoreGive(_dataMutex);
        }
    }
    return ok;
}

void FirebaseSync::update(uint32_t now, bool isNetworkReady, TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded, bool& tasksChanged) {
    // UI thread é 100% não-bloqueante: apenas verifica a flag atômica sinalizada pela tarefa FreeRTOS
    if (_tasksChangedPending) {
        if (_dataMutex && xSemaphoreTake(_dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            tasksChanged = true;
            _tasksChangedPending = false;
            xSemaphoreGive(_dataMutex);
        }
    }
}

bool FirebaseSync::authenticateAnonymous() {
    // TRAVA ANTI-DUPLICACAO: Se o dispositivo ja tem UID permanente na NVS, bloqueia nova criacao
    if (_deviceUid.length() > 0) {
        Serial.printf("[FIREBASE-AUTH] BLOQUEIO ATIVO: O dispositivo ja possui UID permanente (%s). Criacao de conta duplicada evitada!\n", _deviceUid.c_str());
        return false;
    }

    Serial.println("[FIREBASE-AUTH] Primeiro boot detectado. Solicitando credencial unica ao Google Identity Toolkit...");

    // Valida resolução DNS de identitytoolkit.googleapis.com (mantendo DHCP 100% ativo)
    IPAddress resolvedIP;
    bool dnsOk = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (WiFi.hostByName("identitytoolkit.googleapis.com", resolvedIP) == 1 && resolvedIP != IPAddress(0, 0, 0, 0)) {
            dnsOk = true;
            break;
        }
        if (attempt == 1) {
            // Adiciona Google DNS (8.8.8.8) como servidor DNS de fallback no lwIP sem parar o cliente DHCP
            ip_addr_t fbDns;
            IP_ADDR4(&fbDns, 8, 8, 8, 8);
            dns_setserver(1, &fbDns);
        }
        delay(150);
    }

    Serial.printf("[FIREBASE-AUTH] DNS: %s (IP: %s) | Free Heap: %u | Max Block: %u\n",
                  dnsOk ? "OK" : "FALHOU", resolvedIP.toString().c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    if (!dnsOk || resolvedIP == IPAddress(0, 0, 0, 0)) {
        Serial.println("[FIREBASE-AUTH] AVISO: DNS ainda retornou 0.0.0.0. Aguardando estabilizacao da rede...");
        return false;
    }

    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    http.setTimeout(FIREBASE_HTTP_TIMEOUT_MS);
    http.setReuse(false);

    String url = "https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=" + String(FIREBASE_API_KEY);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");

    String requestBody = "{\"returnSecureToken\":true}";
    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode == 200) {
        String response = http.getString();
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(2048);
#endif
        DeserializationError error = deserializeJson(doc, response);

        if (!error) {
            _idToken       = doc["idToken"].as<String>();
            _refreshToken  = doc["refreshToken"].as<String>();
            _deviceUid     = doc["localId"].as<String>();
            uint32_t exp   = doc["expiresIn"].as<uint32_t>();

            _authenticated = true;
            _tokenExpireTime = millis() + ((exp > 300 ? exp - 300 : 3300) * 1000);

            // Salva credenciais na NVS flash
            preferences.begin("utome_auth", false);
            preferences.putString("deviceUid", _deviceUid);
            preferences.putString("refreshToken", _refreshToken);
            preferences.end();

            Serial.println("[FIREBASE-AUTH] Autenticado com sucesso!");
            Serial.printf("[FIREBASE-AUTH] Device UID do Robo: %s\n", _deviceUid.c_str());
            http.end();
            return true;
        }
    } else {
        String errResponse = http.getString();
        char tlsErr[128] = {0};
        client.lastError(tlsErr, sizeof(tlsErr));
        Serial.printf("[FIREBASE-AUTH] Falha na autenticacao (Codigo HTTP: %d): %s\n", httpResponseCode, errResponse.c_str());
        Serial.printf("[FIREBASE-AUTH] Detalhe HTTP: %s\n", http.errorToString(httpResponseCode).c_str());
        if (strlen(tlsErr) > 0) {
            Serial.printf("[FIREBASE-AUTH] Detalhe TLS/SSL: %s\n", tlsErr);
        }
        Serial.printf("[FIREBASE-AUTH] Diagnostico: IP=%s | GW=%s | DNS=%s | Heap=%u | MaxBlock=%u\n",
                      WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(),
                      WiFi.dnsIP().toString().c_str(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        if (httpResponseCode == 400) {
            Serial.println("[FIREBASE-AUTH] DICA: Certifique-se de que o provedor 'Anonimo' esta ATIVADO no Firebase Console (Authentication > Sign-in method).");
        }
    }

    http.end();
    return false;
}

bool FirebaseSync::refreshAuthToken() {
    Serial.println("[FIREBASE-AUTH] Renovando token de acesso do Firebase...");

    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    http.setTimeout(FIREBASE_HTTP_TIMEOUT_MS);

    String url = "https://securetoken.googleapis.com/v1/token?key=" + String(FIREBASE_API_KEY);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");

    String payload = "grant_type=refresh_token&refresh_token=" + _refreshToken;
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode == 200) {
        String response = http.getString();
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(2048);
#endif
        DeserializationError error = deserializeJson(doc, response);

        if (!error) {
            _idToken       = doc["id_token"].as<String>();
            _refreshToken  = doc["refresh_token"].as<String>();
            uint32_t exp   = doc["expires_in"].as<uint32_t>();

            _authenticated = true;
            _tokenExpireTime = millis() + ((exp > 300 ? exp - 300 : 3300) * 1000);

            preferences.begin("utome_auth", false);
            preferences.putString("refreshToken", _refreshToken);
            preferences.end();

            Serial.println("[FIREBASE-AUTH] Token renovado com sucesso!");
            http.end();
            return true;
        }
    } else {
        String errRes = http.getString();
        char tlsErr[128] = {0};
        client.lastError(tlsErr, sizeof(tlsErr));
        Serial.printf("[FIREBASE-AUTH] Falha na renovacao do token (HTTP %d): %s\n", httpResponseCode, errRes.c_str());
        if (strlen(tlsErr) > 0) {
            Serial.printf("[FIREBASE-AUTH] Detalhe TLS: %s\n", tlsErr);
        }

        if (httpResponseCode == 400) {
            // [A8] Só faz reset/nova auth em erro 400 com corpo indicando token inválido.
            // Evita resetar identidade por erro de rede transitório (httpCode < 0).
            bool tokenInvalid = errRes.indexOf("INVALID_REFRESH_TOKEN") >= 0 ||
                                errRes.indexOf("USER_NOT_FOUND") >= 0 ||
                                errRes.indexOf("USER_DISABLED") >= 0 ||
                                errRes.indexOf("TOKEN_EXPIRED") >= 0;
            if (tokenInvalid) {
                Serial.println("[FIREBASE-AUTH] Token permanentemente invalido. Auto-cura: limpando NVS e autenticando novamente...");
                resetDeviceIdentity();
                http.end();
                return authenticateAnonymous();
            } else {
                Serial.println("[FIREBASE-AUTH] HTTP 400 sem indicacao clara de token invalido. Mantendo identidade; tentando na proxima janela...");
            }
        } else if (httpResponseCode < 0) {
            // Erro de rede/TLS transitório: mantém _authenticated para não entrar em loop
            // de criação de conta desnecessário. O worker vai tentar de novo em 10s.
            Serial.printf("[FIREBASE-AUTH] Erro de rede transitorio (HTTP %d). Mantendo estado auth para retry automatico.\n", httpResponseCode);
            http.end();
            return false;
        }
    }

    http.end();
    return false;
}

bool FirebaseSync::checkDeviceRegistration() {
    if (!_authenticated || _deviceUid.length() == 0) return false;

    // Timeout reduzido para resposta rápida ao usuário durante a vinculação
    WiFiClientSecure client;
    configureClient(client);
    client.setTimeout(FIREBASE_PAIR_CHECK_TIMEOUT_MS);
    HTTPClient http;
    http.setTimeout(FIREBASE_PAIR_CHECK_TIMEOUT_MS);

    String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                 "/databases/(default)/documents/devices/" + _deviceUid;

    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + _idToken);
    http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");

    int httpCode = http.GET();

    if (httpCode == 200) {
        String response = http.getString();
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(2048);
#endif
        deserializeJson(doc, response);

        if (doc["fields"]["ownerUid"]["stringValue"].is<const char*>() &&
            strlen(doc["fields"]["ownerUid"]["stringValue"].as<const char*>()) > 0) {
            _ownerUid = doc["fields"]["ownerUid"]["stringValue"].as<String>();
            _isPaired = true;

            preferences.begin("utome_auth", false);
            preferences.putString("ownerUid", _ownerUid);
            preferences.end();

            Serial.printf("[FIREBASE] Robo pareado com o usuario: %s\n", _ownerUid.c_str());

            // Garante que o status no documento do robô esteja marcado como online
            http.end();
            http.begin(client, url + "?updateMask.fieldPaths=status");
            http.addHeader("Authorization", "Bearer " + _idToken);
            http.addHeader("Content-Type", "application/json");
            http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");
            http.PATCH("{\"fields\":{\"status\":{\"stringValue\":\"online\"}}}");
            http.end();
            return true;
        } else {
            // Dispositivo existente sem dono vinculado no Firestore
            _isPaired = false;
            if (_ownerUid.length() > 0) {
                _ownerUid = "";
                preferences.begin("utome_auth", false);
                preferences.remove("ownerUid");
                preferences.end();
            }

            // Log informativo throttled (a cada 10s) para manter o monitor limpo
            static uint32_t lastUnpairedLog = 0;
            if (millis() - lastUnpairedLog >= 10000 || lastUnpairedLog == 0) {
                lastUnpairedLog = millis();
                Serial.printf("[FIREBASE] Robo aguardando vinculacao no site! Codigo ativo: [%s]\n",
                              _pairingCode.length() > 0 ? _pairingCode.c_str() : "Pendente");
            }

            // Se por algum motivo o código de pareamento ainda não foi gerado/publicado, gera agora
            if (!_codePublished || _pairingCode.length() != 6) {
                generatePairingCode();
            }
        }
        http.end();
        return true;
    } else if (httpCode == 404) {
        // Documento ainda não existe no Firestore: cria documento base
        Serial.println("[FIREBASE] Documento do dispositivo nao existe (404). Criando documento base...");
        _isPaired = false;
        if (_ownerUid.length() > 0) {
            _ownerUid = "";
            preferences.begin("utome_auth", false);
            preferences.remove("ownerUid");
            preferences.end();
        }

        http.end();
        http.begin(client, url);
        http.addHeader("Authorization", "Bearer " + _idToken);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");

        String createDoc = "{\"fields\":{\"status\":{\"stringValue\":\"online\"},\"fw\":{\"stringValue\":\"2.0\"}}}";
        http.PATCH(createDoc);
        http.end();

        if (!_codePublished || _pairingCode.length() != 6) {
            generatePairingCode();
        }
        return true;
    }

    http.end();
    return false;
}

bool FirebaseSync::generatePairingCode() {
    _codePublished = false;

    // Se ainda não autenticado, tenta autenticar antes de publicar
    if (!_authenticated) {
        if (_deviceUid.length() > 0 && _refreshToken.length() > 0) {
            refreshAuthToken();
        } else {
            authenticateAnonymous();
        }
    }

    if (!_authenticated || _deviceUid.length() == 0) {
        Serial.println("[FIREBASE] ERRO: Nao e possivel gerar codigo sem autenticacao previa no Firebase!");
        return false;
    }

    // [FIX-PAIRING] Gera um novo código aleatório APENAS se ainda não temos um.
    // Em retentativas de publicação, reutiliza o mesmo código para o usuário
    // não precisar redigitar no site enquanto o robô tenta novamente.
    if (_pairingCode.length() != 6) {
        uint32_t randomNum = esp_random() % 900000 + 100000;
        _pairingCode = String(randomNum);
    }

    Serial.println();
    Serial.println("==========================================================");
    Serial.printf("  [PAREAMENTO] PUBLICANDO CODIGO: %s\n", _pairingCode.c_str());
    Serial.println("  Destino: Firestore pairingCodes/" + _pairingCode + " (Validade: 24h)");
    Serial.println("==========================================================");

    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    http.setTimeout(FIREBASE_HTTP_TIMEOUT_MS);

    String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                 "/databases/(default)/documents/pairingCodes/" + _pairingCode;

    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + _idToken);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");

    // [FIX-C9] Timestamp de expiração sempre no futuro.
    // Se o NTP ainda não sincronizou, usa uma base segura de 2 anos no futuro (2028).
    // Assim o código nunca nasce expirado, independente do relógio do robô.
    time_t nowSec = time(nullptr);
    if (nowSec < 1000000000) {
        // NTP não sincronizou: base segura = 1 Jan 2028 00:00:00 UTC
        nowSec = 1830384000;
    }
    time_t expireSec = nowSec + 86400; // 24 horas de validade
    struct tm* tmInfo = gmtime(&expireSec);
    char expireBuf[32];
    strftime(expireBuf, sizeof(expireBuf), "%Y-%m-%dT%H:%M:%SZ", tmInfo);

    String docPayload = "{\"fields\":{\"deviceUid\":{\"stringValue\":\"" + _deviceUid + "\"},\"expiresAt\":{\"timestampValue\":\"" + String(expireBuf) + "\"}}}";
    int httpCode = http.PATCH(docPayload);

    if (httpCode == 200) {
        _codePublished = true;
        Serial.printf("[FIREBASE] SUCESSO: Codigo %s publicado no Firestore! Validade: %s\n",
                      _pairingCode.c_str(), expireBuf);
        http.end();
        return true;
    } else {
        String errRes = http.getString();
        Serial.printf("[FIREBASE] ERRO ao publicar codigo no Firestore (HTTP %d): %s\n", httpCode, errRes.c_str());
        http.end();
        return false;
    }
}

bool FirebaseSync::fetchRoutinesFromCloud(TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded) {
    if (!_authenticated || _deviceUid.length() == 0) return false;

    Serial.println("[FIREBASE-SYNC] Consultando rotinas agendadas no Firestore...");

    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    http.setTimeout(FIREBASE_HTTP_TIMEOUT_MS);

    String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                 "/databases/(default)/documents/devices/" + _deviceUid + "/routines";

    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + _idToken);
    http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");

    int httpCode = http.GET();

    if (httpCode == 200) {
        String response = http.getString();
#if ARDUINOJSON_VERSION_MAJOR >= 7
        JsonDocument doc;
#else
        DynamicJsonDocument doc(4096);
#endif
        DeserializationError err = deserializeJson(doc, response);

        if (!err && doc["documents"].is<JsonArray>()) {
            JsonArray docs = doc["documents"].as<JsonArray>();
            uint8_t count = 0;
            _totalCloudRoutines = (uint8_t)docs.size();
            _routineLimitExceeded = (_totalCloudRoutines > maxTasks);

            // [PRIORIDADE 2.4] Alerta claro no Serial caso a nuvem tenha mais rotinas que o limite do firmware
            if (_routineLimitExceeded) {
                Serial.println();
                Serial.println("******************************************************************");
                Serial.printf(" [AVISO-LIMITE] A nuvem possui %u rotinas cadastradas!\n", _totalCloudRoutines);
                Serial.printf(" O UTOME suporta ate %u rotinas na memoria. As %u rotinas excedentes foram ignoradas.\n",
                              maxTasks, _totalCloudRoutines - maxTasks);
                Serial.println(" DICA: Remova ou reorganize rotinas antigas na plataforma web.");
                Serial.println("******************************************************************");
                Serial.println();
            }

            for (JsonObject item : docs) {
                if (count >= maxTasks) break;

                String fullPath = item["name"].as<String>();
                int lastSlash = fullPath.lastIndexOf('/');
                String routineId = (lastSlash != -1) ? fullPath.substring(lastSlash + 1) : fullPath;

                JsonObject fields = item["fields"];
                String title = fields["title"]["stringValue"].as<String>();
                String timeStr = fields["time"]["stringValue"].as<String>();
                String priorityStr = fields["priority"]["stringValue"].as<String>();
                bool completed = fields["completed"]["booleanValue"] | false;

                // Determina prioridade P1, P2 ou P3
                TaskPriority p = PRIORITY_P2;
                if (priorityStr.equalsIgnoreCase("P1") || priorityStr.equalsIgnoreCase("urgente")) {
                    p = PRIORITY_P1;
                } else if (priorityStr.equalsIgnoreCase("P3") || priorityStr.equalsIgnoreCase("baixa")) {
                    p = PRIORITY_P3;
                }

                // Preenche o TaskItem no array em RAM
                snprintf(tasksList[count].id, sizeof(tasksList[count].id), "%s", routineId.c_str());
                snprintf(tasksList[count].title, sizeof(tasksList[count].title), "%s", title.c_str());
                snprintf(tasksList[count].time, sizeof(tasksList[count].time), "%s", timeStr.c_str());
                tasksList[count].priority = p;
                tasksList[count].completed = completed;

                count++;
            }

            totalLoaded = count;
            Serial.printf("[FIREBASE-SYNC] %u rotinas sincronizadas com sucesso da nuvem!\n", totalLoaded);

            // [M3] Ordena rotinas por horário ("HH:MM") e, em caso de empate, por prioridade (P1>P2>P3)
            // Bubble sort — máx 10 itens, custo O(n²) irrelevante nesta escala
            for (uint8_t i = 0; i < totalLoaded && totalLoaded > 1; i++) {
                for (uint8_t j = 0; j < totalLoaded - 1 - i; j++) {
                    // Compara "HH:MM" como string — funciona porque o formato é fixo
                    int cmpTime = strncmp(tasksList[j].time, tasksList[j + 1].time, 5);
                    bool swap = false;
                    if (cmpTime > 0) {
                        swap = true; // horário maior = mais tarde no dia
                    } else if (cmpTime == 0) {
                        // Mesmo horário: P1 (urgente) fica antes de P2 e P3
                        swap = (tasksList[j].priority > tasksList[j + 1].priority);
                    }
                    if (swap) {
                        TaskItem tmp   = tasksList[j];
                        tasksList[j]   = tasksList[j + 1];
                        tasksList[j + 1] = tmp;
                    }
                }
            }

            // Salva na memória flash para tolerância a falhas offline
            saveTasksToFlash(tasksList, totalLoaded);

            http.end();
            return true;
        } else if (!err) {
            // Se o Firestore retornou 200 sem o array "documents", significa que não há rotinas cadastradas
            totalLoaded = 0;
            _totalCloudRoutines = 0;
            _routineLimitExceeded = false;
            saveTasksToFlash(tasksList, 0);
            Serial.println("[FIREBASE-SYNC] Nenhuma rotina cadastrada nesta conta. Lista zerada.");
            http.end();
            return true;
        } else {
            // [C4] Falha de deserialização JSON: NUNCA ZERAR AS TAREFAS SALVAS NA NVS/FLASH!
            Serial.printf("[FIREBASE-SYNC] Erro ao deserializar JSON de rotinas: %s. Cache local mantido intacto!\n", err.c_str());
            http.end();
            return false;
        }
    } else {
        Serial.printf("[FIREBASE-SYNC] Consulta Firestore retornou codigo HTTP: %d\n", httpCode);
    }

    http.end();
    return false;
}

bool FirebaseSync::reportTaskCompleted(const TaskItem* task) {
    if (!task) return false;

    // Enfileiramento assíncrono (A9): resposta em <0.1ms para não travar UI
    TaskCompletionEvent evt;
    memset(&evt, 0, sizeof(evt));
    strncpy(evt.id, task->id, sizeof(evt.id) - 1);
    strncpy(evt.title, task->title, sizeof(evt.title) - 1);
    evt.timestamp = time(nullptr);

    if (_completionQueue) {
        if (xQueueSend(_completionQueue, &evt, 0) == pdPASS) {
            Serial.printf("[FIREBASE-LOG] Conclusao de rotina enfileirada (assincrono <0.1ms): [%s]\n", evt.title);
            return true;
        } else {
            Serial.println("[FIREBASE-LOG] AVISO: Fila de conclusao de tarefas cheia!");
            return false;
        }
    }
    return false;
}

void FirebaseSync::processCompletionQueue() {
    if (!_completionQueue) return;
    TaskCompletionEvent evt;
    uint8_t count = 0;
    while (xQueueReceive(_completionQueue, &evt, 0) == pdPASS && count < 3) {
        count++;
        if (_authenticated && _deviceUid.length() > 0) {
            sendCompletionToCloud(evt);
        } else {
            Serial.printf("[FIREBASE-LOG] Dispositivo desconectado ou sem auth, adiando conclusao de: %s\n", evt.title);
            xQueueSend(_completionQueue, &evt, 0);
            break;
        }
    }
}

bool FirebaseSync::sendCompletionToCloud(const TaskCompletionEvent& evt) {
    if (!_authenticated || _deviceUid.length() == 0) return false;

    Serial.printf("[FIREBASE-LOG] [RTOS] Enviando conclusao da tarefa em background: [%s]...\n", evt.title);

    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    http.setTimeout(FIREBASE_HTTP_TIMEOUT_MS);

    // 1. Registra no histórico do robô: devices/{uid}/syncLogs com timestamp ISO
    time_t nowSec = evt.timestamp;
    if (nowSec < 1000000000) {
        nowSec = time(nullptr);
    }
    char timeBuf[32] = "";
    if (nowSec >= 1000000000) {
        struct tm* tmInfo = gmtime(&nowSec);
        if (tmInfo) {
            strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", tmInfo);
        }
    }

    String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                 "/databases/(default)/documents/devices/" + _deviceUid + "/syncLogs";

    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + _idToken);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");

    String logBody = "{\"fields\":{"
                     "\"routineId\":{\"stringValue\":\"" + String(evt.id) + "\"},"
                     "\"routineTitle\":{\"stringValue\":\"" + String(evt.title) + "\"},"
                     "\"completed\":{\"booleanValue\":true}";
    if (strlen(timeBuf) > 0) {
        logBody += ",\"timestamp\":{\"timestampValue\":\"" + String(timeBuf) + "\"}";
    }
    logBody += "}}";

    int httpCode = http.POST(logBody);
    http.end();

    // 2. Atualiza status no documento da rotina: devices/{uid}/routines/{id}
    if (strlen(evt.id) > 0) {
        String routineUrl = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                            "/databases/(default)/documents/devices/" + _deviceUid + "/routines/" + String(evt.id) +
                            "?updateMask.fieldPaths=completed";

        http.begin(client, routineUrl);
        http.addHeader("Authorization", "Bearer " + _idToken);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");

        String updateBody = "{\"fields\":{\"completed\":{\"booleanValue\":true}}}";
        http.PATCH(updateBody);
        http.end();
    }

    Serial.println("[FIREBASE-LOG] [RTOS] Conclusao registrada com sucesso no Firestore em background!");
    return (httpCode == 200 || httpCode == 201);
}

void FirebaseSync::saveTasksToFlash(const TaskItem* tasksList, uint8_t count) {
    preferences.begin("utome_tasks", false);
    preferences.putUChar("count", count);

    for (uint8_t i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "t_%u", i);
        preferences.putBytes(key, &tasksList[i], sizeof(TaskItem));
    }
    preferences.end();
    Serial.println("[CACHE-NVS] Rotinas salvas em memoria permanente para operacao offline.");
}

uint8_t FirebaseSync::loadTasksFromFlash(TaskItem* tasksList, uint8_t maxTasks) {
    preferences.begin("utome_tasks", true);
    uint8_t count = preferences.getUChar("count", 0);

    if (count > maxTasks) count = maxTasks;

    for (uint8_t i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), "t_%u", i);
        preferences.getBytes(key, &tasksList[i], sizeof(TaskItem));
    }
    preferences.end();

    if (count > 0) {
        Serial.printf("[CACHE-NVS] %u rotinas offline carregadas com sucesso da memoria flash.\n", count);
    }
    return count;
}

bool FirebaseSync::syncRoutinesNow(TaskItem* tasksList, uint8_t maxTasks, uint8_t& totalLoaded) {
    return fetchRoutinesFromCloud(tasksList, maxTasks, totalLoaded);
}

void FirebaseSync::printSyncStatus() const {
    Serial.println();
    Serial.println("------------- STATUS DO FIREBASE & PAREAMENTO -------------");
    Serial.printf(" Autenticado:       %s\n", _authenticated ? "SIM (Online)" : "NAO (Pendente)");
    Serial.printf(" Device UID:        %s\n", _deviceUid.length() > 0 ? _deviceUid.c_str() : "(Nenhum)");
    Serial.printf(" Status Pareamento: %s\n", _isPaired ? "PAREADO COM O RESPONSAVEL" : "AGUARDANDO CODIGO");
    if (_isPaired) {
        Serial.printf(" Responsavel (UID): %s\n", _ownerUid.c_str());
    } else {
        Serial.printf(" Codigo de Pareamento: [%s] (Valido no site)\n",
                      _pairingCode.length() > 0 ? _pairingCode.c_str() : "Pendente de geracao");
    }
    Serial.println("-----------------------------------------------------------");
    Serial.println();
}

void FirebaseSync::resetDeviceIdentity() {
    Serial.println("[FIREBASE-AUTH] RESET DE IDENTIDADE: Limpando credenciais da NVS Flash...");
    preferences.begin("utome_auth", false);
    preferences.remove("deviceUid");
    preferences.remove("refreshToken");
    preferences.remove("ownerUid");
    preferences.end();

    _deviceUid = "";
    _refreshToken = "";
    _ownerUid = "";
    _idToken = "";
    _authenticated = false;
    _isPaired = false;
    _codePublished = false;
    _pairingCode = "";
    Serial.println("[FIREBASE-AUTH] NVS limpa! No proximo ciclo/boot uma nova identidade unica sera gerada.");
}

uint32_t FirebaseSync::getAdaptivePollIntervalMs() const {
    time_t nowSec = time(nullptr);
    if (nowSec >= 1000000000) {
        struct tm* tmInfo = localtime(&nowSec);
        if (tmInfo) {
            // Horário noturno (23h às 06h): economia de rede e leituras Firestore (5 minutos)
            if (tmInfo->tm_hour >= 23 || tmInfo->tm_hour < 6) {
                return 300000;
            }
        }
    }
    return FIREBASE_POLL_INTERVAL_MS; // 60s padrão ativo diurno
}

// =============================================================================
// FETCH DE REDES WI-FI DO FIRESTORE
// Lê a subcoleção devices/{deviceUid}/wifiNetworks, ordena por priority
// e injeta no NetworkManager para roaming automático.
// =============================================================================
bool FirebaseSync::fetchWifiNetworks() {
    if (!_authenticated || _deviceUid.length() == 0 || _network == nullptr) {
        return false;
    }

    Serial.println("[FIREBASE-WIFI] Buscando redes Wi-Fi salvas no Firestore...");

    WiFiClientSecure client;
    configureClient(client);
    HTTPClient http;
    http.setTimeout(FIREBASE_HTTP_TIMEOUT_MS);

    // 1. Endpoint da subcoleção de redes do robô (lista limpa sem query string quebrada)
    String url = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                 "/databases/(default)/documents/devices/" + _deviceUid +
                 "/wifiNetworks";

    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + _idToken);
    http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");

    int httpCode = http.GET();

    if (httpCode != 200) {
        String errBody = http.getString();
        Serial.printf("[FIREBASE-WIFI] Erro ao buscar redes Wi-Fi (HTTP %d): %s\n", httpCode, errBody.c_str());
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(8192);
#endif

    DeserializationError err = deserializeJson(doc, response);
    if (err) {
        Serial.printf("[FIREBASE-WIFI] Erro ao parsear JSON: %s\n", err.c_str());
        return false;
    }

    JsonArray documents = doc["documents"].as<JsonArray>();

    // Se não encontrou redes no dispositivo mas o robô tem dono vinculado (_ownerUid),
    // busca como fallback na conta do usuário (/users/{ownerUid}/wifiNetworks)
    if ((documents.isNull() || documents.size() == 0) && _ownerUid.length() > 0) {
        Serial.printf("[FIREBASE-WIFI] Nenhuma rede em devices/%s. Verificando perfil do dono (%s)...\n",
                      _deviceUid.c_str(), _ownerUid.c_str());
        String userUrl = "https://firestore.googleapis.com/v1/projects/" + String(FIREBASE_PROJECT_ID) +
                         "/databases/(default)/documents/users/" + _ownerUid + "/wifiNetworks";
        http.begin(client, userUrl);
        http.addHeader("Authorization", "Bearer " + _idToken);
        http.addHeader("Referer", "https://" FIREBASE_PROJECT_ID ".firebaseapp.com");
        int uCode = http.GET();
        if (uCode == 200) {
            String uResp = http.getString();
            http.end();
            deserializeJson(doc, uResp);
            documents = doc["documents"].as<JsonArray>();
        } else {
            http.end();
        }
    }

    if (documents.isNull() || documents.size() == 0) {
        Serial.println("[FIREBASE-WIFI] Nenhuma rede Wi-Fi cadastrada no dashboard ainda.");
        WifiCredential empty[1];
        _network->setCloudNetworks(empty, 0);
        return true;
    }

    // Monta array temporário de credenciais
    WifiCredential nets[WIFI_MAX_CLOUD_NETWORKS];
    uint8_t count = 0;

    for (JsonObject docObj : documents) {
        if (count >= WIFI_MAX_CLOUD_NETWORKS) break;

        JsonObject fields = docObj["fields"];
        if (fields.isNull()) continue;

        const char* ssid = fields["ssid"]["stringValue"] | "";
        const char* pass = fields["password"]["stringValue"] | "";

        // Extração resiliente da prioridade (compatível com int64, double ou string do REST)
        int prio = count; // valor padrão pela ordem de inserção
        if (fields["priority"].is<JsonObject>()) {
            JsonObject prioObj = fields["priority"].as<JsonObject>();
            if (prioObj.containsKey("integerValue")) {
                prio = prioObj["integerValue"].as<int>();
            } else if (prioObj.containsKey("doubleValue")) {
                prio = (int)prioObj["doubleValue"].as<double>();
            } else if (prioObj.containsKey("stringValue")) {
                prio = prioObj["stringValue"].as<int>();
            }
        }

        if (strlen(ssid) == 0) continue; // Ignora documentos malformados

        nets[count].ssid     = String(ssid);
        nets[count].password = String(pass);
        nets[count].priority = (uint8_t)(prio < 0 ? 0 : (prio > 9 ? 9 : prio));
        count++;
    }

    // Ordena por priority (bubble sort — máx 10 itens, custo irrelevante)
    for (uint8_t i = 0; i < count - 1; i++) {
        for (uint8_t j = 0; j < count - 1 - i; j++) {
            if (nets[j].priority > nets[j + 1].priority) {
                WifiCredential tmp = nets[j];
                nets[j]           = nets[j + 1];
                nets[j + 1]       = tmp;
            }
        }
    }

    // Injeta no NetworkManager
    _network->setCloudNetworks(nets, count);

    // Imprime no Monitor Serial detalhamento claro das redes sincronizadas
    Serial.printf("[FIREBASE-WIFI] >>> SUCESSO: %u rede(s) Wi-Fi sincronizadas do site! <<<\n", count);
    for (uint8_t i = 0; i < count; i++) {
        Serial.printf("  [%u] Prioridade %u: SSID '%s'%s\n",
                      i + 1, nets[i].priority, nets[i].ssid.c_str(),
                      nets[i].priority == 0 ? " (Principal)" : " (Backup)");
    }

    // Persiste as redes na NVS local para que o robo possa usa-las no proximo boot
    // mesmo sem internet (carregadas via loadCloudNetworksFromFlash no caminho offline)
    {
        Preferences wifiPrefs;
        wifiPrefs.begin("utome_wifi_c", false);
        wifiPrefs.putUChar("count", count);
        for (uint8_t i = 0; i < count; i++) {
            String keyS = "ssid" + String(i);
            String keyP = "pass" + String(i);
            String keyPr = "prio" + String(i);
            wifiPrefs.putString(keyS.c_str(), nets[i].ssid);
            wifiPrefs.putString(keyP.c_str(), nets[i].password);
            wifiPrefs.putUChar(keyPr.c_str(), nets[i].priority);
        }
        wifiPrefs.end();
        Serial.printf("[FIREBASE-WIFI] %u rede(s) persistidas na NVS para uso offline.\n", count);
    }

    // Se nao esta conectado, dispara roaming imediato para tentar a rede principal
    if (!_network->isConnected() && count > 0) {
        _network->triggerCloudRoam();
    }

    Serial.printf("[FIREBASE-WIFI] %u rede(s) carregadas e prontas para roaming.\n", count);
    return true;
}
