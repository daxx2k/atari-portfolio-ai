#pragma once
#include <Arduino.h>

bool cameraInit();
bool cameraReady();
int cameraErrorCode();
unsigned long cameraFrameCount();
// Fixed 12-byte header then 1920-byte monochrome bitmap, no ASCII/EOT encoding.
// The next frame is sent only in response to a new /frame request.
void cameraSendFrame(Stream &output,bool paced=false,bool lowResolution=false);
void cameraSendJPEG(Stream &output);
