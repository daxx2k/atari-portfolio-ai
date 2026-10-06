#pragma once
#include <Arduino.h>
#include "cJSON.h"
bool conversationInit(String (*ask)(const String&,bool&),bool (*network)());
bool conversationStart(const String &key);
void conversationStop();
bool conversationBusy();
bool conversationActive();
void conversationSendFrame(Stream &out,bool force=false,bool paced=false);
void conversationDelta(const String &delta);
void conversationInput(cJSON *request,const String &prompt);
void conversationStatus();
void conversationUsbLog(const char *line);
