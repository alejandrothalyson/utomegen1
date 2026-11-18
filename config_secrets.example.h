// ============================================================================
// config_secrets.h — ARQUIVO DE SEGREDOS DO FIRMWARE UTOME 1
//
// ⚠️  ESTE ARQUIVO *NUNCA* DEVE SER COMMITADO NO GIT!
//     Ele está listado no .gitignore por segurança.
//     Use config_secrets.example.h como template para novos desenvolvedores.
//
// Preencha com suas credenciais reais antes de compilar o firmware.
// ============================================================================

#ifndef CONFIG_SECRETS_H
#define CONFIG_SECRETS_H

// --- Rede Wi-Fi 2.4GHz que o robô usará no seu ambiente de desenvolvimento ---
// (Em produção, as credenciais são salvas via Portal Cativo — não é necessário
//  preencher WIFI_SSID/WIFI_PASSWORD para dispositivos que usarão o portal)
#define WIFI_SSID       "sua_rede_wifi"
#define WIFI_PASSWORD   "sua_senha_wifi"

// --- Firebase Web API Key (Identity Toolkit + Firestore REST) ---
// Obtenha em: Firebase Console > Configurações do Projeto > Chave de API da Web
#define FIREBASE_API_KEY "AIzaSy_SUA_CHAVE_AQUI"

#endif // CONFIG_SECRETS_H
