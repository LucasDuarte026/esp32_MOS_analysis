#include <Arduino.h>

#define USE_ASYNC_WEBSERVER
#include "web_ui.h"
#include "generated/web_dashboard.h"

namespace webui
{

#ifdef USE_ASYNC_WEBSERVER
// ESPAsyncWebServer version - use chunked response for large PROGMEM strings
void sendIndex(AsyncWebServerRequest *request)
{
  // Use beginResponse_P for PROGMEM data - avoids copying to RAM
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", kIndexHtml);
  request->send(response);
}

void sendVisualization(AsyncWebServerRequest *request)
{
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", kVisualizationHtml);
  request->send(response);
}

void sendEmail(AsyncWebServerRequest *request)
{
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", kEmailHtml);
  request->send(response);
}

void sendCSS(AsyncWebServerRequest *request)
{
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/css", kDashboardCss);
  request->send(response);
}

void sendJS(AsyncWebServerRequest *request)
{
  AsyncWebServerResponse *response = request->beginResponse_P(200, "application/javascript", kDashboardJs);
  request->send(response);
}

#else
// Sync WebServer version (legacy)
void sendIndex(WebServer &server)
{
  server.send_P(200, "text/html", kIndexHtml);
}

void sendVisualization(WebServer &server)
{
  server.send_P(200, "text/html", kVisualizationHtml);
}

void sendEmail(WebServer &server)
{
  server.send_P(200, "text/html", kEmailHtml);
}

void sendCSS(WebServer &server)
{
  server.send_P(200, "text/css", kDashboardCss);
}

void sendJS(WebServer &server)
{
  server.send_P(200, "application/javascript", kDashboardJs);
}
#endif

} // namespace webui
