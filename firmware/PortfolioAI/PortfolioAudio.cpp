#include "PortfolioAudio.h"
#include "PortfolioConversation.h"
#include "AudioDSP.h"
#include "ESP_I2S.h"
#include "Wire.h"
#include "es8311.h"
#include "es7210.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <atomic>

namespace {
I2SClass audio;
es8311_handle_t codec = nullptr;
SemaphoreHandle_t outputLock = nullptr;
QueueHandle_t speechQueue = nullptr;
std::atomic<bool> ready{false}, busy{false}, voice{true}, robot{true};
std::atomic<bool> playing{false};
std::atomic<bool> radioOutput{false};
std::atomic<unsigned> outstanding{0};
std::atomic<int> micRms{0},micThreshold{0},micChannel{0};
std::atomic<int> volume{40}, lastHttp{0}, lastError{0};
std::atomic<uint32_t> generation{0}, sampleCount{0}, cueCount{0};
SourceLeveler radioLevel(false);
float outputGain=.4f; // only the owner of outputLock updates this ramp
uint8_t expanderState = 0x60; // EXIO5 BAT_EN, EXIO6 LED; EXIO3 camera PWDN stays LOW.
bool powerInitialized=false;
constexpr size_t MAX_PCM = AUDIO_RATE * 2 * 75; // bounded to 75 s, in PSRAM
struct SpeechJob { char *text; char *key; uint32_t generation; };
struct PCMBody {
  uint8_t *data; uint32_t generation;
  std::atomic<size_t> size{0};
  std::atomic<bool> done{false}, abort{false}, overflow{false};
  std::atomic<int> code{0};
  esp_err_t error=ESP_OK;
  esp_http_client_handle_t http=nullptr;
  uint32_t began=0, first=0, complete=0;
  char contentType[64]={};
  bool completeData=false;
};

bool expanderWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x24); Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}
void amplifier(bool on) {
  if (on) expanderState |= 0x10; else expanderState &= ~0x10;
  expanderWrite(3, expanderState);
}
void releaseOutput() {
  int16_t zeros[512] = {};
  audio.write((uint8_t*)zeros, sizeof(zeros));
  delay(80); // let the DMA drain before switching off the amplifier
  amplifier(false);
  xSemaphoreGive(outputLock);
}
void freeJob(SpeechJob &job) {
  free(job.text);
  if (job.key) {
    // Do not leave an extra plaintext key copy in a freed queue allocation.
    volatile char *p = job.key;
    for (size_t n=strlen(job.key); n; --n) *p++=0;
    free(job.key);
  }
}
esp_err_t pcmEvent(esp_http_client_event_t *event) {
  auto *body = (PCMBody*)event->user_data;
  if (body->abort.load() || generation.load()!=body->generation || !voice.load()) return ESP_FAIL;
  if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && !strcasecmp(event->header_key,"Content-Type")) {
    snprintf(body->contentType,sizeof(body->contentType),"%s",event->header_value);
  }
  if (event->event_id!=HTTP_EVENT_ON_DATA) return ESP_OK;
  const int code=esp_http_client_get_status_code(event->client);
  body->code.store(code);
  if (code!=200) return ESP_OK; // never play an HTTP error body as PCM
  const size_t size=body->size.load();
  if (event->data_len<0 || size+(size_t)event->data_len>MAX_PCM) {
    body->overflow.store(true);return ESP_FAIL;
  }
  if (!size && event->data_len) body->first=millis()-body->began;
  memcpy(body->data+size,event->data,event->data_len);
  // Publish only after the bytes are copied; the consumer reads disjoint data.
  body->size.store(size+event->data_len);
  delay(1); // let Wi-Fi/TLS and the independent audio consumer progress
  return ESP_OK;
}
void speechDownloadTask(void *argument) {
  auto *body=(PCMBody*)argument;
  body->error=esp_http_client_perform(body->http);
  body->code.store(esp_http_client_get_status_code(body->http));
  body->completeData=esp_http_client_is_complete_data_received(body->http);
  esp_http_client_cleanup(body->http);
  body->complete=millis()-body->began;
  body->done.store(true); // owner must wait for this before freeing body/payload
  vTaskDelete(nullptr);
}
void performSpeech(const SpeechJob &job) {
  if (generation.load()!=job.generation || !voice.load()) return;
  busy.store(true); sampleCount.store(0); lastHttp.store(0); lastError.store(0);
  auto *pcm = (uint8_t*)ps_malloc(MAX_PCM);
  if (!pcm) { lastError.store(1); busy.store(false); return; }
  cJSON *request = cJSON_CreateObject();
  cJSON_AddStringToObject(request,"model","gpt-4o-mini-tts");
  cJSON_AddStringToObject(request,"voice","onyx");
  cJSON_AddStringToObject(request,"input",job.text);
  cJSON_AddStringToObject(request,"response_format","pcm");
  cJSON_AddNumberToObject(request,"speed",1.0);
  cJSON_AddStringToObject(request,"instructions",robot.load()?
    "Speak in English as SKYNET, a fictional synthetic cybernetic machine inspired by 1980s Terminator science fiction. Use a very deep bass register, firm chest resonance, flat pitch, minimal emotion and short clipped syllables. Sound cold, mechanical and controlled, never cheerful or conversational. Keep consonants crisp and words clearly intelligible, at a normal speaking pace without long dramatic pauses. Speak exactly the supplied text. Do not add introductions, sound effects, or extra words.":
    "Speak clearly in English in a calm, steady low voice at a normal speaking pace. Speak exactly the supplied text, without introductions or extra words.");
  char *payload = cJSON_PrintUnformatted(request);
  cJSON_Delete(request);
  PCMBody body{}; body.data=pcm; body.generation=job.generation; body.began=millis();
  esp_http_client_config_t config = {};
  config.url="https://api.openai.com/v1/audio/speech";
  config.method=HTTP_METHOD_POST;
  config.timeout_ms=45000;
  config.crt_bundle_attach=esp_crt_bundle_attach;
  config.event_handler=pcmEvent;
  config.user_data=&body;
  config.disable_auto_redirect=true;
  config.buffer_size=4096;
  esp_http_client_handle_t http = payload ? esp_http_client_init(&config) : nullptr;
  uint32_t startMs=0, underruns=0;
  bool started=false, failed=false;
  if (http) {
    String authorization="Bearer "; authorization+=job.key;
    esp_http_client_set_header(http,"Authorization",authorization.c_str());
    esp_http_client_set_header(http,"Content-Type","application/json");
    esp_http_client_set_post_field(http,payload,strlen(payload));
    body.http=http;
    if (xTaskCreatePinnedToCore(speechDownloadTask,"speech-download",16384,&body,2,nullptr,1)!=pdPASS) {
      esp_http_client_cleanup(http);body.error=ESP_ERR_NO_MEM;body.done.store(true);
    }
  } else {body.error=ESP_ERR_NO_MEM;body.done.store(true);}
  constexpr size_t PREBUFFER=AUDIO_RATE; // 500 ms PCM, not the whole utterance
  while (!body.done.load() && body.size.load()<PREBUFFER && generation.load()==job.generation && voice.load()) delay(5);
  if (generation.load()!=job.generation || !voice.load()) lastError.store(5);
  else if (body.code.load()!=200 || body.size.load()<2 || body.overflow.load()) lastError.store(2);
  else if (xSemaphoreTake(outputLock,pdMS_TO_TICKS(1000))==pdTRUE) {
    const bool robotic=robot.load();
    size_t initial=std::min(PREBUFFER,body.size.load())/2;
    double gain=speechGain((int16_t*)pcm,initial,robotic,[](){delay(1);});
    // Leading silence must not permanently leave the utterance too quiet.
    if (gain==1) gain=3;
    playing.store(true);amplifier(true);started=true;startMs=millis()-body.began;
    char startLog[160];snprintf(startLog,sizeof(startLog),"SPEECH_START {\"first_ms\":%lu,\"start_ms\":%lu,\"download_done\":%s}",body.first,startMs,body.done.load()?"true":"false");conversationUsbLog(startLog);
    SourceLeveler speechLevel(true);
    size_t consumed=0;
    int16_t block[512];
    while (generation.load()==job.generation && voice.load()) {
      size_t available=body.size.load()/2;
      size_t count=std::min((size_t)512,available-consumed);
      if (!count) {
        if (body.done.load()) break;
        ++underruns;
        // Stop DMA replaying stale samples if the network temporarily stalls.
        memset(block,0,sizeof(block));audio.write((uint8_t*)block,sizeof(block));delay(1);continue;
      }
      const auto *samples=(const int16_t*)pcm;
      for(size_t i=0;i<count;++i) block[i]=leveledSpeech(samples[consumed+i],consumed+i,robotic,gain);
      speechLevel.process(block,count);
      applyOutputVolume(block,count,volume.load(),outputGain);
      size_t offset=0;
      while(offset<count*2) {
        size_t n=audio.write((uint8_t*)block+offset,count*2-offset);
        if(!n){lastError.store(4);failed=true;break;}
        offset+=n;
      }
      sampleCount.fetch_add(offset/2);consumed+=offset/2;
      if(failed)break;
      delay(1);
    }
    releaseOutput();playing.store(false);
    if(generation.load()!=job.generation || !voice.load())lastError.store(5);
  } else lastError.store(4);
  if(lastError.load())body.abort.store(true);
  while(!body.done.load()) delay(5); // producer owns http and may still be copying
  const int code=body.code.load();lastHttp.store(code);
  if(!lastError.load() && (body.error!=ESP_OK || code!=200 || !body.completeData || body.overflow.load() || body.size.load()%2))lastError.store(body.overflow.load()?3:2);
  if(payload)cJSON_free(payload);
  char diagnostic[320];snprintf(diagnostic,sizeof(diagnostic),"SPEECH {\"http\":%d,\"bytes\":%u,\"samples\":%lu,\"error\":%d,\"first_ms\":%lu,\"start_ms\":%lu,\"download_ms\":%lu,\"underruns\":%lu,\"streamed\":%s}",code,(unsigned)body.size.load(),(unsigned long)sampleCount.load(),lastError.load(),body.first,startMs,body.complete,underruns,started && startMs<body.complete?"true":"false");conversationUsbLog(diagnostic);
  char formatLog[160];snprintf(formatLog,sizeof(formatLog),"SPEECH_FORMAT {\"type\":\"%s\",\"complete\":%s,\"prefix\":\"%02x%02x%02x%02x\"}",body.contentType,body.completeData?"true":"false",pcm[0],pcm[1],pcm[2],pcm[3]);conversationUsbLog(formatLog);
  free(pcm);
  busy.store(false);
}
void speechTask(void*) {
  SpeechJob job{};
  for (;;) {
    if (xQueueReceive(speechQueue,&job,portMAX_DELAY)==pdTRUE) {
      performSpeech(job);
      freeJob(job);
      outstanding.fetch_sub(1);
    }
  }
}
}

bool boardPowerInit() {
  if (powerInitialized) return true;
  pinMode(15,INPUT_PULLUP);
  if (!Wire.begin(8,7,100000)) return false;
  for (unsigned attempt=0;attempt<10;++attempt) {
    // EXIO7 is the charger status input, not an output.
    if (expanderWrite(2,0x7f) && expanderWrite(3,expanderState)) {
      powerInitialized=true;return true;
    }
    delay(20);
  }
  return false;
}
void boardPowerStatus() {
  // Manufacturer ADC register is a little-endian word. Report raw data
  // without inventing a voltage conversion for the expander firmware.
  auto readRegister=[](uint8_t reg,uint8_t *data,size_t size)->bool {
    Wire.beginTransmission(0x24);Wire.write(reg);
    if (Wire.endTransmission(false)!=0) return false;
    if (Wire.requestFrom((uint8_t)0x24,(uint8_t)size)!=size) return false;
    for (size_t i=0;i<size;++i) data[i]=Wire.read();
    return true;
  };
  uint8_t input=0,adc[2]={};
  bool inputOk=readRegister(4,&input,1);
  bool adcOk=readRegister(6,adc,2);
  Serial.printf("POWER_STATUS {\"initialized\":%s,\"uptime_ms\":%lu,\"pwr_pressed\":%s,\"output_command\":%u,\"input_ok\":%s,\"input_raw\":%u,\"adc_ok\":%s,\"battery_adc_raw\":%u}\n",
    powerInitialized?"true":"false",millis(),digitalRead(15)==LOW?"true":"false",expanderState,
    inputOk?"true":"false",input,adcOk?"true":"false",(unsigned)(adc[0]|adc[1]<<8));
}
bool audioInit() {
  if (!boardPowerInit()) return false;
  delay(100);
  audio.setPins(11,12,14,-1,10);
  bool bus=audio.begin(I2S_MODE_STD,AUDIO_RATE,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO,I2S_STD_SLOT_LEFT);
  audio.setTimeout(1000);
  codec=es8311_create(I2C_NUM_0,ES8311_ADDRRES_0);
  if (!bus || !codec) return false;
  es8311_clock_config_t clock{};
  clock.mclk_from_mclk_pin=true;
  clock.mclk_frequency=AUDIO_RATE*256;
  clock.sample_frequency=AUDIO_RATE;
  esp_err_t result=es8311_init(codec,&clock,ES8311_RESOLUTION_16,ES8311_RESOLUTION_16);
  if (result==ESP_OK) result=es8311_voice_volume_set(codec,75,nullptr);
  if (result==ESP_OK) result=es8311_microphone_config(codec,false);
  if (result!=ESP_OK) return false;
  outputLock=xSemaphoreCreateMutex();
  speechQueue=xQueueCreate(1,sizeof(SpeechJob));
  if (!outputLock || !speechQueue) return false;
  if (xTaskCreatePinnedToCore(speechTask,"robot-voice",16384,nullptr,1,nullptr,0)!=pdPASS) return false;
  ready.store(true);
  return true;
}
void audioCancel() { generation.fetch_add(1); }
void audioCue(bool received) {
  if (radioOutput.load() || !ready.load() || xSemaphoreTake(outputLock,pdMS_TO_TICKS(500))!=pdTRUE) return;
  amplifier(true);
  int16_t block[240];
  bool failed=false;
  for (unsigned start=0; start<CUE_SAMPLES; start+=240) {
    unsigned count=std::min(240u,CUE_SAMPLES-start);
    for (unsigned i=0; i<count; ++i) block[i]=cyberCueSample(start+i,received);
    applyOutputVolume(block,count,volume.load(),outputGain);
    if (audio.write((uint8_t*)block,count*2)!=count*2) { failed=true; break; }
  }
  releaseOutput();
  if (!failed) cueCount.fetch_add(1);
}
bool audioSpeak(const String &text, const String &key) {
  if (radioOutput.load() || !ready.load() || !voice.load() || text.isEmpty() || key.isEmpty()) return false;
  SpeechJob job{strdup(text.c_str()),strdup(key.c_str()),generation.load()};
  if (!job.text || !job.key) { freeJob(job); return false; }
  SpeechJob stale{};
  if (xQueueReceive(speechQueue,&stale,0)==pdTRUE) { freeJob(stale); outstanding.fetch_sub(1); }
  outstanding.fetch_add(1);
  if (xQueueSend(speechQueue,&job,0)!=pdTRUE) { outstanding.fetch_sub(1); freeJob(job); return false; }
  return true;
}
void audioSetVoice(bool enabled) { voice.store(enabled); if (!enabled) audioCancel(); }
void audioSetRobot(bool enabled) { robot.store(enabled); }
bool audioSetVolume(int value) {
  if (!ready.load() || value<0 || value>100) return false;
  volume.store(value); // the output worker applies it on the next PCM buffer
  return true;
}

bool audioVoiceEnabled() { return voice.load(); }
bool audioRobotEnabled() { return robot.load(); }
int audioVolume() { return volume.load(); }
bool audioRadioBegin() {
  audioCancel();
  if(!ready.load()||audioBusy()||xSemaphoreTake(outputLock,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
  radioLevel.reset();radioOutput.store(true);amplifier(true);xSemaphoreGive(outputLock);return true;
}
bool audioRadioWrite(const int16_t *pcm,size_t count) {
  if(!radioOutput.load()||xSemaphoreTake(outputLock,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
  int16_t block[512];size_t total=0;
  while(total<count){
    unsigned n=std::min((size_t)512,count-total);
    memcpy(block,pcm+total,n*2);
    radioLevel.process(block,n);
    applyOutputVolume(block,n,volume.load(),outputGain);
    size_t offset=0;
    while(offset<n*2){
      size_t written=audio.write((uint8_t*)block+offset,n*2-offset);
      if(!written){xSemaphoreGive(outputLock);return false;}
      offset+=written;
    }
    total+=n;sampleCount.fetch_add(n);
  }
  xSemaphoreGive(outputLock);delay(1);return true;
}
void audioRadioEnd() {
  radioOutput.store(false);
  if(xSemaphoreTake(outputLock,pdMS_TO_TICKS(1200))==pdTRUE)releaseOutput();
}
bool audioReady() { return ready.load(); }
bool audioBusy() { return busy.load() || outstanding.load()!=0; }
bool audioPlaying() { return playing.load(); }
bool audioRadioPlaying(){return radioOutput.load();}
bool audioPowerOff() {
  if (!outputLock || xSemaphoreTake(outputLock,pdMS_TO_TICKS(1500))!=pdTRUE) return false;
  amplifier(false);
  expanderState=(expanderState & ~0x70) | 0x08;
  bool ok=expanderWrite(3,expanderState);
  xSemaphoreGive(outputLock);return ok;
}
void audioMicStatus() {
  Serial.printf("MIC_LEVEL {\"rms\":%d,\"threshold\":%d,\"channel\":%d}\n",micRms.load(),micThreshold.load(),micChannel.load());
}
uint32_t audioSamples() { return sampleCount.load(); }
uint32_t audioCues() { return cueCount.load(); }
int audioHttpCode() { return lastHttp.load(); }
int audioError() { return lastError.load(); }
int audioQueueDepth() { return speechQueue?uxQueueMessagesWaiting(speechQueue):0; }

int16_t *audioCapture(size_t &samples, bool (*keepGoing)(), void (*heard)(), unsigned fixedSeconds) {
  samples=0;
  if (!audioReady() || audioBusy() || xSemaphoreTake(outputLock,pdMS_TO_TICKS(500))!=pdTRUE) return nullptr;
  constexpr size_t RATE=16000, MAX=RATE*10, PRE=RATE/5;
  auto *pcm=(int16_t*)ps_malloc(MAX*2);
  auto *pre=(int16_t*)ps_malloc(PRE*2);
  amplifier(false);
  audio.end(); audio.setPins(11,12,14,13,10);
  bool ok=pcm && pre && audio.begin(I2S_MODE_STD,RATE,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
  audio.setTimeout(200);
  static es7210_dev_handle_t mic=nullptr;
  es7210_i2c_config_t ic{}; ic.i2c_addr=0x40;
  if (ok && !mic) ok=es7210_new_codec(&ic,&mic)==ESP_OK;
  es7210_codec_config_t mc{};
  mc.i2s_format=ES7210_I2S_FMT_I2S; mc.mclk_ratio=256; mc.sample_rate_hz=RATE;
  mc.bit_width=ES7210_I2S_BITS_16B; mc.mic_bias=ES7210_MIC_BIAS_2V87;
  mc.mic_gain=ES7210_MIC_GAIN_30DB; mc.flags.tdm_enable=false;
  if (ok) ok=es7210_config_codec(mic,&mc)==ESP_OK && es7210_config_volume(mic,0)==ESP_OK;
  int16_t stereo[256];
  // Let the clocks and ADC settle, then use the louder of the two mic channels.
  uint64_t energy[2]={};
  for (unsigned block=0;ok && block<50 && keepGoing();++block) {
    size_t got=audio.readBytes((char*)stereo,sizeof(stereo));
    if (got!=sizeof(stereo)) { ok=false; break; }
    if (block>=30) for (unsigned i=0;i<256;++i) energy[i&1]+=(int32_t)stereo[i]*(int32_t)stereo[i];
  }
  unsigned channel=energy[1]>energy[0]?1:0;
  size_t preAt=0,preCount=0; unsigned high=0,voiced=0;
  bool triggered=fixedSeconds!=0;
  float floor=50,previous[2]={},filtered[2]={};
  unsigned calibrate=0;
  unsigned long started=millis(),lastVoice=started;
  while (ok && keepGoing() && samples<MAX && millis()-started<60000) {
    size_t got=audio.readBytes((char*)stereo,sizeof(stereo));
    if (got!=sizeof(stereo)) { ok=false; break; }
    int16_t channels[2][128];uint64_t power[2]={};
    for (unsigned i=0;i<128;++i) {
      for(unsigned c=0;c<2;++c) {
        float x=stereo[i*2+c];filtered[c]=.995f*(filtered[c]+x-previous[c]);previous[c]=x;
        channels[c][i]=(int16_t)constrain(filtered[c],-32768.0f,32767.0f);
        power[c]+=(int32_t)channels[c][i]*(int32_t)channels[c][i];
      }
    }
    // Follow the stronger microphone, rather than fixing a quiet channel before
    // the user starts speaking. Remove DC before comparing channel energy.
    channel=power[1]>power[0]?1:0;const auto &mono=channels[channel];
    float rms=sqrtf((float)power[channel]/128);
    micRms.store(rms);micChannel.store(channel);
    if (!fixedSeconds && calibrate<32) { floor=.9f*floor+.1f*std::min(rms,80.0f); ++calibrate; continue; }
    float threshold=std::max(160.0f,floor*2.5f);micThreshold.store(threshold);
    bool loud=rms>threshold;
    if (!triggered) {
      for (int16_t s:mono) { pre[preAt]=s;preAt=(preAt+1)%PRE;preCount=std::min(PRE,preCount+1); }
      if (loud) ++high; else { if(high)--high; floor=.998f*floor+.002f*rms; }
      if (high>=6) {
        for (size_t i=0;i<preCount;++i) pcm[samples++]=pre[(preAt+PRE-preCount+i)%PRE];
        triggered=true;lastVoice=millis();if (heard) heard();
      }
      continue;
    }
    if (loud) { lastVoice=millis();voiced+=128; }
    for (int16_t s:mono) if (samples<MAX) pcm[samples++]=s;
    if (fixedSeconds && samples>=std::min(MAX,RATE*fixedSeconds)) break;
    if (!fixedSeconds && millis()-lastVoice>750) break;
  }
  bool cancelled=!keepGoing();
  audio.end();audio.setPins(11,12,14,-1,10);
  bool restored=audio.begin(I2S_MODE_STD,AUDIO_RATE,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO,I2S_STD_SLOT_LEFT);
  audio.setTimeout(1000);
  es8311_clock_config_t clock{};clock.mclk_from_mclk_pin=true;
  clock.mclk_frequency=AUDIO_RATE*256;clock.sample_frequency=AUDIO_RATE;
  restored=restored && es8311_init(codec,&clock,ES8311_RESOLUTION_16,ES8311_RESOLUTION_16)==ESP_OK;
  restored=restored && es8311_voice_volume_set(codec,75,nullptr)==ESP_OK;
  restored=restored && es8311_microphone_config(codec,false)==ESP_OK;
  ready.store(restored);amplifier(false);xSemaphoreGive(outputLock);free(pre);
  if (!ok || !restored || cancelled || (!fixedSeconds && voiced<RATE/6)) {
    free(pcm);samples=0;return nullptr;
  }
  return pcm;
}
