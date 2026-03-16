// ============================================================================
// WiFi Manager — Cyclic connection state machine with NVS persistence
// ============================================================================

#include "wifi_manager.h"
#include "wifi_credentials.h"
#include "led_status.h"
#include "log_buffer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

namespace wifi_manager
{

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// Try to connect to a given SSID/password within timeoutMs milliseconds.
/// Returns true if connected, false on timeout.
static bool tryConnect(const char* ssid, const char* password, uint32_t timeoutMs)
{
    if (ssid == nullptr || strlen(ssid) == 0) {
        Serial.println("[WiFi] SSID is empty, cannot connect.");
        return false;
    }

    Serial.printf("[WiFi] Connecting to \"%s\"...\n", ssid);
    WiFi.disconnect(true);
    vTaskDelay(pdMS_TO_TICKS(200));

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);
    WiFi.begin(ssid, password);

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) {
            WiFi.disconnect(true);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
    }
    Serial.println();
    return true;
}

/// Scan visible networks and print a numbered list to Serial.
static void scanAndPrintSSIDs()
{
    Serial.println("\n[WiFi] Scanning for available networks...");

    // Switch to station mode for scan (stays there for connection later)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    int n = WiFi.scanNetworks();

    if (n <= 0) {
        Serial.println("[WiFi] No networks found.");
        return;
    }

    Serial.printf("[WiFi] %d network(s) found:\n", n);
    Serial.println("--------------------------------------------");
    for (int i = 0; i < n; i++) {
        Serial.printf("  [%2d] %-32s  RSSI: %4d dBm  %s\n",
                      i + 1,
                      WiFi.SSID(i).c_str(),
                      WiFi.RSSI(i),
                      (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "(open)" : "(secured)");
    }
    Serial.println("--------------------------------------------");

    // Free scan results
    WiFi.scanDelete();
}

/// Discard any bytes currently sitting in the Serial RX buffer.
/// Call this before prompting the user to avoid consuming buffered garbage
/// (e.g. bytes received while the network scan was running).
static void flushSerialRx()
{
    // Give the UART a moment to finish receiving any in-flight bytes
    vTaskDelay(pdMS_TO_TICKS(100));
    while (Serial.available()) {
        Serial.read();
    }
}

/// Block until the user types a line and presses Enter on Serial.
/// - Handles Backspace (0x08) and DEL (0x7F): erases last char with visual echo.
/// - Silently discards ANSI/VT escape sequences (e.g. ESC[3~ from Delete key,
///   arrow keys, etc.) so they never pollute the result string.
/// - Strips trailing \r\n.
static String readLineFromSerial(const char* prompt)
{
    Serial.print(prompt);
    Serial.flush();

    String result = "";
    bool inEscape = false;   // true after receiving ESC (0x1B)
    bool inCSI    = false;   // true after receiving ESC + '['

    while (true) {
        if (!Serial.available()) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        uint8_t c = (uint8_t)Serial.read();

        // ---- ANSI escape sequence filtering ----
        if (inCSI) {
            // Consume bytes until we hit the final byte (0x40–0x7E)
            if (c >= 0x40 && c <= 0x7E) {
                inCSI    = false;
                inEscape = false;
            }
            continue; // discard
        }
        if (inEscape) {
            if (c == '[') {
                inCSI = true;   // CSI sequence — keep consuming
            } else {
                inEscape = false; // single-char escape — discard and move on
            }
            continue;
        }
        if (c == 0x1B) {  // ESC
            inEscape = true;
            continue;
        }

        // ---- Normal input handling ----
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;
        }
        if (c == 0x08 || c == 0x7F) {  // Backspace or DEL
            if (result.length() > 0) {
                result.remove(result.length() - 1);
                Serial.print("\b \b");  // erase last char on terminal
            }
            continue;
        }

        // Regular printable character
        result += (char)c;
        Serial.print((char)c);  // echo
    }

    Serial.println();
    return result;
}

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------

static constexpr const char* NVS_NAMESPACE = "wifi_creds";
static constexpr const char* NVS_KEY_SSID  = "ssid";
static constexpr const char* NVS_KEY_PASS  = "password";

/// Load credentials from NVS. Falls back to compile-time defaults if absent.
static void loadCredentials(String& ssid, String& password)
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);

    String savedSSID = prefs.getString(NVS_KEY_SSID, "");
    String savedPass = prefs.getString(NVS_KEY_PASS, "");
    prefs.end();

    if (savedSSID.length() > 0) {
        ssid     = savedSSID;
        password = savedPass;
        Serial.printf("[WiFi] Loaded credentials from NVS — SSID: \"%s\"\n", ssid.c_str());
    } else {
        // Nothing saved yet — use compile-time defaults
        ssid     = String(WIFI_SSID);
        password = String(WIFI_PASSWORD);
        if (ssid.length() > 0) {
            Serial.printf("[WiFi] Using compiled-in credentials (secrets.h) — SSID: \"%s\"\n", ssid.c_str());
        } else {
            Serial.println("[WiFi] No credentials found (NVS empty, no SSID in secrets.h).");
        }
    }
}

/// Persist credentials to NVS so they survive reboots.
static void saveCredentials(const String& ssid, const String& password)
{
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
    prefs.putString(NVS_KEY_SSID, ssid);
    prefs.putString(NVS_KEY_PASS, password);
    prefs.end();
    Serial.printf("[WiFi] Credentials saved to NVS — SSID: \"%s\"\n", ssid.c_str());
    LOG_INFO("WiFi credentials saved to NVS — SSID: %s", ssid.c_str());
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void connectWithFallback()
{
    led_status::setState(led_status::State::WIFI_CONNECTING);

    String currentSSID;
    String currentPassword;

    // Load from NVS (or fall back to compile-time defaults)
    loadCredentials(currentSSID, currentPassword);

    // Track whether the current credentials were entered by the user via Serial
    // so we only write to NVS when there is something new to persist.
    bool credentialsAreNew = false;

    while (true) {
        // ---- Phase 1: try up to MAX_RETRIES times ----
        bool connected = false;
        for (uint8_t attempt = 1; attempt <= MAX_RETRIES; attempt++) {
            Serial.printf("\n[WiFi] Attempt %d/%d — SSID: \"%s\"\n",
                          attempt, MAX_RETRIES, currentSSID.c_str());

            if (tryConnect(currentSSID.c_str(), currentPassword.c_str(), ATTEMPT_TIMEOUT_MS)) {
                connected = true;
                break;
            }

            Serial.printf("[WiFi] Attempt %d failed.\n", attempt);
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (connected) {
            // Save to NVS only when the user entered new credentials
            if (credentialsAreNew) {
                saveCredentials(currentSSID, currentPassword);
            }
            break; // exit — we are connected
        }

        // ---- Phase 2: all attempts failed, ask user via Serial ----
        led_status::setState(led_status::State::WIFI_WAITING_USER);

        Serial.println();
        Serial.println("============================================");
        Serial.println("  [WiFi] All connection attempts failed.");
        Serial.println("============================================");

        scanAndPrintSSIDs();

        // Keep asking until the user provides a valid SSID
        while (true) {
            Serial.println();
            Serial.println("  Enter new credentials and press Enter.");
            flushSerialRx();
            currentSSID     = readLineFromSerial("  SSID     : ");
            currentPassword = readLineFromSerial("  Password : ");

            currentSSID.trim();

            if (currentSSID.length() == 0) {
                Serial.println("  [!] SSID cannot be empty, try again.");
                continue;
            }
            if (currentSSID.length() > 32) {
                Serial.println("  [!] SSID too long (max 32 chars), try again.");
                continue;
            }
            break;
        }

        credentialsAreNew = true;
        Serial.printf("\n  -> Connecting to \"%s\"...\n", currentSSID.c_str());
        led_status::setState(led_status::State::WIFI_CONNECTING);
    }

    // Connected!
    led_status::setState(led_status::State::STANDBY);
    LOG_INFO("WiFi connected.");
    LOG_INFO("IP address: %s", WiFi.localIP().toString().c_str());
    LOG_INFO("Hostname: %s.local", WIFI_HOSTNAME);

    Serial.println("\n[WiFi] Connected!");
    Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] Hostname: http://%s.local/\n", WIFI_HOSTNAME);
}

} // namespace wifi_manager
