#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "cJSON.h"
void contextInit(Preferences &settings);
String contextSummary();
String contextInstructions();
bool contextNeedsSearch(const String &prompt);
void contextSearchTool(cJSON *request, bool required);
