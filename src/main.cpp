#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include "version.h"

#include "wifi_credentials.h"
#include "wifi_manager.h"
#define USE_ASYNC_WEBSERVER
#include "web_ui.h"
#include "mosfet_controller.h"
#include "monitoring_task.h"
#include "log_buffer.h"
#include "led_status.h"
#include "debug_mode.h"
#include "hardware_hal.h"

#include "file_manager.h"
#include <FFat.h>
#include "email_manager.h"

// FreeRTOS headers
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
}

MOSFETController mosfet_controller;

namespace
{
constexpr uint8_t LED_PIN = 2;

// Persistent configuration (NVS)
Preferences prefs;

// ESPAsyncWebServer - Non-blocking!
AsyncWebServer server(80);
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
  
  StaticJsonDocument<1024> doc;
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
      tzset(); // Apply TZ immediately
      LOG_INFO("System time synchronized to: %lu (Local: %s)", ts, ctime(&tv.tv_sec));
    }
  }
  
  SweepConfig config;
  config.vgs_start = doc["vgs_start"] | 0.0f;
  config.vgs_end = doc["vgs_end"] | 3.5f;
  config.vgs_step = doc["vgs_step"] | 0.05f;
  config.rshunt = doc["rshunt"] | 100.0f;
  config.settling_ms = doc["settling_ms"] | 0;
  
  const char* fname = doc["filename"] | "mosfet_data";
  config.filename = String(fname);
  
  config.vds_start = doc["vds_start"] | 0.0f;
  config.vds_end = doc["vds_end"] | 5.0f;
  config.vds_step = doc["vds_step"] | 0.05f;
  
  const char* sweepModeStr = doc["sweep_mode"] | "VGS";
  config.sweep_mode = (strcmp(sweepModeStr, "VDS") == 0) ? SWEEP_VDS : SWEEP_VGS;
  config.use_vsh_precise = doc["use_vsh_precise"] | true;

  // Ensure V_start <= V_end for both axes
  if (config.vgs_start > config.vgs_end) {
    float temp = config.vgs_start;
    config.vgs_start = config.vgs_end;
    config.vgs_end = temp;
    LOG_WARN("VGS range swapped: %.3f to %.3fV", config.vgs_start, config.vgs_end);
  }
  if (config.vds_start > config.vds_end) {
    float temp = config.vds_start;
    config.vds_start = config.vds_end;
    config.vds_end = temp;
    LOG_WARN("VDS range swapped: %.3f to %.3fV", config.vds_start, config.vds_end);
  }
  
  // Oversampling configuration (1 = disabled, 16 = default)
  uint16_t oversampling = doc["oversampling"] | 16;
  config.oversampling = oversampling;
  // NOTE: setOversamplingCount removed here — switchMode() below recreates the
  // ADC instance with halCfg.adc_oversampling, making a prior call redundant.
  LOG_INFO("ADC oversampling set to %d (%s)", oversampling, oversampling > 1 ? "enabled" : "disabled");

  // ADC PGA gain (255 = Auto-Ranging)
  config.adc_gain_vsh = (uint8_t)(doc["adc_gain_vsh"] | 255);  // default: Auto
  config.adc_gain_vd  = (uint8_t)(doc["adc_gain_vd"]  | 255);  // default: Auto
  config.adc_gain_vg  = (uint8_t)(doc["adc_gain_vg"]  | 255);  // default: Auto

  // Parse ext_dac_vref from request or fall back to NVS-stored value
  float extDacVref = doc["ext_dac_vref"] | 5.0f;
  if (doc.containsKey("ext_dac_vref")) {
    // Validate range 4.0 - 5.5 V
    if (extDacVref < 4.0f || extDacVref > 5.5f) {
      LOG_ERROR("ext_dac_vref %.2f out of range [4.0, 5.5]", extDacVref);
      AsyncWebServerResponse *response = request->beginResponse(400, "application/json",
        "{\"error\":\"out_of_range_vref\",\"message\":\"VDD do MCP4725 deve estar entre 4.0V e 5.5V\"}");
      addCORSHeaders(response);
      request->send(response);
      return;
    }
    // Persist to NVS
    prefs.begin("config", false);
    prefs.putFloat("ext_dac_vref", extDacVref);
    prefs.end();
    LOG_INFO("ext_dac_vref = %.3f V saved to NVS", extDacVref);
  }
  config.ext_dac_vref = extDacVref;

  // Hardware mode: external = dual MCP4725 (0x61 VDS, 0x60 VGS) + ADS1115; internal = dual ESP32 DAC + internal ADC
  bool useExternal = doc["use_external_hw"] | true;  // default: external
  config.use_external_hw = useExternal;
  hal::HardwareMode targetMode = useExternal ? hal::HardwareMode::HW_EXTERNAL : hal::HardwareMode::HW_INTERNAL;

  // ── Hardware Pre-flight Check (Backend) ──────────────────────────────────
  if (targetMode == hal::HardwareMode::HW_EXTERNAL) {
    auto status = hal::HardwareHAL::checkExternalDevices();
    if (!status.all_ok()) {
      LOG_ERROR("Cannot start measurement: External hardware missing");
      String err = "{\"error\":\"hardware_missing\",\"mcp4725_vds\":";
      err += (status.mcp4725_vds ? "true" : "false");
      err += ",\"mcp4725_vgs\":";
      err += (status.mcp4725_vgs ? "true" : "false");
      err += ",\"ads1115\":";
      err += (status.ads1115 ? "true" : "false");
      err += "}";
      
      AsyncWebServerResponse *response = request->beginResponse(424, "application/json", err);
      addCORSHeaders(response);
      request->send(response);
      return;
    }
  }
  // ─────────────────────────────────────────────────────────────────────────

  hal::HalConfig halCfg;
  halCfg.hardware_mode    = targetMode;
  halCfg.adc_oversampling = oversampling;
  halCfg.ext_dac_vref     = extDacVref;
  // Use the USB/External VDD as the software limit for both modes.
  // Note: Internal ESP32 DACs will still physically clamp at 3.3V.
  halCfg.max_vgs          = extDacVref;
  halCfg.max_vds          = extDacVref;
  
  hal::HardwareHAL::instance().switchMode(targetMode, halCfg);

  // Apply VDD reference to both MCP4725 (when present)
  {
    auto& hal = hal::HardwareHAL::instance();
    if (auto* vdsDac = hal.getExternalVDS()) vdsDac->setExtDacVref(extDacVref);
    if (auto* vgsDac = hal.getExternalVGS()) vgsDac->setExtDacVref(extDacVref);
  }

  // Note: PGA gains for ExternalADC are now applied dynamically during the sweep 
  // via mosfet_controller based on config.adc_gain_vsh, config.adc_gain_vd, and config.adc_gain_vg.
  LOG_INFO("Hardware mode: %s", useExternal ? "EXTERNAL (MCP4725 VDS@0x61 + VGS@0x60 + ADS1115@0x48)" : "INTERNAL (ESP32 dual DAC + ADC)");
  
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
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
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

void handleHwCheck(AsyncWebServerRequest *request)
{
  // Only meaningful in EXTERNAL mode; in INTERNAL mode all devices are "not needed"
  bool isExternal = (hal::HardwareHAL::instance().getMode() == hal::HardwareMode::HW_EXTERNAL);

  String json = "{";
  if (!isExternal) {
    // Internal mode — no external devices required
    json += "\"mode\":\"internal\",";
    json += "\"mcp4725_vds\":null,";
    json += "\"mcp4725_vgs\":null,";
    json += "\"ads1115\":null,";
    json += "\"all_ok\":true";
  } else {
    auto s = hal::HardwareHAL::checkExternalDevices();
    json += "\"mode\":\"external\",";
    json += "\"mcp4725_vds\":" + String(s.mcp4725_vds ? "true" : "false") + ",";
    json += "\"mcp4725_vgs\":" + String(s.mcp4725_vgs ? "true" : "false") + ",";
    json += "\"ads1115\":" + String(s.ads1115 ? "true" : "false") + ",";
    json += "\"all_ok\":" + String(s.all_ok() ? "true" : "false");
    LOG_INFO("HW check: MCP4725_VDS=%s MCP4725_VGS=%s ADS1115=%s",
             s.mcp4725_vds ? "OK" : "MISSING",
             s.mcp4725_vgs ? "OK" : "MISSING",
             s.ads1115     ? "OK" : "MISSING");
  }
  json += "}";

  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
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
  json += "\"storage_percent\":" + String((int)(status.storage_percent * 100)) + ",";
  json += "\"storage_total\":" + String(status.storage_total) + ",";
  json += "\"storage_used\":" + String(status.storage_used);
  json += "}";
  
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  addCORSHeaders(response);
  request->send(response);
}

void handleGetLogs(AsyncWebServerRequest *request)
{
  String json = g_log_buffer.getLogsJSON();
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
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

void handleEmailSend(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  LOG_INFO("HTTP POST /api/email/send from %s", request->client()->remoteIP().toString().c_str());
  
  String body = String((char*)data).substring(0, len);
  // Log body responsibly (truncate if huge)
  if (body.length() > 200) LOG_DEBUG("Email req body: %s...", body.substring(0, 200).c_str());
  else LOG_DEBUG("Email req body: %s", body.c_str());

  StaticJsonDocument<1024> doc; 
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    LOG_ERROR("JSON Error: %s", error.c_str());
    AsyncWebServerResponse *response = request->beginResponse(400, "application/json", "{\"error\":\"invalid_json\"}");
    addCORSHeaders(response);
    request->send(response);
    return;
  }

  String to = doc["to"] | "";
  String cc = doc["cc"] | "";
  String subjectInput = doc["subject"] | "No Subject";
  String bodyText = doc["body"] | "";
  
  // Credentials
  String senderEmail = doc["sender_email"] | "";
  String senderPass = doc["sender_password"] | "";
  String smtpHost = doc["smtp_host"] | "";
  int smtpPort = doc["smtp_port"] | 465;

  if (to.isEmpty() || senderEmail.isEmpty() || senderPass.isEmpty() || smtpHost.isEmpty()) {
    String err = "{\"error\":\"missing_fields\", \"details\":\"to, sender_email, sender_password, smtp_host are required\"}";
    request->send(400, "application/json", err);
    return;
  }

  std::vector<String> files;
  if (doc.containsKey("files")) {
      JsonArray fileArray = doc["files"];
      for(String f : fileArray) {
          if (FileManager::isValidFilename(f)) {
              files.push_back(String(FileManager::MEASUREMENTS_DIR) + "/" + f);
          }
      }
  }

  // Prefix
  String fullSubject = "[ESP32 PARAMETER ANALYSER FOR MOSFETs] " + subjectInput;
  
  // Build Request
  EmailRequest req;
  req.smtpHost = smtpHost;
  req.smtpPort = smtpPort;
  req.senderEmail = senderEmail;
  req.senderPassword = senderPass;
  req.to = to;
  req.cc = cc;
  req.subject = fullSubject;
  req.body = bodyText;
  req.files = files;

  if (EmailManager::getInstance().sendEmailAsync(req)) {
      AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"status\":\"queued\"}");
      addCORSHeaders(response);
      request->send(response);
  } else {
      AsyncWebServerResponse *response = request->beginResponse(429, "application/json", "{\"error\":\"busy\"}");
      addCORSHeaders(response);
      request->send(response);
  }
}

void handleEmailStatus(AsyncWebServerRequest *request)
{
  EmailStatus status = EmailManager::getInstance().getStatus();
  
  String stateStr = "IDLE";
  switch(status.state) {
      case EmailStatus::IDLE: stateStr = "IDLE"; break;
      case EmailStatus::CONNECTING: stateStr = "CONNECTING"; break;
      case EmailStatus::SENDING_BODY: stateStr = "SENDING_BODY"; break;
      case EmailStatus::SENDING_ATTACHMENT: stateStr = "SENDING_ATTACHMENT"; break;
      case EmailStatus::SUCCESS: stateStr = "SUCCESS"; break;
      case EmailStatus::FAILED: stateStr = "FAILED"; break;
  }

  // Create JSON response
  String json = "{";
  json += "\"status\":\"" + stateStr + "\",";
  json += "\"progress\":" + String(status.progress) + ",";
  json += "\"message\":\"" + status.message + "\",";
  json += "\"file\":\"" + status.currentFile + "\",";
  json += "\"timestamp\":" + String(status.timestamp);
  json += "}";

  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  addCORSHeaders(response);
  request->send(response);
}

void handleGetConfig(AsyncWebServerRequest *request)
{
  prefs.begin("config", true); // read-only
  float vref = prefs.getFloat("ext_dac_vref", 5.0f);
  prefs.end();

  String json = "{\"ext_dac_vref\":" + String(vref, 2) + "}";
  AsyncWebServerResponse *response = request->beginResponse(200, "application/json", json);
  response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
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


} // namespace

void setup()
{
  Serial.begin(115200);
  delay(100);

  // Set timezone for Sao Paulo, Brazil (UTC-3, no DST)
  setenv("TZ", "<-03>3", 1);
  tzset();

  initAsyncLogging();
  debug_mode::init();

  // Load persisted config from NVS and apply to HAL
  prefs.begin("config", false); // Open in read-write to ensure namespace exists
  if (!prefs.isKey("ext_dac_vref")) {
    prefs.putFloat("ext_dac_vref", 5.0f);
  }
  float storedVref = prefs.getFloat("ext_dac_vref", 5.0f);
  prefs.end();
  LOG_INFO("NVS: ext_dac_vref loaded = %.3f V", storedVref);
  // ExternalDAC will be initialized by mosfet_controller.begin() → hal::init();
  // Vref will be applied after the first switchMode() call.
  // Store globally so handleStartMeasurement can use it as default.

  if (!FileManager::init()) {
    LOG_ERROR("File system initialization failed");
  }
  
  pinMode(LED_PIN, OUTPUT);

  mosfet_controller.begin();
  wifi_manager::connectWithFallback();

  if (MDNS.begin(WIFI_HOSTNAME))
  {
    Serial.println("mDNS responder started.");
    Serial.printf("Device accessible at: http://%s.local/\n", WIFI_HOSTNAME);
    MDNS.addService("http", "tcp", 80);
  }

  monitoring::begin();
  LOG_INFO("Monitoring system started");
  
  led_status::init();
  
  // Initialize Email Manager
  EmailManager::getInstance().begin();

  // Configure AsyncWebServer routes
  LOG_INFO("Configuring AsyncWebServer routes");
  
  // Static files
  server.on("/", HTTP_GET, handleRoot);
  server.on("/visualization", HTTP_GET, handleVisualization);
  server.on("/email", HTTP_GET, handleEmail);
  server.on("/dashboard.css", HTTP_GET, handleCSS);
  server.on("/core.js", HTTP_GET, webui::sendCoreJs);
  server.on("/collection.js", HTTP_GET, webui::sendCollectionJs);
  server.on("/visualization.js", HTTP_GET, webui::sendVisualizationJs);
  server.on("/email.js", HTTP_GET, webui::sendEmailJs);
  
  // API endpoints
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/temperature", HTTP_GET, handleTemperature);
  server.on("/api/usb_status", HTTP_GET, handleUSBStatus);
  server.on("/api/hw/check", HTTP_GET, handleHwCheck);  // External peripheral probe
  server.on("/api/system_info", HTTP_GET, handleSystemInfo);
  server.on("/api/progress", HTTP_GET, handleGetProgress);
  server.on("/api/logs", HTTP_GET, handleGetLogs);
  // Important: specific routes first
  server.on("/api/files/download", HTTP_GET, handleDownloadFile);
  server.on("/api/files", HTTP_GET, handleListFiles);
  server.on("/api/storage", HTTP_GET, handleStorageInfo);
  
  // Email endpoints
  server.on("/api/email/status", HTTP_GET, handleEmailStatus);
  server.on("/api/email/send", HTTP_POST, 
    [](AsyncWebServerRequest *request){},
    NULL,
    handleEmailSend);

  server.on("/api/email/send", HTTP_OPTIONS, handleCORS);
  server.on("/api/email/status", HTTP_OPTIONS, handleCORS);

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
  Serial.println("HTTP server listening on port 80.");
  Serial.println("Dashboard available at:");
  Serial.println("  - By IP:       http://" + WiFi.localIP().toString() + "/");
  Serial.printf("  - By hostname: http://%s.local/\n", WIFI_HOSTNAME);
}

void loop()
{
  // ESPAsyncWebServer handles requests in background - no need for handleClient()
  vTaskDelay(portMAX_DELAY);
}
