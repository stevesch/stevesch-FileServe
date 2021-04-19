#ifndef STEVESCH_FILESERVE_INTERNAL_FILESERVE_H_
#define STEVESCH_FILESERVE_INTERNAL_FILESERVE_H_
#include <Arduino.h>

class AsyncWebServer;

namespace stevesch
{
namespace FileServe {
  void begin(AsyncWebServer& server);
}
}

#endif
