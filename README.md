# stevesch-FileServe
Quickly add file serving capability to any wifi-enabled ESP32 sketch.
# Building and Running
If you already have a sketch connecting to WiFi, simply do the following (see minimal.ino in examples folder for a working example):

- add include
`#include <stevesch-FileServe.h>`

- add an AsyncWebServer server object if you do not already create one:
`AsyncWebServer server(80);`

- make sure you call server.begin() after WiFi connects (after wifi startup for sync wifi manager, or in onWiFiConnect for async wifi manager):
`server.begin();`

- After the server.begin() call, call begin for FileServe as well:
`stevesch::FileServe::begin(server);`

- Place files in the 'data' folder of you sketch and trigger the "Upload Filesystem Image" command in Platform IO (or create files in your device's SPIFFS file system however you see fit).

- In a browser, browse to your device ip, followed by "ls", e.g.
http://192.168.0.15/ls
(replace "192.168.0.15" with your device IP or name/address)
