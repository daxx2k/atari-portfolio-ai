#pragma once
#include <Arduino.h>
#include "cJSON.h"

bool visionRequested(const String &prompt);
// The data URL is allocated in PSRAM; the caller owns it and must free it.
bool visionCapture(char *&url,size_t &jpegBytes,unsigned long &captureMs);
// Only the current user message receives an image. History stays text-only.
bool visionAttach(cJSON *request,const String &prompt,const char *url);
