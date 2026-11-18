#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>
#include <lwip/dns.h>
#include "config.h"

// ============================================================================
// UTOME 1 - GERENCIADOR DE REDE WI-FI, PORTAL CATIVO E HORÁRIO NTP
// ============================================================================

// Limite de redes salvas (espelho do limite do Firestore no dashboard)
#define WIFI_MAX_CLOUD_NETWORKS 10

// Estrutura de uma credencial Wi-Fi vinda do Firestore ou da NVS local
struct WifiCredential {
    String  ssid;
    String  password;
    uint8_t priority; // 0 = principal, 1..9 = backup
};

class UtomeNetwork {
public:
    UtomeNetwork();

    // Inicialização do Wi-Fi (lê da NVS permanente ou usa contingência de fábrica)
    void init();

    // Processamento periódico não-bloqueante (reconectividade, portal cativo e NTP)
    void update(uint32_t now);

    // Configuração de NTP com servidores e fuso horário
    void syncNtpTime();

    // Status da conexão
    bool isConnected()  const { return _isConnected; }
    bool isTimeSynced() const { return _timeSynced;   }
    bool isApMode()     const { return _isApMode;      }

    // Métodos do Portal Cativo / Wi-Fi Manager
    void   startConfigPortal();
    void   stopConfigPortal();
    void   processPortal();
    bool   isPortalActive()  const { return _isApMode; }
    String getApSSID()       const { return _apSSID;   }
    String getApIP()         const { return "192.168.4.1"; }
    bool   checkJustConfigured();

    // Credenciais salvas na Flash permanente (NVS) — rede principal/manual
    bool   hasSavedCredentials() const;
    String getSavedSSID() const { return _savedSSID; }
    void   saveCredentials(const String& ssid, const String& password);
    void   clearSavedCredentials();

    // -------------------------------------------------------------------------
    // Gerenciamento de múltiplas redes vindas do Firestore (dashboard do usuário)
    // -------------------------------------------------------------------------

    // Injeta a lista de redes baixadas do Firestore (chamado pelo FirebaseSync)
    void setCloudNetworks(const WifiCredential* networks, uint8_t count);

    // Retorna quantas redes cloud estão carregadas
    uint8_t getCloudNetworkCount() const { return _cloudNetworkCount; }

    // Força roaming imediato entre redes cloud (chamado quando há novas redes)
    void triggerCloudRoam();

    // Carrega redes cloud persistidas na NVS (usado no boot offline, antes do Firebase)
    void loadCloudNetworksFromFlash();

    // Métodos utilitários de formatação de data e hora
    String  getFormattedTime() const;
    String  getFormattedDate() const;
    uint8_t getDayOfWeek()     const; // 0 = Domingo, 1 = Segunda ... 6 = Sábado
    int     getHours()         const;
    int     getMinutes()       const;
    int     getSeconds()       const;

    // Métricas de rede
    int32_t getRssi()    const;
    String  getLocalIP() const;
    void    forceReconnect();

    // Diagnóstico serial
    void printNetworkStatus() const;

private:
    bool     _isConnected;
    bool     _timeSynced;
    uint32_t _lastReconnectAttempt;
    uint32_t _reconnectStartTime;
    bool     _reconnecting;
    uint32_t _lastTimeCheck;
    bool     _ntpConfigured;

    // Estado e serviços do Portal Cativo
    bool     _isApMode;
    bool     _justConfigured;
    bool     _pendingApShutdown;
    uint32_t _configuredTime;
    uint32_t _apStartTime;
    String   _apSSID;
    String   _savedSSID;
    String   _savedPassword;

    // -------------------------------------------------------------------------
    // Roaming automático entre múltiplas redes cloud
    // -------------------------------------------------------------------------
    WifiCredential _cloudNetworks[WIFI_MAX_CLOUD_NETWORKS];
    uint8_t        _cloudNetworkCount; // número de redes carregadas do Firestore
    uint8_t        _roamIndex;         // próxima rede a tentar na sequência circular
    uint32_t       _lastRoamAttempt;   // timestamp da última tentativa de roaming
    bool           _roamPending;       // roaming explicitamente solicitado

    // Contador de falhas consecutivas na rede salva (fallback → cloud roaming)
    // Após WIFI_FALLBACK_FAIL_LIMIT falhas em WIFI_FALLBACK_FAIL_WINDOW_MS,
    // o robô abandona a rede salva e inicia o cloud roaming imediatamente.
    uint8_t        _savedNetFailCount;       // quantas tentativas falharam consecutivamente
    uint32_t       _savedNetFailWindowStart; // início da janela de 2 minutos
    bool           _usingCloudRoam;          // true = escalou para redes do dashboard

    // Tenta conectar a próxima rede cloud na sequência circular
    void attemptCloudRoam(uint32_t now);

    WebServer* _server;
    DNSServer* _dnsServer;

    void   loadCredentialsFromFlash();
    void   setupWebServerRoutes();
    String buildPortalHtml(const String& scannedOptions);
    String scanNetworksAsHtml();
};

#endif // NETWORK_MANAGER_H
