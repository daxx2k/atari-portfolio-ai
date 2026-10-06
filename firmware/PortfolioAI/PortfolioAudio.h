#pragma once
#include <Arduino.h>

// Latch battery power before USB, NVS, camera or audio initialization.
bool boardPowerInit();
void boardPowerStatus();
bool audioInit();
void audioCue(bool received);
void audioCancel();
bool audioSpeak(const String &text, const String &key);
void audioSetVoice(bool enabled);
void audioSetRobot(bool enabled);
bool audioSetVolume(int value);
bool audioVoiceEnabled();
bool audioRobotEnabled();
int audioVolume();
bool audioReady();
bool audioBusy();
bool audioPlaying();
bool audioPowerOff();
void audioMicStatus();
// Caller owns PCM. fixedSeconds=0 uses voice activity and stops after silence.
int16_t *audioCapture(size_t &samples, bool (*keepGoing)(), void (*heard)(), unsigned fixedSeconds=0);
uint32_t audioSamples();
uint32_t audioCues();
int audioHttpCode();
int audioError();
int audioQueueDepth();
bool audioRadioBegin();
bool audioRadioWrite(const int16_t *pcm,size_t samples);
void audioRadioEnd();

bool audioRadioPlaying();
