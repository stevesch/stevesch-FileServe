#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <algorithm>

#include "fileServe.h"

namespace
{
void handleListFiles(AsyncWebServerRequest *request);
void handleMore(AsyncWebServerRequest *request);
void handleRemove(AsyncWebServerRequest *request);
void handleServeFile(AsyncWebServerRequest *request);

FS* sFileSys = &SPIFFS;
}

namespace stevesch {
namespace FileServe {
  void begin(AsyncWebServer& server, FS* optionalFileSys)
  {
    if (optionalFileSys) {
      sFileSys = optionalFileSys;
    }
    server.on("/ls", HTTP_GET, handleListFiles);
    server.on("/more", HTTP_GET, handleMore);
    server.on("/rm", HTTP_GET, handleRemove);
    server.on("/dl", HTTP_GET, handleServeFile);
  }
} // namespace FileServe
} // namespace stevesch

namespace
{
const char kPageTemplatePreBody[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html><head><title>%TITLE%</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
<style>
html { font-family: Verdana; background-color: #f8f8f8; }
body { margin: 0; }
.content { padding: 4px; font-size: 1.0rem; }
td { padding: 2px 4px; }
ul { list-style-type: none; margin: 0; padding: 0; overflow: hidden; background-color: #29a64f; }
li { display: block; float: left; color: black; font-family: Verdana; text-align: center; padding: 12px 16px; text-decoration: none; }
li a:hover { background-color: #96c47f; }
code { background-color: #ffffff; }
.pr { display: flex; flex-flow: column; flex-wrap: wrap; font-family: Courier; font-size: 15px; padding: 2px 6px; border-left: 4px solid #60e060; }
.cod { white-space: pre-wrap; word-break: break-word }
.rt { float: right }
.hdr { font-weight: bold; color: white; }
.dlicon { margin: 2px 6px; }
</style></head>
<body>
)rawliteral";

// const char kFontAws[] PROGMEM = R"#HTM(
//   <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
// )#HTM";

const char kPageTemplatePostBody[] PROGMEM = R"rawliteral(
</body></html>)rawliteral";

const char kErrorPage[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html><head><title>File reqest error</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
html { font-family: Verdana; background-color: #f8f8f8; }
</style></head>
<body>
<h3>Error encountered while requesting file info</h3>
</body></html>)rawliteral";

void escape(String& esc)
{
  // NOTE: this is a minimal set of reserved characters.
  // esc.replace("\n", "<br/>");
  esc.replace(">", "&gt;");
  esc.replace("<", "&lt;");
  esc.replace("\"", "&quot;");
  esc.replace("'", "&apos;");
  esc.replace("\r\n", "\n");
}

const char kMainIcon[] PROGMEM = R"#HTM(
  <li><a href="/"><i class="fas fa-home" style="color: white"></i></a></li>
)#HTM";

const char kBackToLsIcon[] PROGMEM = R"#HTM(
  <li><a href="/ls"><i class="fas fa-arrow-left" style="color: white"></i></a></li>
)#HTM";
// <i class="fas fa-home"></i>
// <i class="fas fa-th"></i>
// <i class="fas fa-arrow-left"></i>
// <i class="fas fa-bars"></i>

void postFsError(AsyncWebServerRequest *request)
{
  Serial.println("File system error (is it formatted?)");
  request->send(500, "text/html", kErrorPage);
}

bool validateFileSys()
{
  if (sFileSys == &SPIFFS) {
    return SPIFFS.begin();
  }
  return true;
}

const int kDisplaySizeMax = 65536;
const size_t kReadChunkMax = 1023;
uint8_t buf[kReadChunkMax + 1];

// Show file contents
void handleMore(AsyncWebServerRequest *request)
{
  if (!validateFileSys()) {
    postFsError(request);
    return;
  }

  // TODO show error if no path?
  // if (!request->hasArg("path")) {
  // }

  String filePath = request->arg("path");

  String s(kPageTemplatePreBody);
  s.replace("%TITLE%", String("File: ") + filePath);

  s += "<ul>";

  s += kBackToLsIcon;
  
  s += "<li><span class=\"hdr\">File ";
  s += filePath;

  File f;
  if (sFileSys->exists(filePath)) {
    f = sFileSys->open(filePath, FILE_READ);
  }
  size_t fileSize = 0;
  if (f) {
    fileSize = f.size();
  }
  yield();

  if (fileSize > kDisplaySizeMax)
  {
    s += " (truncated-- size ";
    s += fileSize;
    s += " exceeds display size of ";
    s += kDisplaySizeMax;
    s += ")";
  }
  s += "</span></li>";
  
  s += "</ul>";

  s += "<div class=\"content\">";

  if (f) {
    s += "<div><pre class=\"pr\"><code class=\"cod\">";
    // AsyncWebServerResponse* resp = request->beginChunkedResponse("test/html"); // TODO?
    bool overflow = false;
    size_t n = f.available();
    if (n > kDisplaySizeMax) {
      n = kDisplaySizeMax;
      overflow = true;
    }
    Serial.printf("Reading %d bytes from %s\n", (int)n, filePath.c_str());
    int l0 = s.length();
    String esc;
    esc.reserve(std::min(n, kReadChunkMax + 1));
    while (n) {
      size_t toRead = std::min(n, kReadChunkMax);
      int numRead = f.read(buf, toRead);
      yield();
      if (!numRead) {
        break;
      }
      buf[numRead] = '\0';
      esc = (const char*)buf;
      escape(esc);
      s += esc;
      n -= numRead;
      yield();
    }
    Serial.printf("Added %d characters to output\n", (int)(s.length() - l0));
    s += "</code></pre></div>";
    if (overflow) {
      s += "<div>. . . (more)</div>";
    }
    f.close();
  }
  else
  {
    Serial.printf("### Unable to read file '%s'\n", filePath.c_str());
    Serial.printf("### reported file size %d\n", (int)fileSize);
  }
  s += "</div>";
  s += kPageTemplatePostBody;
  yield();

  request->send(200, "text/html", s);
}

void handleRemove(AsyncWebServerRequest *request)
{
  if (!validateFileSys()) {
    postFsError(request);
    return;
  }
  String filePath = request->arg("path");
  File f;
  if (sFileSys->exists(filePath)) {
    sFileSys->remove(filePath);
  }
  request->redirect("/ls");
}

void handleListFiles(AsyncWebServerRequest *request)
{
  if (!validateFileSys()) {
    postFsError(request);
    return;
  }

  String s(kPageTemplatePreBody);
  s.replace("%TITLE%", "File List");
  File root = sFileSys->open("/", FILE_READ);
  File file = root.openNextFile();

  s += "<ul>";

  s += kMainIcon;

  s += "<li>";
  s += "<span class=\"hdr\">Files:</span>";
  s += "</li>";

  s += "</ul>";

  s += "<div class=\"content\">";
  s += "<table>";
  while (file)
  {
      // const char* fileName = file.name();
      const char* filePath = file.path();
      size_t fileSize = file.size();
      // Serial.print("FILE: ");
      // Serial.println(file.name());
      s += "<tr>";

      s += "<td>";
      s += "<a download href=\"";
      s += "/dl?path=";
      s += filePath;
      s += "\">";
      s += "<i class=\"dlicon fas fa-download\" color=\"#29a64f\"></i>";
      s += "</a>";
      s += "</td>";

      s += "<td>";
      if (fileSize < 4096) {
        // bytes
        s += fileSize;
        s += "B";
      } else if (fileSize < 1024*1024) {
        float k = (float)fileSize / 1024;
        String sz(k, 2);
        s += sz;
        s += "K";
      } else {
        float m = (float)fileSize / (1024*1024);
        String sz(m, 2);
        s += sz;
        s += "M";
      }
      s += "</td>";

      s += "<td>";
      s += "<a href=\"";
      s += "/more?path=";
      s += filePath;
      s += "\">";
      s += filePath;
      s += "</a>";
      s += "</td>";

      s += "<td>";
      s += "<a href=\"";
      s += "/rm?path=";
      s += filePath;
      s += "\">";
      s += "<i class=\"dlicon fas fa-trash-alt\" color=\"#8c0106\"></i>";
      s += "</a>";
      s += "</td>";

      s += "</tr>";
      file.close();
      yield();
      file = root.openNextFile();
  }
  root.close();

  s += "</table>";
  s += "</div>";

  s += kPageTemplatePostBody;
  request->send(200, "text/html", s);
}

void handleServeFile(AsyncWebServerRequest *request)
{
  if (!validateFileSys()) {
    postFsError(request);
    return;
  }

  String filePath = request->arg("path");
  request->send(*sFileSys, filePath);
}

}
