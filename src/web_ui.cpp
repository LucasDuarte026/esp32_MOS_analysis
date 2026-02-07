#include <Arduino.h>

#define USE_ASYNC_WEBSERVER
#include "web_ui.h"
#include "generated/web_dashboard.h"

namespace webui
{

#ifdef USE_ASYNC_WEBSERVER
// Helper: Create chunked response for PROGMEM content
// This avoids allocating entire content in RAM at once
static void sendProgmemChunked(AsyncWebServerRequest *request, const char *contentType, const char *progmemContent)
{
  size_t contentLen = strlen_P(progmemContent);

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      contentType,
      [progmemContent, contentLen](uint8_t *buffer, size_t maxLen, size_t index) -> size_t
      {
        if (index >= contentLen)
          return 0; // Done

        size_t remaining = contentLen - index;
        size_t toSend = remaining < maxLen ? remaining : maxLen;
        memcpy_P(buffer, progmemContent + index, toSend);
        return toSend;
      });

  request->send(response);
}

// ESPAsyncWebServer version - use chunked response for large PROGMEM strings
void sendIndex(AsyncWebServerRequest *request)
{
  sendProgmemChunked(request, "text/html", kIndexHtml);
}

void sendVisualization(AsyncWebServerRequest *request)
{
  sendProgmemChunked(request, "text/html", kVisualizationHtml);
}

void sendEmail(AsyncWebServerRequest *request)
{
  sendProgmemChunked(request, "text/html", kEmailHtml);
}

void sendCSS(AsyncWebServerRequest *request)
{
  sendProgmemChunked(request, "text/css", kDashboardCss);
}

void sendCoreJs(AsyncWebServerRequest *request)
{
  sendProgmemChunked(request, "application/javascript", kCoreJs);
}

void sendCollectionJs(AsyncWebServerRequest *request)
{
  sendProgmemChunked(request, "application/javascript", kCollectionJs);
}

void sendVisualizationJs(AsyncWebServerRequest *request)
{
  sendProgmemChunked(request, "application/javascript", kVisualizationJs);
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

void sendCoreJs(WebServer &server)
{
  server.send_P(200, "application/javascript", kCoreJs);
}

void sendCollectionJs(WebServer &server)
{
  server.send_P(200, "application/javascript", kCollectionJs);
}

void sendVisualizationJs(WebServer &server)
{
  server.send_P(200, "application/javascript", kVisualizationJs);
}
#endif

} // namespace webui
