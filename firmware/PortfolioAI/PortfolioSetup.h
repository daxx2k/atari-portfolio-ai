#pragma once
#include <Arduino.h>
#include <Preferences.h>
// Stages the entire file. No values are saved before a valid END transaction.
bool setupLine(const String &line, Preferences &settings, bool busy, String &result, bool &changed);
void setupExpire();
