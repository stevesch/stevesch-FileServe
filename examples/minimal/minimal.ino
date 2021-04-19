#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <stevesch-WiFiConnector.h>

#include <stevesch-FileServe.h>
// This is a test example for FileServe

AsyncWebServer server(80);

///////////////////////////////////////

void onWiFiConnect();
void onWiFiLost();
void handleWifiConnected(bool connected);

///////////////////////////////////////

void setup()
{
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Setup initializing...");

  stevesch::WiFiConnector::enableModeless(true);
  stevesch::WiFiConnector::setOnConnected(handleWifiConnected);
  stevesch::WiFiConnector::setup(&server, ESP_NAME, ESP_AUTH);

  server.begin();

  Serial.println("Setup complete.");
}

void loop()
{
  // place loop code here
}

///////////////////////////////////////

void handleIndex(AsyncWebServerRequest *request)
{
  // for FileServe demo, just redirect to "/ls"
  request->redirect("/ls");
}

void ICACHE_FLASH_ATTR onWiFiConnect()
{
  server.on("/", HTTP_GET, &handleIndex); // replace this w/your root page handler

  server.begin();

  stevesch::FileServe::begin(server);

  stevesch::WiFiConnector::printStatus(Serial);
}

void ICACHE_FLASH_ATTR onWiFiLost()
{
  Serial.println("WiFi connection lost");

  server.end();
}

///////////////////////////////////////

void ICACHE_FLASH_ATTR handleWifiConnected(bool connected)
{
  Serial.printf("WiFi connected: %s\n", connected ? "TRUE" : "FALSE");
  if (connected)
  {
    onWiFiConnect();
  }
  else
  {
    onWiFiLost();
  }
}

///////////////////////////////////////
