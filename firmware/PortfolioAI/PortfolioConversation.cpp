#include "PortfolioConversation.h"
#include "PortfolioFrameTransport.h"
#include "PortfolioAudio.h"
#include "CodecAssets.h"
#include "PortfolioText.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <atomic>
#include <cstring>

namespace {
enum Phase:uint16_t { STOPPED,LISTENING,HEARING,TRANSCRIBING,THINKING,SPEAKING,ERROR };
SemaphoreHandle_t lock=nullptr;
SemaphoreHandle_t usbLock=nullptr;
TaskHandle_t task=nullptr;
String (*askAI)(const String&,bool&)=nullptr;
bool (*networkReady)()=nullptr;
std::atomic<bool> active{false},working{false};
std::atomic<unsigned long> lastPoll{0};
String secret,logText,draft,history[8];
unsigned historyCount=0;
Phase phase=STOPPED;
uint16_t revision=1,lastSent=0;
unsigned long turns=0;int sttHttp=0;
const char *labels[]={"LINK OFFLINE","MIC ACTIVE","VOICE DETECTED","DECODING","ANALYSIS","TRANSMITTING","LINK ERROR"};

String safe(const String &s) {return portfolioAscii(s);}
void setPhase(Phase next) {
  xSemaphoreTake(lock,portMAX_DELAY);
  if (phase!=next) { phase=next;++revision; }
  xSemaphoreGive(lock);
}
void append(const String &s) {
  xSemaphoreTake(lock,portMAX_DELAY);logText+=safe(s);
  if (logText.length()>2000) logText.remove(0,logText.length()-2000);
  draft="";++revision;xSemaphoreGive(lock);
}
bool keepGoing() {
  if (active.load() && millis()-lastPoll.load()>30000) active.store(false);
  return active.load();
}
void heard() { setPhase(HEARING); }
struct Body { String text;bool overflow=false; };
esp_err_t transcriptionEvent(esp_http_client_event_t *event) {
  if (!active.load()) return ESP_FAIL;
  if (event->event_id==HTTP_EVENT_ON_DATA) {
    auto &body=*(Body*)event->user_data;
    if (event->data_len<0 || body.text.length()+event->data_len>8192) { body.overflow=true;return ESP_FAIL; }
    body.text.concat((const char*)event->data,event->data_len);
  }
  return ESP_OK;
}
void le16(uint8_t *p,uint16_t v) { p[0]=v;p[1]=v>>8; }
void le32(uint8_t *p,uint32_t v) { le16(p,v);le16(p+2,v>>16); }
String transcribe(const int16_t *pcm,size_t samples) {
  const char *boundary="portfolioMicBoundary04";
  String prefix="--"+String(boundary)+"\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\ngpt-4o-mini-transcribe\r\n--"+boundary+"\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n--"+boundary+"\r\nContent-Disposition: form-data; name=\"file\"; filename=\"voice.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String suffix="\r\n--"+String(boundary)+"--\r\n";
  size_t size=prefix.length()+44+samples*2+suffix.length();
  auto *payload=(uint8_t*)ps_malloc(size);if (!payload) return "";
  memcpy(payload,prefix.c_str(),prefix.length());
  uint8_t *wav=payload+prefix.length();memset(wav,0,44);
  memcpy(wav,"RIFF",4);le32(wav+4,36+samples*2);memcpy(wav+8,"WAVEfmt ",8);
  le32(wav+16,16);le16(wav+20,1);le16(wav+22,1);le32(wav+24,16000);
  le32(wav+28,32000);le16(wav+32,2);le16(wav+34,16);memcpy(wav+36,"data",4);le32(wav+40,samples*2);
  memcpy(wav+44,pcm,samples*2);memcpy(wav+44+samples*2,suffix.c_str(),suffix.length());
  Body body;esp_http_client_config_t cfg{};
  cfg.url="https://api.openai.com/v1/audio/transcriptions";cfg.method=HTTP_METHOD_POST;
  cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.timeout_ms=45000;cfg.disable_auto_redirect=true;
  cfg.event_handler=transcriptionEvent;cfg.user_data=&body;
  auto http=esp_http_client_init(&cfg);esp_err_t result=ESP_FAIL;sttHttp=0;
  if (http) {
    String auth="Bearer "+secret,content="multipart/form-data; boundary="+String(boundary);
    esp_http_client_set_header(http,"Authorization",auth.c_str());
    esp_http_client_set_header(http,"Content-Type",content.c_str());
    esp_http_client_set_post_field(http,(char*)payload,size);
    result=esp_http_client_perform(http);sttHttp=esp_http_client_get_status_code(http);
    esp_http_client_cleanup(http);
  }
  free(payload);
  String transcript;
  if (result==ESP_OK && sttHttp==200 && !body.overflow) {
    cJSON *root=cJSON_Parse(body.text.c_str()),*text=cJSON_GetObjectItemCaseSensitive(root,"text");
    if (cJSON_IsString(text)) transcript=text->valuestring;
    cJSON_Delete(root);
  }
  transcript.trim();if (transcript.length()>800) transcript=transcript.substring(0,800);
  char diagnostic[160];snprintf(diagnostic,sizeof(diagnostic),"TRANSCRIPTION {\"http\":%d,\"samples\":%u,\"chars\":%u}",sttHttp,(unsigned)samples,(unsigned)transcript.length());conversationUsbLog(diagnostic);
  return transcript;
}
void remember(const String &prompt,const String &answer) {
  if (historyCount==8) { for(unsigned i=0;i<6;++i) history[i]=history[i+2];historyCount=6; }
  history[historyCount++]=prompt;history[historyCount++]=answer;
}
void run(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    while (keepGoing()) {
      while (audioBusy() && keepGoing()) delay(20);
      if (!keepGoing()) break;
      setPhase(LISTENING);
      size_t samples=0;int16_t *pcm=audioCapture(samples,keepGoing,heard);
      if (!keepGoing()) { free(pcm);break; }
      if (!pcm) {
        if (!audioReady()) { append("\nMICROPHONE UNAVAILABLE");setPhase(ERROR);active.store(false); }
        continue;
      }
      setPhase(TRANSCRIBING);
      String prompt=networkReady()?transcribe(pcm,samples):String();free(pcm);
      if (!keepGoing()) break;
      if (prompt.isEmpty()) {
        append("\nNO TRANSCRIPT. TRY AGAIN.");setPhase(ERROR);
        for(unsigned n=0;n<40&&keepGoing();++n) delay(100);
        continue;
      }
      append("\nOPERATOR: "+prompt+"\nSKYNET: ");setPhase(THINKING);audioCue(false);
      if (!keepGoing()) break;
      bool success=false;String answer=askAI(prompt,success);
      if (!keepGoing()) break;
      append(answer+"\n");
      if (!success) {
        setPhase(ERROR);for(unsigned n=0;n<40&&keepGoing();++n) delay(100);continue;
      }
      remember(prompt,answer);++turns;audioCue(true);
      if (!keepGoing()) break;
      if (audioSpeak(answer,secret)) {
        while (audioBusy() && keepGoing()) { setPhase(audioPlaying()?SPEAKING:THINKING);delay(40); }
        if (keepGoing() && audioError()!=0) { append("VOICE UNAVAILABLE. TEXT RETAINED.\n");setPhase(ERROR);delay(1200); }
      }
      // Wait for sound decay; re-arm listening automatically after each turn.
      if (keepGoing()) delay(300);
    }
    active.store(false);audioCancel();secret="";
    setPhase(STOPPED);working.store(false);
  }
}
uint16_t crc16(const uint8_t *data,unsigned count) {
  uint16_t crc=0xffff;for(unsigned i=0;i<count;++i) { crc^=data[i];for(unsigned j=0;j<8;++j)crc=(crc>>1)^((crc&1)?0xa001:0); }return crc;
}
void glyph(uint8_t *frame,unsigned x,unsigned y,char c) {
  if(c<32||c>126)c='?';const uint8_t *g=codecFont+(c-32)*7;
  for(unsigned row=0;row<7;++row) for(unsigned col=0;col<5;++col)
    if(g[row]&(16>>col))frame[(y+row)*30+(x+col)/8]|=0x80>>((x+col)&7);
}
void bitmap(uint8_t *frame,const String &s,Phase state) {
  memset(frame,0,1920);
  String lines[7];unsigned row=0,col=0;
  for(char c:portfolioWrap(s)) {
    if(c=='\r')continue;
    if(c=='\n'||col==40) {
      if(row<6)++row;else { for(unsigned i=0;i<6;++i)lines[i]=lines[i+1];lines[6]=""; }
      col=0;if(c=='\n')continue;
    }
    lines[row]+=c;++col;
  }
  String label=String(labels[state])+"  ESC:EXIT";
  for(unsigned i=0;i<label.length()&&i<40;++i)glyph(frame,i*6,0,label[i]);
  for(unsigned y=0;y<7;++y)for(unsigned x=0;x<lines[y].length();++x)glyph(frame,x*6,(y+1)*8,lines[y][x]);
}
}
bool conversationInit(String (*ask)(const String&,bool&),bool (*network)()) {
  askAI=ask;networkReady=network;lock=xSemaphoreCreateMutex();usbLock=xSemaphoreCreateMutex();
  return lock && xTaskCreatePinnedToCore(run,"conversation",20480,nullptr,1,&task,0)==pdPASS;
}
bool conversationStart(const String &key) {
  if (active.load()) return true;
  if (!task || working.load() || !audioReady() || key.isEmpty()) return false;
  audioCancel();secret=key;historyCount=0;
  for(auto &s:history)s="";
  xSemaphoreTake(lock,portMAX_DELAY);logText="SKYNET VOICE TERMINAL\nOperator, speak naturally.\nSynthetic voice active.\n";draft="";lastSent=0;++revision;xSemaphoreGive(lock);
  working.store(true);active.store(true);lastPoll.store(millis());setPhase(LISTENING);
  xTaskNotifyGive(task);return true;
}
void conversationStop() { active.store(false);audioCancel();if(lock)setPhase(STOPPED); }
bool conversationBusy() { return working.load(); }
bool conversationActive() { return active.load(); }
void conversationDelta(const String &delta) {
  if(!active.load())return;
  xSemaphoreTake(lock,portMAX_DELAY);draft+=safe(delta);++revision;xSemaphoreGive(lock);
}
void conversationInput(cJSON *request,const String &prompt) {
  cJSON *input=cJSON_AddArrayToObject(request,"input");
  for(unsigned i=0;i<historyCount+1;++i) {
    cJSON *msg=cJSON_CreateObject();cJSON_AddStringToObject(msg,"role",i==historyCount?"user":i%2?"assistant":"user");
    cJSON_AddStringToObject(msg,"content",i==historyCount?prompt.c_str():history[i].c_str());cJSON_AddItemToArray(input,msg);
  }
}
void conversationSendFrame(Stream &out,bool force,bool paced) {
  lastPoll.store(millis());uint8_t frame[1920],header[12];Phase state;uint16_t rev;String text;
  xSemaphoreTake(lock,portMAX_DELAY);state=phase;rev=revision;text=logText+draft;xSemaphoreGive(lock);
  unsigned size=(force||rev!=lastSent)?1920:0;
  if(size)bitmap(frame,text,state);
  memcpy(header,"PTK1",4);le16(header+4,state);le16(header+6,rev);le16(header+8,size);le16(header+10,size?crc16(frame,size):0xffff);
  bool usb=&out==(Stream*)&Serial;
  if(usb)xSemaphoreTake(usbLock,portMAX_DELAY);
  if(portfolioWriteFrame(out,header,12,paced,4000) && (!size||portfolioWriteFrame(out,frame,size,paced)))lastSent=rev;
  if(usb)xSemaphoreGive(usbLock);
}
void conversationStatus() {
  Serial.printf("TALK {\"active\":%s,\"busy\":%s,\"turns\":%lu,\"stt_http\":%d,\"phase\":%u}\n",active.load()?"true":"false",working.load()?"true":"false",turns,sttHttp,(unsigned)phase);
}
void conversationUsbLog(const char *line) {
  if(usbLock)xSemaphoreTake(usbLock,portMAX_DELAY);
  Serial.println(line);
  if(usbLock)xSemaphoreGive(usbLock);
}
