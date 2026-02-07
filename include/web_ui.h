#pragma once

// Support both sync and async WebServer
#ifdef USE_ASYNC_WEBSERVER
#include <ESPAsyncWebServer.h>
namespace webui
{
void sendIndex(AsyncWebServerRequest *request);
void sendVisualization(AsyncWebServerRequest *request);
void sendEmail(AsyncWebServerRequest *request);
void sendCSS(AsyncWebServerRequest *request);
void sendCoreJs(AsyncWebServerRequest *request);
void sendCollectionJs(AsyncWebServerRequest *request);
void sendVisualizationJs(AsyncWebServerRequest *request);
} // namespace webui
#else
#include <WebServer.h>
namespace webui
{
void sendIndex(WebServer &server);
void sendVisualization(WebServer &server);
void sendEmail(WebServer &server);
void sendCSS(WebServer &server);
void sendCoreJs(WebServer &server);
void sendCollectionJs(WebServer &server);
void sendVisualizationJs(WebServer &server);
} // namespace webui
#endif
