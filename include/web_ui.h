#pragma once

#include <WebServer.h>

namespace webui
{
void sendIndex(WebServer &server);
void sendVisualization(WebServer &server);
void sendEmail(WebServer &server);
void sendCSS(WebServer &server);
void sendJS(WebServer &server);
} // namespace webui
