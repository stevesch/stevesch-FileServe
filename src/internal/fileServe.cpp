#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <algorithm>

#include "fileServe.h"

namespace
{
void handleListFiles(AsyncWebServerRequest *request);
void handleListFilesJson(AsyncWebServerRequest *request);
void handleMore(AsyncWebServerRequest *request);
void handleRemove(AsyncWebServerRequest *request);
void handleServeFile(AsyncWebServerRequest *request);
void handleTestPage(AsyncWebServerRequest *request);

FS* sFileSys = &SPIFFS;
}

namespace stevesch {
namespace FileServe {
  int sDisplaySizeMax = 65536;

  void begin(AsyncWebServer& server, FS* optionalFileSys)
  {
    if (optionalFileSys) {
      sFileSys = optionalFileSys;
    }
    server.on("/ls", HTTP_GET, handleListFiles);
  server.on("/ls.json", HTTP_GET, handleListFilesJson);
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
  // Buffered escaper: build up escaped text in a stack buffer and write
  // larger chunks to the response to avoid many small heap allocations.
  const size_t OUT_BUF = 256;
  char out[OUT_BUF];
  size_t oi = 0;

  auto flushOut = [&](void) {
    if (oi) {
      response->write(reinterpret_cast<const uint8_t*>(out), oi);
      oi = 0;
      // allow background tasks (TCP, LED updates) to run and drain buffers
      yield();
    }
  };

  for (size_t i = 0; i < len; ++i) {
    char ch = static_cast<char>(data[i]);
    const char* rep = nullptr;
    size_t repLen = 0;
    switch (ch) {
      case '>': rep = "&gt;"; repLen = 4; break;
      case '<': rep = "&lt;"; repLen = 4; break;
      case '"': rep = "&quot;"; repLen = 6; break;
      case '\'': rep = "&apos;"; repLen = 6; break;
      case '\r':
        // drop carriage returns; rely on the newline that follows
        rep = nullptr; repLen = 0; break;
      default:
        // single character
        if (oi + 1 >= OUT_BUF) {
          flushOut();
        }
        out[oi++] = ch;
        continue;
    }

    // If replacement present, ensure it fits in out, otherwise flush first
    if (rep && repLen) {
      if (oi + repLen >= OUT_BUF) {
        flushOut();
      }
      // copy rep
      for (size_t j = 0; j < repLen; ++j) out[oi++] = rep[j];
    }
  }
  flushOut();
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

const size_t kReadChunkMax = 511;
uint8_t buf[kReadChunkMax + 1];

// simple URL-encoder for use when constructing preview <img src="/dl?path=...">
static void urlEncode(const char* src, char* dst, size_t dstSize)
{
  size_t di = 0;
  for (size_t si = 0; src[si] != '\0' && di + 4 < dstSize; ++si) {
    unsigned char c = static_cast<unsigned char>(src[si]);
    // unreserved characters according to RFC3986
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      dst[di++] = c;
    } else {
      // percent-encode
      if (di + 3 >= dstSize) break;
      static const char hex[] = "0123456789ABCDEF";
      dst[di++] = '%';
      dst[di++] = hex[(c >> 4) & 0xF];
      dst[di++] = hex[c & 0xF];
    }
  }
  dst[di] = '\0';
}

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

  // avoid allocating a large String for the path when possible
  String filePathArg = request->arg("path");
  const char* filePath = filePathArg.c_str();

  // smaller response buffer to reduce heap usage while streaming
  // smaller response buffer to reduce heap usage while streaming
  AsyncResponseStream* response = request->beginResponseStream("text/html", 1024);
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
  Serial.printf("/more requested path (raw): '%s'\n", filePath);
  // try path as given; if not found, try with a leading '/'
  if (sFileSys->exists(filePath)) {
    f = sFileSys->open(filePath, FILE_READ);
  } else {
    String alt = filePathArg;
    if (alt.length() && alt.charAt(0) != '/') alt = String("/") + alt;
    if (sFileSys->exists(alt.c_str())) {
      Serial.printf("/more: trying alternative path '%s'\n", alt.c_str());
      f = sFileSys->open(alt.c_str(), FILE_READ);
    }
  }
  size_t fileSize = f ? f.size() : 0;
  yield();

  // If the file looks like an image (common types), don't stream its raw
  // binary into the HTML preview. Instead embed it via an <img> that
  // references the download endpoint. This avoids escaping/streaming binary
  // data into the HTML page (which can crash or consume lots of memory).
  if (f) {
    String pathLower = String(filePath);
    pathLower.toLowerCase();
    bool isImage = false;
    if (pathLower.endsWith(".gif") || pathLower.endsWith(".png") || pathLower.endsWith(".jpg") || pathLower.endsWith(".jpeg") || pathLower.endsWith(".bmp") || pathLower.endsWith(".webp") || pathLower.endsWith(".svg")) {
      isImage = true;
    }
    if (isImage) {
      // Close the file handle; we'll let /dl stream the binary when the browser
      // requests it. Build a tiny HTML page that embeds the image using a
      // URL-encoded path.
      f.close();

  char enc[256];
      urlEncode(filePath, enc, sizeof(enc));

      response->print(F("<div class=\"content\">"));
      response->print(F("<div style=\"text-align:center\">"));
      response->print(F("<img id=\"preview\" style=\"max-width:100%;height:auto\">"));
      response->print(F("</div>"));
      // set src via JS to ensure correct encoding in case of odd characters
      response->print("<script>document.getElementById('preview').src='/dl?path=");
      response->print(enc);
      response->print("';</script>");

      response->print(FPSTR(kPageTemplatePostBody));
      yield();
      request->send(response);
      return;
    }
  }

  if (fileSize > stevesch::FileServe::sDisplaySizeMax)
  {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), " (truncated-- size %u exceeds display size of %d)", (unsigned)fileSize, stevesch::FileServe::sDisplaySizeMax);
    response->print(tmp);
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

  Serial.printf("Reading %d bytes from %s\n", (int)n, filePath);

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
  Serial.printf("### Unable to read file '%s'\n", filePath);
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
  Serial.printf("/rm requested path: '%s'\n", filePath.c_str());
  // try removing with given path, otherwise try with leading slash
  if (sFileSys->exists(filePath)) {
    sFileSys->remove(filePath);
  } else {
    if (filePath.length() && filePath.charAt(0) != '/') {
      String alt = String("/") + filePath;
      if (sFileSys->exists(alt.c_str())) {
        Serial.printf("/rm trying alternative path: '%s'\n", alt.c_str());
        sFileSys->remove(alt.c_str());
      }
    }
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
  // keep the response buffer small to avoid big heap allocations which
  // can fragment memory during other real-time tasks (LED updates, websockets)
  const size_t bufSize = 2048;

  AsyncResponseStream *response = request->beginResponseStream("text/html", bufSize);
  response->setCode(200);

  response->print(FPSTR(kPagePreTitle));
  response->print("File List");
  response->print(FPSTR(kPagePostTitle));

  response->print("<ul>");
  response->print(FPSTR(kMainIcon));
  response->print("<li><span class=\"hdr\">Files:</span></li></ul>");

  // Serve a small HTML page that fetches a compact JSON file list from /ls.json
  // and renders the table client-side. This keeps server memory usage low.
  response->print("<div class=\"content\">\n");
  response->print("<div id=\"filelist\">Loading files...</div>\n");
  // Client-side script: fetch JSON and build table similar to previous output
  response->print("<script>\n");
  // JS: size formatter and row renderer
  const char jsFmt[] = "function fmtSize(n){if(n<4096)return n+'B'; if(n<1024*1024) return (Math.round(n/1024*100)/100).toFixed(2)+'K'; return (Math.round(n/(1024*1024)*100)/100).toFixed(2)+'M';}\n";
  const char jsMakeRow[] =
    "function makeRow(it){"
    "var p = encodeURIComponent(it.path);"
    "return '<tr>' +"
      "'<td><a download href=\"/dl?path=' + p + '\"><i class=\"dlicon fas fa-download\"></i></a></td>' +"
      "'<td>' + fmtSize(it.size) + '</td>' +"
      "'<td><a href=\"/more?path=' + p + '\">' + it.path + '</a></td>' +"
      "'<td><a href=\"/rm?path=' + p + '\"><i class=\"dlicon fas fa-trash-alt\"></i></a></td>' +"
      "'</tr>'; }\n";
  response->print(jsFmt);
  response->print(jsMakeRow);

  // JS: fetch and render full list (no server-side truncation)
  response->print("fetch('/ls.json').then(r=>r.json()).then(list=>{var out='<table>'; if(list.length==0){out+='<tr><td><i>No files</i></td></tr>';} else {for(var i=0;i<list.length;i++){out+=makeRow(list[i]);}} out+='</table>'; document.getElementById('filelist').innerHTML=out}).catch(e=>{document.getElementById('filelist').innerText='Error loading file list'; console.error(e);});\n");
  response->print("</script>\n");
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
  Serial.printf("/dl requested path: '%s'\n", filePath.c_str());
  // check existence, try alternative with leading slash if necessary
  if (!sFileSys->exists(filePath)) {
    if (filePath.length() && filePath.charAt(0) != '/') {
      String alt = String("/") + filePath;
      if (sFileSys->exists(alt.c_str())) {
        filePath = alt;
      }
    }
  }
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

// Minimal JSON string escaper (writes to provided buffer)
static void jsonEscapeToBuf(const char* src, char* dst, size_t dstSize)
{
  size_t di = 0;
  for (size_t si = 0; src[si] != '\0' && di + 2 < dstSize; ++si) {
    char c = src[si];
    if (c == '"' || c == '\\') {
      if (di + 2 >= dstSize) break;
      dst[di++] = '\\';
      dst[di++] = c;
    } else if (c >= 0 && c >= 0x20) {
      dst[di++] = c;
    } else {
      // replace control chars with space
      dst[di++] = ' ';
    }
  }
  dst[di] = '\0';
}

// Stream a compact JSON array of file objects: [{"path":"...","size":123}, ...]
void handleListFilesJson(AsyncWebServerRequest *request)
{
  Serial.println("Listing files (json)...");

  if (!validateFileSys()) {
    postFsError(request);
    return;
  }

  File root = sFileSys->open("/", FILE_READ);
  if (!root) {
    request->send(500, "application/json", "{\"error\":\"Failed to open directory\"}");
    return;
  }

  // small response buffer to avoid large allocations; we'll stream entries
  AsyncResponseStream *response = request->beginResponseStream("application/json", 1024);
  response->setCode(200);

  response->print("[");

  int numListed = 0;
  File file = root.openNextFile();
  bool first = true;
  while (file) {
    // prefer full path() so client links (which use the JSON path) match the FS
    String fp = file.path();
    const char* filePathC = fp.c_str();
    size_t fileSize = file.size();

    if (!first) response->print(",");
    first = false;

    // escape path into small stack buffer
    char escPath[256];
    jsonEscapeToBuf(filePathC, escPath, sizeof(escPath));

    char itemBuf[384];
    snprintf(itemBuf, sizeof(itemBuf), "{\"path\":\"%s\",\"size\":%u}", escPath, (unsigned)fileSize);
    response->print(itemBuf);

    file.close();
    numListed++;
    yield();
    file = root.openNextFile();
  }
  root.close();

  response->print("]");
  request->send(response);

  Serial.println("File listing (json) complete.");
}

}
