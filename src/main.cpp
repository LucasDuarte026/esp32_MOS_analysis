#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>  // Fix #4: Proper JSON parsing library
#include <sys/time.h>
#include <time.h>
#include "version.h"

#include "wifi_credentials.h"
#include "web_ui.h"
#include "mosfet_controller.h"
#include "monitoring_task.h"
#include "log_buffer.h"
#include "led_status.h"

#include "file_manager.h"
#include <FFat.h>

// FreeRTOS headers must come after Arduino.h on ESP32
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
}

namespace
{
constexpr uint8_t LED_PIN = 2;   // LED on-board
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
constexpr uint32_t LED_TOGGLE_MS = 500;

WebServer server(80);
MOSFETController mosfet_controller;

// Fix #8: CORS headers helper
void setCORSHeaders()
{
  // In production, replace "*" with your specific domain
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.sendHeader("Access-Control-Max-Age", "86400");
}

void handleCORS()
{
  setCORSHeaders();
  server.send(204);
}

void handleRoot()
{
  LOG_INFO("HTTP GET / from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  webui::sendIndex(server);
}

void handleVisualization()
{
  LOG_INFO("HTTP GET /visualization from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  webui::sendVisualization(server);
}

void handleEmail()
{
  LOG_INFO("HTTP GET /email from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  webui::sendEmail(server);
}

void handleCSS()
{
  LOG_DEBUG("HTTP GET /dashboard.css from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  webui::sendCSS(server);
}

void handleJS()
{
  LOG_DEBUG("HTTP GET /dashboard.js from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  webui::sendJS(server);
}

void handleStatus()
{
  LOG_DEBUG("HTTP GET /api/status from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  String json = "{\"status\":\"ready\",\"device\":\"ESP32-MOSFET\"}";
  server.send(200, "application/json", json);
}

void handleStartMeasurement()
{
  LOG_INFO("HTTP POST /api/start from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  
  // Parse parameters from POST body
  if (!server.hasArg("plain")) {
    LOG_ERROR("Missing request body");
    server.send(400, "application/json", "{\"error\":\"missing_body\"}");
    return;
  }
  
  String body = server.arg("plain");
  LOG_DEBUG("Request body: %s", body.c_str());
  
  // Fix #4: Use ArduinoJson for proper JSON parsing
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    LOG_ERROR("JSON parse error: %s", error.c_str());
    server.send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }

  // Update system time if timestamp provided
  if (doc.containsKey("timestamp")) {
      unsigned long ts = doc["timestamp"];
      if (ts > 1600000000) { // Sanity check (~2020+)
          struct timeval tv;
          tv.tv_sec = ts;
          tv.tv_usec = 0;
          settimeofday(&tv, NULL);
          LOG_INFO("System time synchronized to: %lu", ts);
      }
  }
  
  SweepConfig config;
  config.vgs_start = doc["vgs_start"] | 0.0f;
  config.vgs_end = doc["vgs_end"] | 3.5f;
  config.vgs_step = doc["vgs_step"] | 0.05f;
  config.rshunt = doc["rshunt"] | 100.0f;
  config.settling_ms = doc["settling_ms"] | 5;
  
  // Get filename with default
  const char* fname = doc["filename"] | "mosfet_data";
  config.filename = String(fname);
  
  // VDS parameters
  config.vds_start = doc["vds_start"] | 0.0f;
  config.vds_end = doc["vds_end"] | 5.0f;
  config.vds_step = doc["vds_step"] | 0.05f;
  
  // Validate ranges
  if (config.vgs_start < 0 || config.vgs_end > 5.0) {
    LOG_ERROR("Invalid VGS range");
    server.send(400, "application/json", "{\"error\":\"invalid_vgs_range\"}");
    return;
  }
  
  if (config.rshunt <= 0) {
    LOG_ERROR("Invalid Rshunt value");
    server.send(400, "application/json", "{\"error\":\"invalid_rshunt\"}");
    return;
  }
  
  LOG_INFO("Parsed config: VGS %.2f-%.2f step %.3f, VDS %.2f-%.2f step %.3f",
           config.vgs_start, config.vgs_end, config.vgs_step,
           config.vds_start, config.vds_end, config.vds_step);
  LOG_INFO("  Rshunt=%.1f ohm, Settling=%dms, File=%s",
           config.rshunt, config.settling_ms, config.filename.c_str());
  
  // Async Start
  bool success = mosfet_controller.startMeasurementAsync(config);
  
  if (success) {
    String json = "{\"status\":\"started\",";
    json += "\"filename\":\"" + config.filename + "\"}";
      
    server.send(202, "application/json", json);
  } else {
    LOG_ERROR("Failed to start measurement");
    server.send(500, "application/json", "{\"error\":\"start_failed\"}");
  }
}

void handleCancelMeasurement()
{
  LOG_INFO("HTTP POST /api/cancel from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  
  mosfet_controller.cancelMeasurement();
  server.send(200, "application/json", "{\"status\":\"cancelled\"}");
}

void handleGetProgress()
{
  setCORSHeaders();
  auto progress = mosfet_controller.getProgress();
  
  String json = "{";
  json += "\"running\":" + String(progress.is_running ? "true" : "false") + ",";
  json += "\"progress\":" + String(progress.progress_percent) + ",";
  json += "\"vds\":" + String(progress.current_vds, 3) + ",";
  json += "\"message\":\"" + progress.message + "\",";
  json += "\"error\":" + String(progress.has_error ? "true" : "false") + ",";
  json += "\"error_msg\":\"" + progress.error_message + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleGetData()
{
  LOG_DEBUG("HTTP GET /api/data (Deprecated)");
  setCORSHeaders();
  server.send(200, "application/json", "{\"status\":\"use_download_api\"}");
}

void handleTemperature()
{
  setCORSHeaders();
  monitoring::SystemStatus status = monitoring::getStatus();
  String json = "{\"temperature\":" + String(status.temperature_celsius, 1) + ",\"unit\":\"C\"}";
  server.send(200, "application/json", json);
}

void handleUSBStatus()
{
  setCORSHeaders();
  monitoring::SystemStatus status = monitoring::getStatus();
  String json = "{\"usb_connected\":" + String(status.usb_connected ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleSystemInfo()
{
  setCORSHeaders();
  monitoring::SystemStatus status = monitoring::getStatus();
  char chipId[17];
  snprintf(chipId, sizeof(chipId), "%016llX", status.chip_id);
  
  String json = "{";
  json += "\"chip_id\":\"" + String(chipId) + "\",";
  json += "\"version\":\"" + String(SOFTWARE_VERSION) + "\",";
  json += "\"temperature\":" + String(status.temperature_celsius, 1) + ",";
  json += "\"usb_connected\":" + String(status.usb_connected ? "true" : "false") + ",";
  json += "\"free_heap\":" + String(status.free_heap) + ",";
  json += "\"debug_mode\":" + String(isDebugModeEnabled() ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleNotFound()
{
  LOG_WARN("HTTP 404: %s %s from %s", 
           server.method() == HTTP_GET ? "GET" : "POST",
           server.uri().c_str(),
           server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}

void handleDownloadCSV()
{
  LOG_WARN("HTTP GET /api/download_csv (Deprecated) - Use /api/files/download");
  setCORSHeaders();
  server.send(400, "text/plain", "Please use the File Manager tab to download specific measurements.");
}

void handleGetLogs()
{
  setCORSHeaders();
  String json = g_log_buffer.getLogsJSON();
  server.send(200, "application/json", json);
}

void handleClearLogs()
{
  LOG_INFO("HTTP POST /api/logs/clear from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  g_log_buffer.clear();
  server.send(200, "application/json", "{\"status\":\"logs_cleared\"}");
}

void handleListFiles()
{
  LOG_DEBUG("HTTP GET /api/files from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  
  auto files = FileManager::listFiles();
  int count = files.size();
  
  String json = "{\"files\":[";
  for (size_t i = 0; i < files.size(); i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"" + files[i].name + "\",";
    json += "\"size\":" + String(files[i].size) + ",";
    json += "\"timestamp\":" + String(files[i].timestamp) + "}";
  }
  json += "],\"count\":" + String(count) + ",";
  json += "\"warning\":" + String(count >= FileManager::WARNING_THRESHOLD ? "true" : "false") + "}";
  
  server.send(200, "application/json", json);
}

void handleDownloadFile()
{
  setCORSHeaders();
  
  if (!server.hasArg("file")) {
    server.send(400, "text/plain", "Missing file parameter");
    return;
  }
  
  String filename = server.arg("file");
  
  // Fix #7: Validate filename using FileManager validator
  if (!FileManager::isValidFilename(filename)) {
    LOG_WARN("Invalid filename in download request: %s", filename.c_str());
    server.send(400, "text/plain", "Invalid filename");
    return;
  }
  
  LOG_INFO("HTTP GET /api/files/download?file=%s from %s", 
           filename.c_str(), server.client().remoteIP().toString().c_str());
  
  // Use streaming to avoid memory exhaustion on large files
  FileManager::streamFileToWeb(server, filename);
}

void handleDeleteFile()
{
  setCORSHeaders();
  
  if (!server.hasArg("file")) {
    server.send(400, "application/json", "{\"error\":\"missing_file\"}");
    return;
  }
  
  String filename = server.arg("file");
  
  // Fix #7: Validate filename using FileManager validator
  if (!FileManager::isValidFilename(filename)) {
    LOG_WARN("Invalid filename in delete request: %s", filename.c_str());
    server.send(400, "application/json", "{\"error\":\"invalid_filename\"}");
    return;
  }
  
  LOG_INFO("HTTP POST /api/files/delete?file=%s from %s",
           filename.c_str(), server.client().remoteIP().toString().c_str());
  
  bool success = FileManager::deleteFile(filename);
  
  if (success) {
    int count = FileManager::countFiles();
    String json = "{\"success\":true,\"count\":" + String(count) + "}";
    server.send(200, "application/json", json);
  } else {
    server.send(500, "application/json", "{\"error\":\"delete_failed\"}");
  }
}

void handleDeleteAllFiles()
{
  LOG_INFO("HTTP POST /api/files/delete-all from %s", server.client().remoteIP().toString().c_str());
  setCORSHeaders();
  
  auto files = FileManager::listFiles();
  int deleted = 0;
  int failed = 0;
  
  for (const auto& f : files) {
    if (FileManager::deleteFile(f.name)) {
      deleted++;
    } else {
      failed++;
    }
  }
  
  LOG_INFO("Deleted %d files, %d failed", deleted, failed);
  
  size_t freeBytes = FFat.freeBytes();
  size_t totalBytes = FFat.totalBytes();
  
  String json = "{\"deleted\":" + String(deleted) + ",";
  json += "\"failed\":" + String(failed) + ",";
  json += "\"free_bytes\":" + String(freeBytes) + ",";
  json += "\"total_bytes\":" + String(totalBytes) + "}";
  
  server.send(200, "application/json", json);
}

void handleStorageInfo()
{
  setCORSHeaders();
  
  size_t totalBytes = FFat.totalBytes();
  size_t freeBytes = FFat.freeBytes();
  size_t usedBytes = totalBytes - freeBytes;
  int usedPercent = totalBytes > 0 ? (usedBytes * 100) / totalBytes : 0;
  int fileCount = FileManager::countFiles();
  
  String json = "{";
  json += "\"total_bytes\":" + String(totalBytes) + ",";
  json += "\"free_bytes\":" + String(freeBytes) + ",";
  json += "\"used_bytes\":" + String(usedBytes) + ",";
  json += "\"used_percent\":" + String(usedPercent) + ",";
  json += "\"file_count\":" + String(fileCount) + "}";
  
  server.send(200, "application/json", json);
}

void connectToWifi()
{
  LOG_INFO("Connecting to WiFi: %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Set LED to WiFi disconnected pattern during connection attempt
  led_status::setState(led_status::State::WIFI_DISCONNECTED);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    // Fix #11: Use vTaskDelay instead of blocking delay
    vTaskDelay(pdMS_TO_TICKS(250));

    if (millis() - start > WIFI_TIMEOUT_MS)
    {
      LOG_ERROR("WiFi connection timeout after %d ms", WIFI_TIMEOUT_MS);
      LOG_ERROR("Restarting ESP32 in 5 seconds...");
      // Fix #11: Use vTaskDelay instead of blocking delay
      vTaskDelay(pdMS_TO_TICKS(5000));
      ESP.restart();
    }
  }

  digitalWrite(LED_PIN, HIGH);
  // WiFi connected - set LED to standby
  led_status::setState(led_status::State::STANDBY);
  
  LOG_INFO("WiFi connected!");
  LOG_INFO("IP Address: %s", WiFi.localIP().toString().c_str());
  LOG_INFO("Hostname: %s.local", WIFI_HOSTNAME);
}

void ledTask(void * /*param*/)
{
  pinMode(LED_PIN, OUTPUT);
  while (true)
  {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    vTaskDelay(pdMS_TO_TICKS(LED_TOGGLE_MS));
  }
}

void webServerTask(void * /*param*/)
{
  while (true)
  {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
} // namespace

void setup()
{
  Serial.begin(115200);
  delay(100);
  initAsyncLogging(); // Initialize Async Serial Task
  initDebugModePin(); // Initialize GPIO12 for debug mode control
  
  // Initialize file system
  if (!FileManager::init()) {
    LOG_ERROR("File system initialization failed");
  }
  
  pinMode(LED_PIN, OUTPUT);

  // Inicializar controlador MOSFET
  mosfet_controller.begin();

  connectToWifi();

  // Inicializar mDNS
  if (MDNS.begin(WIFI_HOSTNAME))
  {
    Serial.println("mDNS iniciado com sucesso!");
    Serial.printf("Você pode acessar o dispositivo em: http://%s.local/\n", WIFI_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }
  else
  {
    Serial.println("Erro ao iniciar mDNS");
  }

  // Inicializar sistema de monitoramento
  monitoring::begin();
  LOG_INFO("Monitoring system started");
  
  // Inicializar sistema de LED externo (GPIO14)
  led_status::init();

  // Configurar rotas do servidor web
  LOG_INFO("Configuring web server routes");
  
  // Fix #8: Add CORS preflight handlers
  server.on("/api/start", HTTP_OPTIONS, handleCORS);
  server.on("/api/files/delete", HTTP_OPTIONS, handleCORS);
  server.on("/api/logs/clear", HTTP_OPTIONS, handleCORS);
  
  server.on("/api/logs/clear", HTTP_OPTIONS, handleCORS);
  
  server.on("/", handleRoot);
  server.on("/visualization", handleVisualization);
  server.on("/email", handleEmail);
  server.on("/dashboard.css", handleCSS);
  server.on("/dashboard.js", handleJS);
  server.on("/api/status", handleStatus);
  server.on("/api/temperature", handleTemperature);
  server.on("/api/usb_status", handleUSBStatus);
  server.on("/api/system_info", handleSystemInfo);
  server.on("/api/start", HTTP_POST, handleStartMeasurement);
  server.on("/api/cancel", HTTP_POST, handleCancelMeasurement);
  server.on("/api/cancel", HTTP_OPTIONS, handleCORS);
  server.on("/api/progress", HTTP_GET, handleGetProgress);
  server.on("/api/progress", HTTP_OPTIONS, handleCORS);
  server.on("/api/data", handleGetData);
  server.on("/api/download_csv", handleDownloadCSV);
  server.on("/api/logs", HTTP_GET, handleGetLogs);
  server.on("/api/logs/clear", HTTP_POST, handleClearLogs);
  server.on("/api/files", HTTP_GET, handleListFiles);
  server.on("/api/files/download", HTTP_GET, handleDownloadFile);
  server.on("/api/files/delete", HTTP_POST, handleDeleteFile);
  server.on("/api/files/delete-all", HTTP_POST, handleDeleteAllFiles);
  server.on("/api/files/delete-all", HTTP_OPTIONS, handleCORS);
  server.on("/api/storage", HTTP_GET, handleStorageInfo);
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("Servidor HTTP disponível na porta 80.");
  Serial.println("Acesse o dashboard em:");
  Serial.println("  - Por IP: http://" + WiFi.localIP().toString() + "/");
  Serial.printf("  - Por hostname: http://%s.local/\n", WIFI_HOSTNAME);

  // Note: ledTask replaced by led_status module (GPIO14 external LED)
  // The old on-board LED (GPIO2) is still toggled for backward compatibility
  xTaskCreatePinnedToCore(webServerTask, "WebServerTask", 8192, nullptr, 2, nullptr, tskNO_AFFINITY);
}

void loop()
{
  vTaskDelay(portMAX_DELAY);
}
