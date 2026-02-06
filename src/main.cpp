#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include <time.h>
#include "version.h"

#include "wifi_credentials.h"
#define USE_ASYNC_WEBSERVER
#include "web_ui.h"
#include "mosfet_controller.h"
#include "monitoring_task.h"
#include "log_buffer.h"
#include "led_status.h"
#include "debug_mode.h"

#include "file_manager.h"
#include <FFat.h>

// FreeRTOS headers
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
}

namespace
{
constexpr uint8_t LED_PIN = 2;
constexpr uint32_t WIFI_TIMEOUT_MS = 20000;

// ESPAsyncWebServer - Non-blocking!
AsyncWebServer server(80);
MOSFETController mosfet_controller;

// ============================================================================
// CORS Headers Helper (Async version)
// ============================================================================
void addCORSHeaders(AsyncWebServerResponse *response)
{
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  response->addHeader("Access-Control-Allow-Headers", "Content-Type");
  response->addHeader("Access-Control-Max-Age", "86400");
}

// ============================================================================
// Request Handlers (Async - non-blocking)
// ============================================================================

void handleRoot(AsyncWebServerRequest *request)
{
  LOG_INFO("HTTP GET / from %s", request->client()->remoteIP().toString().c_str());
  webui::sendIndex(request);
}

void handleVisualization(AsyncWebServerRequest *request)
{
  LOG_INFO("HTTP GET /visualization from %s", request->client()->remoteIP().toString().c_str());
  webui::sendVisualization(request);
}

void handleEmail(AsyncWebServerRequest *request)
{
  LOG_INFO("HTTP GET /email from %s", request->client()->remoteIP().toString().c_str());
  webui::sendEmail(request);
}

void handleCSS(AsyncWebServerRequest *request)
{
  webui::sendCSS(request);
}

void handleJS(AsyncWebServerRequest *request)
{
  webui::sendJS(request);
}

void handleCORS(AsyncWebServerRequest *request)
{
  AsyncWebServerResponse *response = request->beginResponse(204);
  addCORSHeaders(response);
  request->send(response);
}

void handleStatus(AsyncWebServerRequest *request)
{
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json",
    "{\"status\":\"ready\",\"device\":\"ESP32-MOSFET\"}");
  addCORSHeaders(response);
  request->send(response);
}

void handleStartMeasurement(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  LOG_INFO("HTTP POST /api/start from %s", request->client()->remoteIP().toString().c_str());
  
  String body = String((char*)data).substring(0, len);
  LOG_DEBUG("Request body: %s", body.c_str());
  
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    LOG_ERROR("JSON parse error: %s", error.c_str());
    AsyncWebServerResponse *response = request->beginResponse(400, "application/json", 
      "{\"error\":\"invalid_json\"}");
    addCORSHeaders(response);
    request->send(response);
    return;
  }

  // Update system time if timestamp provided
  if (doc.containsKey("timestamp")) {
    unsigned long ts = doc["timestamp"];
    if (ts > 1600000000) {
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
  
  const char* fname = doc["filename"] | "mosfet_data";
  config.filename = String(fname);
  
  config.vds_start = doc["vds_start"] | 0.0f;
  config.vds_end = doc["vds_end"] | 5.0f;
  config.vds_step = doc["vds_step"] | 0.05f;
  
  const char* sweepModeStr = doc["sweep_mode"] | "VGS";
  config.sweep_mode = (strcmp(sweepModeStr, "VDS") == 0) ? SWEEP_VDS : SWEEP_VGS;
  
  // Validate
  if (config.vgs_start < 0 || config.vgs_end > 5.0) {
    AsyncWebServerResponse *response = request->beginResponse(400, "application/json",
      "{\"error\":\"invalid_vgs_range\"}");
    addCORSHeaders(response);
    request->send(response);
    return;
  }
  
  if (config.rshunt <= 0) {
    AsyncWebServerResponse *response = request->beginResponse(400, "application/json",
      "{\"error\":\"invalid_rshunt\"}");
    addCORSHeaders(response);
    request->send(response);
    return;
  }
  
  // v2.0.0: Check storage before starting
  if (!FileManager::checkStorageAvailable()) {
    LOG_ERROR("Storage limit exceeded (>80%%)");
    AsyncWebServerResponse *response = request->beginResponse(507, "application/json",
      "{\"error\":\"storage_full\",\"message\":\"Storage exceeds 80%. Delete old files.\"}");
    addCORSHeaders(response);
    request->send(response);
    return;
  }
  
  LOG_INFO("Parsed config: VGS %.2f-%.2f step %.3f, VDS %.2f-%.2f step %.3f, Mode=%s",
           config.vgs_start, config.vgs_end, config.vgs_step,
           config.vds_start, config.vds_end, config.vds_step,
           config.sweep_mode == SWEEP_VDS ? "VDS" : "VGS");
  
  bool success = mosfet_controller.startMeasurementAsync(config);
  
  if (success) {
    String json = "{\"status\":\"started\",\"filename\":\"" + config.filename + "\"}";
    AsyncWebServerResponse *response = request->beginResponse(202, "application/json", json);
    addCORSHeaders(response);
    request->send(response);
  } else {
    LOG_ERROR("Failed to start measurement");
    AsyncWebServerResponse *response = request->beginResponse(500, "application/json",
      "{\"error\":\"start_failed\"}");
    addCORSHeaders(response);
    request->send(response);
  }
}

void handleCancelMeasurement(AsyncWebServerRequest *request)
{
  LOG_INFO("HTTP POST /api/cancel from %s", request->client()->remoteIP().toString().c_str());
  mosfet_controller.cancelMeasurement();
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json",
    "{\"status\":\"cancelled\"}");
  addCORSHeaders(response);
  request->send(response);
}

void handleGetProgress(AsyncWebServerRequest *request)
{
  auto progress = mosfet_controller.getProgress();
  
  String json = "{";
  json += "\"running\":" + String(progress.is_running ? "true" : "false") + ",";
  json += "\"progress\":" + String(progress.progress_percent) + ",";
  json += "\"vds\":" + String(progress.current_vds, 3) + ",";
  json += "\"message\":\"" + progress.message + "\",";
  json += "\"error\":" + String(progress.has_error ? "true" : "false") + ",";
  json += "\"error_msg\":\"" + progress.error_message + "\"";
  json += "}";
  
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  addCORSHeaders(response);
  request->send(response);
}

void handleTemperature(AsyncWebServerRequest *request)
{
  monitoring::SystemStatus status = monitoring::getStatus();
  String json = "{\"temperature\":" + String(status.temperature_celsius, 1) + ",\"unit\":\"C\"}";
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  addCORSHeaders(response);
  request->send(response);
}

void handleUSBStatus(AsyncWebServerRequest *request)
{
  monitoring::SystemStatus status = monitoring::getStatus();
  String json = "{\"usb_connected\":" + String(status.usb_connected ? "true" : "false") + "}";
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  addCORSHeaders(response);
  request->send(response);
}

void handleSystemInfo(AsyncWebServerRequest *request)
{
  monitoring::SystemStatus status = monitoring::getStatus();
  char chipId[17];
  snprintf(chipId, sizeof(chipId), "%016llX", status.chip_id);
  
  String json = "{";
  json += "\"chip_id\":\"" + String(chipId) + "\",";
  json += "\"version\":\"" + String(SOFTWARE_VERSION) + "\",";
  json += "\"temperature\":" + String(status.temperature_celsius, 1) + ",";
  json += "\"usb_connected\":" + String(status.usb_connected ? "true" : "false") + ",";
  json += "\"free_heap\":" + String(status.free_heap) + ",";
  json += "\"debug_mode\":" + String(debug_mode::isEnabled() ? "true" : "false") + ",";
  json += "\"storage_percent\":" + String((int)(status.storage_percent * 100));
  json += "}";
  
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  addCORSHeaders(response);
  request->send(response);
}

void handleGetLogs(AsyncWebServerRequest *request)
{
  String json = g_log_buffer.getLogsJSON();
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  addCORSHeaders(response);
  request->send(response);
}

void handleClearLogs(AsyncWebServerRequest *request)
{
  LOG_INFO("HTTP POST /api/logs/clear from %s", request->client()->remoteIP().toString().c_str());
  g_log_buffer.clear();
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json",
    "{\"status\":\"logs_cleared\"}");
  addCORSHeaders(response);
  request->send(response);
}

void handleListFiles(AsyncWebServerRequest *request)
{
  LOG_DEBUG("HTTP GET /api/files from %s", request->client()->remoteIP().toString().c_str());
  
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
  
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  addCORSHeaders(response);
  request->send(response);
}

void handleDownloadFile(AsyncWebServerRequest *request)
{
  if (!request->hasParam("file")) {
    request->send(400, "text/plain", "Missing file parameter");
    return;
  }
  
  String filename = request->getParam("file")->value();
  
  if (!FileManager::isValidFilename(filename)) {
    LOG_WARN("Invalid filename: %s", filename.c_str());
    request->send(400, "text/plain", "Invalid filename");
    return;
  }
  
  LOG_INFO("HTTP GET /api/files/download?file=%s", filename.c_str());
  
  String fullPath = String(FileManager::MEASUREMENTS_DIR) + "/" + filename;
  
  // Use chunked response for large files
  request->send(FFat, fullPath.c_str(), "text/csv", true);
}

void handleDeleteFile(AsyncWebServerRequest *request)
{
  if (!request->hasParam("file")) {
    AsyncWebServerResponse *response = request->beginResponse(400, "application/json",
      "{\"error\":\"missing_file\"}");
    addCORSHeaders(response);
    request->send(response);
    return;
  }
  
  String filename = request->getParam("file")->value();
  
  if (!FileManager::isValidFilename(filename)) {
    AsyncWebServerResponse *response = request->beginResponse(400, "application/json",
      "{\"error\":\"invalid_filename\"}");
    addCORSHeaders(response);
    request->send(response);
    return;
  }
  
  LOG_INFO("HTTP POST /api/files/delete?file=%s", filename.c_str());
  
  bool success = FileManager::deleteFile(filename);
  
  if (success) {
    int count = FileManager::countFiles();
    String json = "{\"success\":true,\"count\":" + String(count) + "}";
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
    addCORSHeaders(response);
    request->send(response);
  } else {
    AsyncWebServerResponse *response = request->beginResponse(500, "application/json",
      "{\"error\":\"delete_failed\"}");
    addCORSHeaders(response);
    request->send(response);
  }
}

void handleDeleteAllFiles(AsyncWebServerRequest *request)
{
  LOG_INFO("HTTP POST /api/files/delete-all");
  
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
  
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  addCORSHeaders(response);
  request->send(response);
}

void handleStorageInfo(AsyncWebServerRequest *request)
{
  StorageInfo info = FileManager::getStorageInfo();
  int fileCount = FileManager::countFiles();
  
  String json = "{";
  json += "\"total_bytes\":" + String(info.totalBytes) + ",";
  json += "\"free_bytes\":" + String(info.freeBytes) + ",";
  json += "\"used_bytes\":" + String(info.usedBytes) + ",";
  json += "\"used_percent\":" + String((int)(info.percentUsed * 100)) + ",";
  json += "\"file_count\":" + String(fileCount) + "}";
  
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  addCORSHeaders(response);
  request->send(response);
}

void handleNotFound(AsyncWebServerRequest *request)
{
  LOG_WARN("HTTP 404: %s %s", 
           request->methodToString(), 
           request->url().c_str());
  AsyncWebServerResponse *response = request->beginResponse(404, "application/json",
    "{\"error\":\"not found\"}");
  addCORSHeaders(response);
  request->send(response);
}

void connectToWifi()
{
  LOG_INFO("Connecting to WiFi: %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  led_status::setState(led_status::State::WIFI_DISCONNECTED);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    vTaskDelay(pdMS_TO_TICKS(250));

    if (millis() - start > WIFI_TIMEOUT_MS)
    {
      LOG_ERROR("WiFi connection timeout after %d ms", WIFI_TIMEOUT_MS);
      LOG_ERROR("Restarting ESP32 in 5 seconds...");
      vTaskDelay(pdMS_TO_TICKS(5000));
      ESP.restart();
    }
  }

  digitalWrite(LED_PIN, HIGH);
  led_status::setState(led_status::State::STANDBY);
  
  LOG_INFO("WiFi connected!");
  LOG_INFO("IP Address: %s", WiFi.localIP().toString().c_str());
  LOG_INFO("Hostname: %s.local", WIFI_HOSTNAME);
}

} // namespace

void setup()
{
  Serial.begin(115200);
  delay(100);
  initAsyncLogging();
  debug_mode::init();
  
  if (!FileManager::init()) {
    LOG_ERROR("File system initialization failed");
  }
  
  pinMode(LED_PIN, OUTPUT);

  mosfet_controller.begin();
  connectToWifi();

  if (MDNS.begin(WIFI_HOSTNAME))
  {
    Serial.println("mDNS iniciado com sucesso!");
    Serial.printf("Você pode acessar o dispositivo em: http://%s.local/\n", WIFI_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }

  monitoring::begin();
  LOG_INFO("Monitoring system started");
  
  led_status::init();

  // Configure AsyncWebServer routes
  LOG_INFO("Configuring AsyncWebServer routes");
  
  // Static files
  server.on("/", HTTP_GET, handleRoot);
  server.on("/visualization", HTTP_GET, handleVisualization);
  server.on("/email", HTTP_GET, handleEmail);
  server.on("/dashboard.css", HTTP_GET, handleCSS);
  server.on("/dashboard.js", HTTP_GET, handleJS);
  
  // API endpoints
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/temperature", HTTP_GET, handleTemperature);
  server.on("/api/usb_status", HTTP_GET, handleUSBStatus);
  server.on("/api/system_info", HTTP_GET, handleSystemInfo);
  server.on("/api/progress", HTTP_GET, handleGetProgress);
  server.on("/api/logs", HTTP_GET, handleGetLogs);
  // Important: specific routes first
  server.on("/api/files/download", HTTP_GET, handleDownloadFile);
  server.on("/api/files", HTTP_GET, handleListFiles);
  server.on("/api/storage", HTTP_GET, handleStorageInfo);
  
  // POST endpoints with body handling
  server.on("/api/start", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleStartMeasurement);
  
  server.on("/api/cancel", HTTP_POST, handleCancelMeasurement);
  server.on("/api/logs/clear", HTTP_POST, handleClearLogs);
  server.on("/api/files/delete", HTTP_POST, handleDeleteFile);
  server.on("/api/files/delete-all", HTTP_POST, handleDeleteAllFiles);
  
  // CORS preflight handlers
  server.on("/api/start", HTTP_OPTIONS, handleCORS);
  server.on("/api/cancel", HTTP_OPTIONS, handleCORS);
  server.on("/api/progress", HTTP_OPTIONS, handleCORS);
  server.on("/api/logs/clear", HTTP_OPTIONS, handleCORS);
  server.on("/api/files/delete", HTTP_OPTIONS, handleCORS);
  server.on("/api/files/delete-all", HTTP_OPTIONS, handleCORS);
  
  server.onNotFound(handleNotFound);
  
  server.begin();
  LOG_INFO("AsyncWebServer started on port 80");
  Serial.println("Servidor HTTP disponível na porta 80.");
  Serial.println("Acesse o dashboard em:");
  Serial.println("  - Por IP: http://" + WiFi.localIP().toString() + "/");
  Serial.printf("  - Por hostname: http://%s.local/\n", WIFI_HOSTNAME);
}

void loop()
{
  // ESPAsyncWebServer handles requests in background - no need for handleClient()
  vTaskDelay(portMAX_DELAY);
}
