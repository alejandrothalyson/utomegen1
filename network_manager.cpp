#include "network_manager.h"
#include <esp_wifi.h>

// ============================================================================
// TEMPLATES HTML DO PORTAL CATIVO (Armazenados na Flash PROGMEM)
// ============================================================================

static const char PORTAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>UTOME 1 - Configurar Wi-Fi</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: #FAF7F2;
      color: #2B2625;
      padding: 1.5rem 1rem;
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .card {
      background: #FFFFFF;
      border-radius: 24px;
      padding: 2.2rem 1.6rem;
      max-width: 400px;
      width: 100%;
      box-shadow: 0 12px 36px rgba(0,0,0,0.06);
      border: 1px solid rgba(70,50,40,0.07);
      text-align: center;
    }
    .logo-badge {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      width: 58px;
      height: 58px;
      border-radius: 50%;
      background: rgba(255, 104, 56, 0.12);
      color: #FF6838;
      font-size: 28px;
      margin-bottom: 1rem;
    }
    h1 { font-size: 1.45rem; font-weight: 800; color: #2B2625; margin-bottom: 0.3rem; letter-spacing: -0.02em; }
    p.desc { font-size: 0.88rem; color: #615A56; margin-bottom: 1.6rem; line-height: 1.45; }
    .form-group { text-align: left; margin-bottom: 1.25rem; }
    label { display: block; font-size: 0.8rem; font-weight: 700; color: #2B2625; margin-bottom: 0.45rem; }
    select, input[type="text"], input[type="password"] {
      width: 100%;
      padding: 0.85rem 1rem;
      border: 1.5px solid #E5E0D8;
      border-radius: 14px;
      font-size: 0.95rem;
      background: #FAF7F2;
      color: #2B2625;
      outline: none;
      transition: border 0.2s, background 0.2s;
    }
    select:focus, input:focus { border-color: #FF6838; background: #FFFFFF; }
    .pass-wrap { position: relative; }
    .btn-show {
      position: absolute;
      right: 12px;
      top: 50%;
      transform: translateY(-50%);
      background: none;
      border: none;
      color: #8E8580;
      cursor: pointer;
      font-size: 0.8rem;
      font-weight: 700;
      padding: 4px;
    }
    .btn-submit {
      width: 100%;
      background: linear-gradient(135deg, #FF6838 0%, #FF5018 100%);
      color: white;
      border: none;
      padding: 0.95rem;
      border-radius: 9999px;
      font-size: 1rem;
      font-weight: 700;
      cursor: pointer;
      box-shadow: 0 6px 20px rgba(255, 104, 56, 0.3);
      margin-top: 0.6rem;
      transition: transform 0.1s, box-shadow 0.2s;
    }
    .btn-submit:active { transform: scale(0.98); }
    .footer-note { font-size: 0.75rem; color: #8E8580; margin-top: 1.4rem; }
  </style>
</head>
<body>
  <div class="card">
    <div class="logo-badge">🤖</div>
    <h1>UTOME 1</h1>
    <p class="desc">Conecte o seu pequeno robô à rede Wi-Fi da sua casa para sincronizar rotinas e afeto.</p>
    <form action="/save" method="POST">
      <div class="form-group">
        <label for="ssid">Rede Wi-Fi (2.4 GHz):</label>
        <select id="ssid" name="ssid" onchange="toggleCustom(this.value)">
          {{NETWORKS}}
          <option value="__custom__">Digitar outra rede...</option>
        </select>
      </div>
      <div class="form-group" id="customGroup" style="display:none;">
        <label for="customSsid">Nome da Rede Manual:</label>
        <input type="text" id="customSsid" placeholder="Nome exato do Wi-Fi">
      </div>
      <div class="form-group">
        <label for="pass">Senha:</label>
        <div class="pass-wrap">
          <input type="password" id="pass" name="password" placeholder="Digite a senha do Wi-Fi">
          <button type="button" class="btn-show" onclick="togglePass()">VER</button>
        </div>
      </div>
      <button type="submit" class="btn-submit" onclick="prepSubmit()">Salvar e Conectar</button>
    </form>
    <p class="footer-note">UTOME Robotics • Tecnologia Assistiva</p>
  </div>
  <script>
    function toggleCustom(val) {
      document.getElementById('customGroup').style.display = (val === '__custom__') ? 'block' : 'none';
    }
    function togglePass() {
      var p = document.getElementById('pass');
      p.type = (p.type === 'password') ? 'text' : 'password';
    }
    function prepSubmit() {
      var s = document.getElementById('ssid');
      if (s.value === '__custom__') {
        var c = document.getElementById('customSsid').value;
        if (c.trim() !== '') s.options[s.selectedIndex].value = c.trim();
      }
    }
  </script>
</body>
</html>
)rawliteral";

static const char SUCCESS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Wi-Fi Configurado!</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: #FAF7F2;
      color: #2B2625;
      padding: 2rem 1rem;
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
    }
    .card {
      background: #FFFFFF;
      border-radius: 24px;
      padding: 2.4rem 1.6rem;
      max-width: 380px;
      width: 100%;
      text-align: center;
      box-shadow: 0 12px 36px rgba(0,0,0,0.06);
    }
    .icon { font-size: 42px; margin-bottom: 1rem; }
    h1 { font-size: 1.4rem; font-weight: 800; color: #2B2625; margin-bottom: 0.5rem; }
    p { font-size: 0.92rem; color: #615A56; line-height: 1.5; margin-bottom: 1.2rem; }
    .note { font-size: 0.82rem; color: #8E8580; background: #FAF7F2; padding: 0.8rem; border-radius: 12px; }
  </style>
</head>
<body>
  <div class="card">
    <div class="icon">✨</div>
    <h1>Configuração Salva!</h1>
    <p>O UTOME 1 recebeu suas credenciais e está conectando à sua rede Wi-Fi agora mesmo.</p>
    <div class="note">Olhe para o visor OLED do robô para acompanhar a conexão.</div>
  </div>
</body>
</html>
)rawliteral";

// ============================================================================
// IMPLEMENTAÇÃO DE UtomeNetwork
// ============================================================================

UtomeNetwork::UtomeNetwork() :
    _isConnected(false),
    _timeSynced(false),
    _lastReconnectAttempt(0),
    _reconnectStartTime(0),
    _reconnecting(false),
    _lastTimeCheck(0),
    _ntpConfigured(false),
    _isApMode(false),
    _justConfigured(false),
    _pendingApShutdown(false),
    _configuredTime(0),
    _apStartTime(0),
    _apSSID(""),
    _savedSSID(""),
    _savedPassword(""),
    _cloudNetworkCount(0),
    _roamIndex(0),
    _lastRoamAttempt(0),
    _roamPending(false),
    _savedNetFailCount(0),
    _savedNetFailWindowStart(0),
    _usingCloudRoam(false),
    _server(nullptr),
    _dnsServer(nullptr)
{
}

void UtomeNetwork::loadCredentialsFromFlash() {
    Preferences prefs;
    prefs.begin("utome_wifi", true); // Modo somente leitura
    _savedSSID = prefs.getString("ssid", "");
    _savedPassword = prefs.getString("password", "");
    prefs.end();

    if (_savedSSID.length() > 0) {
        Serial.printf("[WIFI] Credenciais carregadas da Flash NVS: SSID = '%s'\n", _savedSSID.c_str());
    } else if (strcmp(WIFI_SSID, "SEU_WIFI_AQUI") != 0 && strcmp(WIFI_SSID, "sua_rede_wifi") != 0 && strlen(WIFI_SSID) > 0) {
        // Se a Flash estiver vazia, utiliza as credenciais do config.h
        _savedSSID = String(WIFI_SSID);
        _savedPassword = String(WIFI_PASSWORD);
        Serial.printf("[WIFI] Flash vazia. Usando credenciais do config.h: SSID = '%s'\n", _savedSSID.c_str());
    }
}

bool UtomeNetwork::hasSavedCredentials() const {
    return (_savedSSID.length() > 0 && _savedSSID != "SEU_WIFI_AQUI" && _savedSSID != "sua_rede_wifi");
}

void UtomeNetwork::saveCredentials(const String& ssid, const String& password) {
    _savedSSID = ssid;
    _savedPassword = password;

    Preferences prefs;
    prefs.begin("utome_wifi", false); // Leitura e escrita
    prefs.putString("ssid", _savedSSID);
    prefs.putString("password", _savedPassword);
    prefs.end();

    Serial.printf("[WIFI] Novas credenciais salvas na Flash NVS: SSID = %s\n", _savedSSID.c_str());
}

void UtomeNetwork::clearSavedCredentials() {
    Preferences prefs;
    prefs.begin("utome_wifi", false);
    prefs.clear();
    prefs.end();

    _savedSSID = "";
    _savedPassword = "";
    Serial.println("[WIFI] Credenciais apagadas da memoria Flash!");
}

void UtomeNetwork::init() {
    Serial.println();
    Serial.println("[NETWORK] Inicializando subsistema de rede...");

    loadCredentialsFromFlash();

    // Configurações críticas para estabilidade do ESP32-C3:
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);                    // Desativa modem sleep (elimina quedas e timeouts de DHCP no ESP32-C3)
    WiFi.setTxPower(WIFI_DEFAULT_TX_POWER);  // 15 dBm: Estabiliza alimentação 3.3V do ESP32-C3 Supermini
    WiFi.setAutoReconnect(true);

    _lastReconnectAttempt = millis();
    _reconnecting = false;
    _reconnectStartTime = 0;

    if (!hasSavedCredentials()) {
        Serial.println("[WIFI] Nenhuma credencial valida salva na Flash.");
        Serial.println("[WIFI] O Portal de Configuracao (Ponto de Acesso) sera disponibilizado.");
        _isConnected = false;
        return;
    }

    Serial.printf("[WIFI] Conectando a rede salva: %s...\n", _savedSSID.c_str());
    _reconnecting = true;
    _reconnectStartTime = millis();
    if (_savedPassword.length() > 0) {
        WiFi.begin(_savedSSID.c_str(), _savedPassword.c_str());
    } else {
        WiFi.begin(_savedSSID.c_str()); // Suporte transparente para redes abertas sem senha
    }

    _isConnected = (WiFi.status() == WL_CONNECTED);
    if (_isConnected) {
        _reconnecting = false;
        syncNtpTime();
    }
}

void UtomeNetwork::syncNtpTime() {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
    _ntpConfigured = true;
    Serial.printf("[NTP] Servidores NTP configurados: %s, %s (Fuso: UTC-3)\n", NTP_SERVER_1, NTP_SERVER_2);
}

String UtomeNetwork::scanNetworksAsHtml() {
    Serial.println("[WIFI-AP] Escaneando redes 2.4GHz proximas...");
    int n = WiFi.scanNetworks(false, false);
    String html = "";

    if (n <= 0) {
        html += "<option value=\"\">Nenhuma rede encontrada</option>";
    } else {
        // Evita duplicatas de roteadores mesh ou repetidores
        String addedSsids[20];
        uint8_t addedCount = 0;

        for (int i = 0; i < n; i++) {
            if (addedCount >= 20) break;
            String ssid = WiFi.SSID(i);
            int32_t rssi = WiFi.RSSI(i);
            if (ssid.length() == 0) continue; // Pula redes ocultas sem nome

            bool duplicate = false;
            for (uint8_t j = 0; j < addedCount; j++) {
                if (addedSsids[j] == ssid) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
            addedSsids[addedCount++] = ssid;

            int quality = 2 * (rssi + 100);
            if (quality > 100) quality = 100;
            if (quality < 0) quality = 0;

            html += "<option value=\"" + ssid + "\">" + ssid + " (" + String(quality) + "%)</option>";
        }
    }
    WiFi.scanDelete(); // Libera buffers de scan da RAM do ESP32
    return html;
}

void UtomeNetwork::startConfigPortal() {
    if (_isApMode) return;

    Serial.println();
    Serial.println("==========================================================");
    Serial.println("    INICIANDO MODO PONTO DE ACESSO (PORTAL CATIVO WI-FI)  ");
    Serial.println("==========================================================");

    // 1. Define o nome exato da rede solicitado: "UTOME WIFI"
    _apSSID = WIFI_AP_SSID;

    // 2. Desativa auto-reconexão e desconecta STA para eliminar channel-hopping
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);
    _isConnected = false;

    // 3. Configura modo AP + STA mantendo o rádio acordado e estável
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_DEFAULT_TX_POWER);

    IPAddress apIP(192, 168, 4, 1);
    IPAddress netMsk(255, 255, 255, 0);
    WiFi.softAPConfig(apIP, apIP, netMsk);

    // 4. Inicia SoftAP fixado no Canal 1 com visibilidade obrigatória (ssid_hidden = 0)
    WiFi.softAP(_apSSID.c_str(), nullptr, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);

    // 5. Trava largura de banda em HT20 (20 MHz) e protocolos 802.11 b/g/n para máxima visibilidade
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    // 6. Inicia servidor DNS para Captive Portal (redireciona tudo para 192.168.4.1)
    if (_dnsServer) { delete _dnsServer; _dnsServer = nullptr; }
    _dnsServer = new DNSServer();
    _dnsServer->start(53, "*", apIP);

    // 7. Inicia WebServer
    if (_server) { delete _server; _server = nullptr; }
    _server = new WebServer(80);
    setupWebServerRoutes();
    _server->begin();

    _isApMode = true;
    _pendingApShutdown = false;
    _apStartTime = millis();

    Serial.printf("[WIFI-AP] Rede criada: %s (Canal %d, Aberta, HT20)\n", _apSSID.c_str(), WIFI_AP_CHANNEL);
    Serial.printf("[WIFI-AP] Conecte pelo smartphone e acesse: http://192.168.4.1\n");
    Serial.println("==========================================================");
    Serial.println();
}

void UtomeNetwork::setupWebServerRoutes() {
    // Rota Principal (Portal de Configuração)
    _server->on("/", HTTP_GET, [this]() {
        String scanOptions = scanNetworksAsHtml();
        String page = FPSTR(PORTAL_HTML);
        page.replace("{{NETWORKS}}", scanOptions);
        _server->send(200, "text/html", page);
    });

    // Rota de Salvamento
    _server->on("/save", HTTP_POST, [this]() {
        String newSsid = _server->arg("ssid");
        String newPass = _server->arg("password");

        if (newSsid.length() > 0) {
            saveCredentials(newSsid, newPass);
            _server->send(200, "text/html", FPSTR(SUCCESS_HTML));
            // Carência de 1.5s: permite ao navegador do celular renderizar a página
            // de confirmação completa antes de derrubar o Ponto de Acesso
            _pendingApShutdown = true;
            _configuredTime = millis();
        } else {
            _server->send(400, "text/plain", "SSID invalido!");
        }
    });

    // Rotas de Detecção de Captive Portal (Apple, Android, Windows)
    auto redirectHome = [this]() {
        _server->sendHeader("Location", "http://192.168.4.1/", true);
        _server->send(302, "text/plain", "");
    };

    _server->on("/generate_204", redirectHome);        // Android
    _server->on("/gen_204", redirectHome);
    _server->on("/hotspot-detect.html", redirectHome);  // Apple iOS / macOS
    _server->on("/ncsi.txt", redirectHome);            // Windows
    _server->onNotFound(redirectHome);
}

void UtomeNetwork::processPortal() {
    if (!_isApMode) return;

    if (_dnsServer) {
        _dnsServer->processNextRequest();
    }
    if (_server) {
        _server->handleClient();
    }

    // Se novas credenciais foram salvas, aguarda 1.5s de carência para o celular
    // receber a página SUCCESS_HTML completamente antes de desligar o AP
    if (_pendingApShutdown && (millis() - _configuredTime >= 1500)) {
        _pendingApShutdown = false;
        _justConfigured = true;
    }

    // Se o portal ficar aberto por mais de 3 minutos sem interação, desativa para economia
    if (millis() - _apStartTime > WIFI_AP_PORTAL_TIMEOUT_MS) {
        Serial.println("[WIFI-AP] Timeout do portal de configuracao atingido. Encerrando AP...");
        stopConfigPortal();
    }
}

void UtomeNetwork::stopConfigPortal() {
    if (!_isApMode) return;

    if (_server) {
        _server->stop();
        delete _server;
        _server = nullptr;
    }
    if (_dnsServer) {
        _dnsServer->stop();
        delete _dnsServer;
        _dnsServer = nullptr;
    }

    WiFi.softAPdisconnect(false);
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_DEFAULT_TX_POWER);
    WiFi.setAutoReconnect(true);
    _isApMode = false;
    _pendingApShutdown = false;
    _reconnecting = false;
    _lastReconnectAttempt = millis();
    Serial.println("[WIFI-AP] Ponto de Acesso desativado. Retornando ao modo cliente (STA).");
}

bool UtomeNetwork::checkJustConfigured() {
    if (_justConfigured) {
        _justConfigured = false;
        return true;
    }
    return false;
}

void UtomeNetwork::update(uint32_t now) {
    // 1. Se estiver em Modo AP (Portal de Configuração), processa requisições web/dns
    if (_isApMode) {
        processPortal();

        if (checkJustConfigured()) {
            Serial.println("[WIFI-AP] Novas credenciais recebidas pelo portal! Conectando ao Wi-Fi...");
            stopConfigPortal();
            delay(50);
            WiFi.mode(WIFI_STA);
            WiFi.setSleep(false);
            WiFi.setTxPower(WIFI_DEFAULT_TX_POWER);
            WiFi.setAutoReconnect(true);
            _reconnecting = true;
            _reconnectStartTime = millis();
            _lastReconnectAttempt = millis();
            _savedNetFailCount = 0;
            if (_savedPassword.length() > 0) {
                WiFi.begin(_savedSSID.c_str(), _savedPassword.c_str());
            } else {
                WiFi.begin(_savedSSID.c_str());
            }
        }
        return; // Não executa reconexão periódica padrão enquanto o AP estiver ativo
    }

    bool currentStatus = (WiFi.status() == WL_CONNECTED);

    // 2. Detecta transição de status Wi-Fi
    if (currentStatus != _isConnected) {
        _isConnected = currentStatus;
        if (_isConnected) {
            _reconnecting = false;
            _savedNetFailCount = 0;
            if (_usingCloudRoam && _cloudNetworkCount > 0) {
                uint8_t connectedIdx = (_roamIndex == 0) ? (_cloudNetworkCount - 1) : (_roamIndex - 1);
                saveCredentials(_cloudNetworks[connectedIdx].ssid, _cloudNetworks[connectedIdx].password);
                _usingCloudRoam = false;
                WiFi.setAutoReconnect(true);
            }

            Serial.println("[WIFI] Conexao estabelecida com sucesso!");
            Serial.printf("[WIFI] IP: %s | Gateway: %s | DNS DHCP: %s\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str(),
                          WiFi.dnsIP().toString().c_str());

            // Adiciona Google DNS (8.8.8.8) como servidor DNS secundário no lwIP
            // NUNCA chamamos WiFi.config() com IP local para não parar o cliente DHCP
            ip_addr_t fbDns;
            IP_ADDR4(&fbDns, 8, 8, 8, 8);
            dns_setserver(1, &fbDns);

            if (!_ntpConfigured) {
                syncNtpTime();
            }
        } else {
            Serial.println("[WIFI] Conexao Wi-Fi perdida. Operando em modo de contingencia local.");
            _timeSynced = false;
            _savedNetFailCount = 0;
            _savedNetFailWindowStart = 0;
            _usingCloudRoam = false;
            _roamIndex = 0;
            _reconnecting = false;
        }
    }

    // 3. Retentativa não-bloqueante em background se desconectado
    if (!_isConnected) {

        // 3a. JÁ escalou para cloud roaming (5 falhas na rede primária atingidas)
        //     Tenta as redes do dashboard em sequência circular de forma não-bloqueante.
        if (_usingCloudRoam) {
            if (_cloudNetworkCount > 0) {
                attemptCloudRoam(now);
            } else {
                loadCloudNetworksFromFlash();
                if (_cloudNetworkCount == 0) {
                    Serial.println("[WIFI] Nenhuma rede cloud na NVS. Voltando para rede salva.");
                    _usingCloudRoam = false;
                    _savedNetFailCount = 0;
                    _reconnecting = false;
                }
            }
        }

        // 3b. Fase primária: tenta a rede salva de forma 100% não-bloqueante
        else if (hasSavedCredentials()) {
            if (_reconnecting) {
                // Tentativa em andamento no background: aguarda até 8s para o driver conectar
                if (now - _reconnectStartTime >= 8000) {
                    _reconnecting = false;
                    _savedNetFailCount++;
                    Serial.printf("[WIFI] Rede '%s' nao respondeu em 8s (%u/5 falhas).\n",
                                  _savedSSID.c_str(), _savedNetFailCount);

                    const uint32_t WIFI_FALLBACK_FAIL_WINDOW_MS = 120000UL;
                    if (_savedNetFailCount > 0 && (now - _savedNetFailWindowStart) > WIFI_FALLBACK_FAIL_WINDOW_MS) {
                        _savedNetFailCount = 0;
                    }

                    if (_savedNetFailCount >= 5) {
                        Serial.println("[WIFI] *** 5 falhas em 2 min! Escalando para redes salvas no dashboard... ***");
                        _savedNetFailCount = 0;
                        if (_cloudNetworkCount == 0) {
                            loadCloudNetworksFromFlash();
                        }
                        if (_cloudNetworkCount > 0) {
                            _usingCloudRoam = true;
                            _roamIndex = 0;
                            _roamPending = true;
                        }
                    }
                }
            } else {
                // Intervalo entre novas tentativas
                if (now - _lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
                    _lastReconnectAttempt = now;
                    _reconnectStartTime = now;
                    _reconnecting = true;

                    if (_savedNetFailCount == 0) {
                        _savedNetFailWindowStart = now;
                    }

                    Serial.printf("[WIFI] Tentando reconectar a rede salva: %s... (tentativa %u/5)\n",
                                  _savedSSID.c_str(), _savedNetFailCount + 1);

                    WiFi.disconnect(false);
                    if (_savedPassword.length() > 0) {
                        WiFi.begin(_savedSSID.c_str(), _savedPassword.c_str());
                    } else {
                        WiFi.begin(_savedSSID.c_str());
                    }
                }
            }
        }
    }

    // 4. Validação periódica da sincronização de horário NTP
    if (_isConnected && (!_timeSynced || (now - _lastTimeCheck >= 10000))) {
        _lastTimeCheck = now;
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 10)) {
            if (!_timeSynced) {
                _timeSynced = true;
                Serial.printf("[NTP] Relogio sincronizado com sucesso: %02d:%02d:%02d (%02d/%02d/%04d)\n",
                              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                              timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            }
        }
    }
}

String UtomeNetwork::getFormattedTime() const {
    if (!_timeSynced) {
        return "--:--";
    }
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        return String(buf);
    }
    return "--:--";
}

String UtomeNetwork::getFormattedDate() const {
    if (!_timeSynced) {
        return "--/--/----";
    }
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        return String(buf);
    }
    return "--/--/----";
}

uint8_t UtomeNetwork::getDayOfWeek() const {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return (uint8_t)timeinfo.tm_wday; // 0 = Domingo, 1 = Segunda ... 6 = Sabado
    }
    return 0;
}

int UtomeNetwork::getHours() const {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return timeinfo.tm_hour;
    }
    return -1;
}

int UtomeNetwork::getMinutes() const {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return timeinfo.tm_min;
    }
    return -1;
}

int UtomeNetwork::getSeconds() const {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        return timeinfo.tm_sec;
    }
    return -1;
}

int32_t UtomeNetwork::getRssi() const {
    if (_isConnected) {
        return WiFi.RSSI();
    }
    return 0;
}

String UtomeNetwork::getLocalIP() const {
    if (_isConnected) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

void UtomeNetwork::forceReconnect() {
    Serial.println("[WIFI] Forcando reconexao imediata...");
    WiFi.disconnect();
    WiFi.reconnect();
    _lastReconnectAttempt = millis();
}

// =============================================================================
// GERENCIAMENTO DE MÚLTIPLAS REDES CLOUD (Dashboard → Firestore → Robô)
// =============================================================================

void UtomeNetwork::setCloudNetworks(const WifiCredential* networks, uint8_t count) {
    uint8_t n = (count > WIFI_MAX_CLOUD_NETWORKS) ? WIFI_MAX_CLOUD_NETWORKS : count;
    for (uint8_t i = 0; i < n; i++) {
        _cloudNetworks[i] = networks[i];
    }
    _cloudNetworkCount = n;
    _roamIndex = 0; // Reinicia a sequência a partir da rede principal

    Serial.printf("[WIFI-CLOUD] %u rede(s) Wi-Fi recebidas do Firestore:\n", n);
    for (uint8_t i = 0; i < n; i++) {
        Serial.printf("  [%u] SSID: '%s' (prioridade %u)\n",
                      i, _cloudNetworks[i].ssid.c_str(), _cloudNetworks[i].priority);
    }
}

void UtomeNetwork::triggerCloudRoam() {
    _roamPending = true;
    _lastRoamAttempt = 0; // Zera o timer para tentar imediatamente no próximo update()
    // Só ativa o cloud roaming se há redes disponíveis
    if (_cloudNetworkCount > 0) {
        _usingCloudRoam = true;
    }
    Serial.println("[WIFI-CLOUD] Roaming imediato solicitado.");
}

// =============================================================================
// Carrega redes cloud da NVS local (persitidas pelo FirebaseSync).
// Chamado no setup() quando o robô inicia sem conexão Wi-Fi, antes de conseguir
// contatar o Firebase — garante que as 10 redes do dashboard sejam usadas como
// fallback de roaming imediato, mesmo sem internet no boot.
// =============================================================================
void UtomeNetwork::loadCloudNetworksFromFlash() {
    Preferences wifiPrefs;
    wifiPrefs.begin("utome_wifi_c", true); // somente leitura
    uint8_t count = wifiPrefs.getUChar("count", 0);
    if (count > WIFI_MAX_CLOUD_NETWORKS) count = WIFI_MAX_CLOUD_NETWORKS;

    if (count == 0) {
        wifiPrefs.end();
        Serial.println("[WIFI-CLOUD] Nenhuma rede cloud na NVS para roaming offline.");
        return;
    }

    for (uint8_t i = 0; i < count; i++) {
        String keyS  = "ssid" + String(i);
        String keyP  = "pass" + String(i);
        String keyPr = "prio" + String(i);
        _cloudNetworks[i].ssid     = wifiPrefs.getString(keyS.c_str(),  "");
        _cloudNetworks[i].password = wifiPrefs.getString(keyP.c_str(),  "");
        _cloudNetworks[i].priority = wifiPrefs.getUChar(keyPr.c_str(),   i);
    }
    wifiPrefs.end();

    _cloudNetworkCount = count;
    _roamIndex = 0;
    _roamPending = true; // Dispara tentativa imediata no próximo update()

    Serial.printf("[WIFI-CLOUD] %u rede(s) carregadas da NVS para roaming offline:\n", count);
    for (uint8_t i = 0; i < count; i++) {
        Serial.printf("  [%u] SSID: '%s' (prioridade %u)\n",
                      i, _cloudNetworks[i].ssid.c_str(), _cloudNetworks[i].priority);
    }
}

void UtomeNetwork::attemptCloudRoam(uint32_t now) {
    const uint32_t ROAM_INTERVAL_MS = 12000;

    if (_cloudNetworkCount == 0) return;

    if (_reconnecting) {
        // Tentativa de roaming em andamento no background
        if (now - _reconnectStartTime >= 8000) {
            _reconnecting = false;
            uint8_t prevIdx = (_roamIndex == 0) ? (_cloudNetworkCount - 1) : (_roamIndex - 1);
            Serial.printf("[WIFI-CLOUD] Falhou em '%s'. Tentando proxima rede do dashboard em breve.\n",
                          _cloudNetworks[prevIdx].ssid.c_str());
        }
        return;
    }

    if (!_roamPending && (now - _lastRoamAttempt < ROAM_INTERVAL_MS)) {
        return; // Aguarda o intervalo antes da próxima tentativa
    }
    _roamPending = false;
    _lastRoamAttempt = now;
    _reconnectStartTime = now;
    _reconnecting = true;

    // Percorre as redes em sequência circular a partir do índice atual
    WifiCredential& net = _cloudNetworks[_roamIndex];

    Serial.printf("[WIFI-CLOUD] Tentando conectar em background: '%s' (rede %u/%u)...\n",
                  net.ssid.c_str(), _roamIndex + 1, _cloudNetworkCount);

    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false);
    WiFi.setTxPower(WIFI_DEFAULT_TX_POWER);

    if (net.password.length() > 0) {
        WiFi.begin(net.ssid.c_str(), net.password.c_str());
    } else {
        WiFi.begin(net.ssid.c_str()); // Rede aberta
    }

    // Avança para a próxima rede na sequência circular
    _roamIndex = (_roamIndex + 1) % _cloudNetworkCount;
}

void UtomeNetwork::printNetworkStatus() const {
    Serial.println();
    Serial.println("------------- STATUS DE REDE & HORARIO -------------");
    Serial.printf(" Wi-Fi Status:      %s\n", _isConnected ? "CONECTADO" : (_isApMode ? "MODO PONTO DE ACESSO (AP)" : "DESCONECTADO"));
    if (_isApMode) {
        Serial.printf(" Rede Criada:       %s (Aberta)\n", _apSSID.c_str());
        Serial.printf(" IP do Portal:      %s\n", getApIP().c_str());
    } else if (_isConnected) {
        Serial.printf(" SSID Conectado:    %s\n", WiFi.SSID().c_str());
        Serial.printf(" Endereco IP:       %s\n", WiFi.localIP().toString().c_str());
        Serial.printf(" Sinal (RSSI):      %d dBm\n", WiFi.RSSI());
    } else {
        Serial.printf(" SSID Salvo:        %s\n", _savedSSID.c_str());
        Serial.printf(" Falhas Salvo:      %u/5 consecutivas (Janela 2 min)\n", _savedNetFailCount);
    }
    Serial.printf(" Redes Cloud (Site):%u rede(s) cadastrada(s) para roaming\n", _cloudNetworkCount);
    for (uint8_t i = 0; i < _cloudNetworkCount; i++) {
        bool isActive = (_isConnected && WiFi.SSID() == _cloudNetworks[i].ssid);
        Serial.printf("   [%u] Prioridade %u: '%s'%s\n",
                      i + 1,
                      _cloudNetworks[i].priority,
                      _cloudNetworks[i].ssid.c_str(),
                      isActive ? " [CONECTADO ATUALMENTE]" : "");
    }
    Serial.printf(" Modo Roaming:      %s\n", _usingCloudRoam ? "ATIVO (Alternando entre redes do site)" : "STANDBY (Rede primaria preferencial)");
    Serial.printf(" Relogio NTP:       %s\n", _timeSynced ? "SINCRONIZADO" : "PENDENTE / DESCONECTADO");
    if (_timeSynced) {
        Serial.printf(" Horario Atual:     %s (Fuso UTC-3)\n", getFormattedTime().c_str());
        Serial.printf(" Data Atual:        %s\n", getFormattedDate().c_str());
        const char* dias[] = {"Domingo", "Segunda-feira", "Terca-feira", "Quarta-feira", "Quinta-feira", "Sexta-feira", "Sabado"};
        Serial.printf(" Dia da Semana:     %s\n", dias[getDayOfWeek()]);
    }
    Serial.println("----------------------------------------------------");
    Serial.println();
}
