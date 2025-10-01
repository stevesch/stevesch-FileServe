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
void handleTestPage(AsyncWebServerRequest *request);

FS* sFileSys = &SPIFFS;
}

namespace stevesch {
namespace FileServe {
  int sDisplaySizeMax = 102400;
  int sLsMaxToList = 128;

  void begin(AsyncWebServer& server, FS* optionalFileSys)
  {
    if (optionalFileSys) {
      sFileSys = optionalFileSys;
    }
    server.on("/ls", HTTP_GET, handleListFiles);
    server.on("/more", HTTP_GET, handleMore);
    server.on("/rm", HTTP_GET, handleRemove);
    server.on("/dl", HTTP_GET, handleServeFile);
    server.on("/fileServeTest", HTTP_GET, handleTestPage);
  }
} // namespace FileServe
} // namespace stevesch

namespace
{

const char kPagePreTitle[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html><head><title>
)rawliteral";

const char kPagePostTitle[] PROGMEM = R"rawliteral(
</title>
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

void writeEscapedChunk(AsyncResponseStream* response, const uint8_t* data, size_t len)
{
  for (size_t i = 0; i < len; ++i) {
    char ch = static_cast<char>(data[i]);
    switch (ch) {
      case '>':
        response->print(F("&gt;"));
        break;
      case '<':
        response->print(F("&lt;"));
        break;
      case '\"':
        response->print(F("&quot;"));
        break;
      case '\'':
        response->print(F("&apos;"));
        break;
      case '\r':
        // drop carriage returns; rely on the newline that follows
        break;
      default:
        response->write(reinterpret_cast<const uint8_t*>(&ch), 1);
        break;
    }
  }
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
  request->send(500, "text/html", FPSTR(kErrorPage));
}

bool validateFileSys()
{
  // XXX SPIFFS.begin MUST be called externally before using FileServe,
  // but we can't call .begin multiple times, so we just assume it's done.
  // if (sFileSys == &SPIFFS) {
  //   return SPIFFS.begin();
  // }
  return true;
}

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

  AsyncResponseStream* response = request->beginResponseStream("text/html", 8192);
  response->setCode(200);

  // Title/header
  response->print(FPSTR(kPagePreTitle));
  response->print(F("File: "));
  response->print(filePath);
  response->print(FPSTR(kPagePostTitle));

  response->print(F("<ul>"));
  response->print(FPSTR(kBackToLsIcon));
  response->print(F("<li><span class=\"hdr\">File "));
  response->print(filePath);

  File f;
  if (sFileSys->exists(filePath)) {
    f = sFileSys->open(filePath, FILE_READ);
  }
  size_t fileSize = f ? f.size() : 0;
  yield();

  if (fileSize > stevesch::FileServe::sDisplaySizeMax)
  {
    response->print(F(" (truncated-- size "));
    response->print(fileSize);
    response->print(F(" exceeds display size of "));
    response->print(stevesch::FileServe::sDisplaySizeMax);
    response->print(F(")"));
  }

  response->print(F("</span></li>"));
  response->print(F("</ul>"));
  response->print(F("<div class=\"content\">"));

  if (f) {
    response->print(F("<div><pre class=\"pr\"><code class=\"cod\">"));

    bool overflow = false;
    size_t n = f.available();
    if (n > stevesch::FileServe::sDisplaySizeMax) {
      n = stevesch::FileServe::sDisplaySizeMax;
      overflow = true;
    }

    Serial.printf("Reading %d bytes from %s\n", (int)n, filePath.c_str());

    size_t totalAdded = 0;
    while (n) {
      size_t toRead = std::min(n, kReadChunkMax);
      int numRead = f.read(buf, toRead);
      yield();
      if (!numRead) {
        break;
      }
      writeEscapedChunk(response, buf, numRead);
      totalAdded += numRead;

      n -= numRead;
      if ((totalAdded % 10240) == 0) {
        yield();
      }
    }

    Serial.printf("Streamed %d bytes to output\n", static_cast<int>(totalAdded));

    response->print(F("</code></pre></div>"));
    if (overflow) {
      response->print(F("<div>. . . (more)</div>"));
    }
    f.close();
  }
  else
  {
    Serial.printf("### Unable to read file '%s'\n", filePath.c_str());
    Serial.printf("### reported file size %d\n", (int)fileSize);
    response->print(F("<div><i>File not found</i></div>"));
  }
  response->print(F("</div>"));

  response->print(FPSTR(kPageTemplatePostBody));
  yield();
  request->send(response);
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
  Serial.println("Listing files...");

  if (!validateFileSys()) {
    postFsError(request);
    return;
  }

  File root = sFileSys->open("/", FILE_READ);
  if (!root) {
    request->send(500, "text/plain", "Failed to open directory");
    return;
  }

  int numListed = 0;
  const size_t bufSize = 8192;

  AsyncResponseStream *response = request->beginResponseStream("text/html", bufSize);
  response->setCode(200);

  response->print(FPSTR(kPagePreTitle));
  response->print("File List");
  response->print(FPSTR(kPagePostTitle));

  response->print("<ul>");
  response->print(FPSTR(kMainIcon));
  response->print("<li><span class=\"hdr\">Files:</span></li></ul>");

  response->print("<div class=\"content\"><table>");

  File file = root.openNextFile();
  while (file)
  {
      // const char* fileName = file.name();
      String filePath = String(file.path());
      size_t fileSize = file.size();
      // Serial.print("FILE: ");
      // Serial.println(file.name());

      response->print("<tr><td><a download href=\"/dl?path=");
      response->print(filePath);
      response->print("\"><i class=\"dlicon fas fa-download\" color=\"#29a64f\"></i></a></td>");

      response->print("<td>");
      if (fileSize < 4096) {
        // bytes
        response->print((int)fileSize);
        response->print("B");
      } else if (fileSize < 1024*1024) {
        float k = (float)fileSize / 1024;
        String sz(k, 2);
        response->print(sz);
        response->print("K");
      } else {
        float m = (float)fileSize / (1024*1024);
        String sz(m, 2);
        response->print(sz);
        response->print("M");
      }
      response->print("</td>");

      response->print("<td><a href=\"/more?path=");
      response->print(filePath);
      response->print("\">");
      response->print(filePath);
      response->print("</a></td>");

      response->print("<td><a href=\"/rm?path=");
      response->print(filePath);
      response->print("\"><i class=\"dlicon fas fa-trash-alt\" color=\"#8c0106\"></i></a></td></tr>");

      file.close();
      numListed++;
      if (numListed >= stevesch::FileServe::sLsMaxToList) {
        response->print("<tr><td>. . .</td></tr>");
        break;
      }

      yield();
      file = root.openNextFile();
  }
  root.close();

  response->print("</table></div>");

  response->print(FPSTR(kPageTemplatePostBody));

  request->send(response);

  Serial.println("File listing complete.");
}

void handleServeFile(AsyncWebServerRequest *request)
{
  if (!validateFileSys()) {
    postFsError(request);
    return;
  }

  String filePath = request->arg("path");
  if (!sFileSys->exists(filePath)) {
    request->send(404, "text/plain", "File not found");
    return;
  }

  const char *contentType = "application/octet-stream";
  AsyncWebServerResponse *response = request->beginResponse(*sFileSys, filePath, contentType, true);
  const String fileName = filePath.substring(filePath.lastIndexOf('/') + 1);
  response->addHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
  request->send(response);
}

const char kPageTest[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html><head>
  <title>Test Page</title>
</head>
<body>
  Test Page Placeholder Content
</body>
</html>)rawliteral";

void handleTestPage(AsyncWebServerRequest *request)
{
  request->send(200, "text/html", FPSTR(kPageTest));
}

}
