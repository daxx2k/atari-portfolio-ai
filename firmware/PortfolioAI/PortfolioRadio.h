#pragma once
#include <Arduino.h>
bool radioInit();
void radioSelect(int direction);
void radioStop();
bool radioActive();
String radioStatus();
String radioDiagnostics();

int radioStation();
void radioSetStation(int oneBased);

int radioState();

constexpr unsigned RADIO_PAUSE_SPEECH=1, RADIO_PAUSE_TALK=2;
bool radioPause(unsigned reason);
void radioResume(unsigned reason);
unsigned radioPaused();
