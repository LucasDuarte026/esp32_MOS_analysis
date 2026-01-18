#include <Arduino.h>

#include "web_ui.h"
#include "generated/web_dashboard.h"

namespace webui
{
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
} // namespace webui
