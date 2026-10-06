#include "PortfolioRadio.h"
#include "PortfolioAudio.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <atomic>
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
namespace {
const char *names[]={"Nightride FM","Darksynth FM","Chillsynth FM","Datawave FM","Nightwave Plaza"};
const char *urls[]={"https://stream.nightride.fm/nightride.mp3","https://stream.nightride.fm/darksynth.mp3","https://stream.nightride.fm/chillsynth.mp3","https://stream.nightride.fm/datawave.mp3","https://radio.plaza.one/mp3"};
constexpr uint32_t CAPACITY=131072,PREBUFFER=12000;
std::atomic<unsigned> pauseReasons{0};
std::atomic<int> station{0},state{0},httpCode{0};
std::atomic<unsigned> generation{0};std::atomic<bool> active{false},running{false};
std::atomic<uint32_t> decoded{0},underruns{0},buffered{0};TaskHandle_t task=nullptr;
struct Session {
 uint8_t *ring;unsigned token;
 std::atomic<uint32_t> write{0},read{0};
 std::atomic<bool> networkDone{false},decoderDone{false};
};
bool current(Session &s){return active.load()&&generation.load()==s.token;}
void decode(void *argument) {
 auto &s=*(Session*)argument;
 auto *decoder=(mp3dec_t*)ps_malloc(sizeof(mp3dec_t));
 auto *pcm=(mp3d_sample_t*)ps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME*sizeof(mp3d_sample_t));
 auto *input=(uint8_t*)ps_malloc(8192);
 bool output=false,primed=false;size_t filled=0;int accumulator=0;unsigned long lastOutput=millis();
 if(!decoder||!pcm||!input)state.store(4);
 else {
  mp3dec_init(decoder);
  while(current(s)) {
   uint32_t available=s.write.load()-s.read.load();buffered.store(available);
   if(!primed){if(available<PREBUFFER&&!s.networkDone.load()){delay(10);continue;}primed=true;}
   if(filled<4096 && available) {
    size_t n=std::min((size_t)available,(size_t)(8192-filled));uint32_t offset=s.read.load()&(CAPACITY-1);
    size_t first=std::min(n,(size_t)(CAPACITY-offset));memcpy(input+filled,s.ring+offset,first);
    if(n>first)memcpy(input+filled+first,s.ring,n-first);
    filled+=n;s.read.fetch_add(n);
   }
   if(!filled){if(s.networkDone.load())break;if(output&&millis()-lastOutput>100){underruns.fetch_add(1);primed=false;state.store(1);}delay(5);continue;}
   mp3dec_frame_info_t info={};int samples=mp3dec_decode_frame(decoder,input,filled,pcm,&info);
   if(info.frame_bytes>0){filled-=info.frame_bytes;memmove(input,input+info.frame_bytes,filled);}
   else {
    if(filled>=8192){filled=0;continue;}
    uint32_t more=s.write.load()-s.read.load();
    if(more){size_t n=std::min((size_t)more,(size_t)(8192-filled));uint32_t offset=s.read.load()&(CAPACITY-1);size_t first=std::min(n,(size_t)(CAPACITY-offset));memcpy(input+filled,s.ring+offset,first);if(n>first)memcpy(input+filled+first,s.ring,n-first);filled+=n;s.read.fetch_add(n);}
    else {if(s.networkDone.load())break;delay(5);}
   }
   if(samples>0 && info.hz>0 && (info.channels==1||info.channels==2)){
    if(!output){output=audioRadioBegin();if(!output)break;}
    int16_t mono[1152];size_t count=0;bool ok=true;
    for(int i=0;i<samples;i++){
     int32_t sample=pcm[i*info.channels];if(info.channels==2)sample=(sample+pcm[i*2+1])/2;
     accumulator+=24000;
     while(accumulator>=info.hz){accumulator-=info.hz;mono[count++]=(int16_t)sample;if(count==1152){ok=audioRadioWrite(mono,count)&&ok;count=0;}}
    }
    if(count)ok=audioRadioWrite(mono,count)&&ok;
    if(!ok)break;
    if(current(s) && !pauseReasons.load()){decoded.fetch_add(samples);state.store(2);lastOutput=millis();}
   }
  }
 }
 if(output)audioRadioEnd();free(decoder);free(pcm);free(input);
 s.decoderDone.store(true);vTaskDelete(nullptr);
}
void run(void*) {
 auto *ring=(uint8_t*)ps_malloc(CAPACITY);
 if(!ring){state.store(4);task=nullptr;vTaskDelete(nullptr);return;}
 unsigned retryToken=0,failures=0;
 for(;;){
  if(!active.load()){ulTaskNotifyTake(pdTRUE,portMAX_DELAY);continue;}
  running.store(true);unsigned token=generation.load();if(token!=retryToken){retryToken=token;failures=0;}int index=station.load();state.store(1);decoded.store(0);underruns.store(0);buffered.store(0);httpCode.store(0);
  esp_http_client_config_t cfg={};cfg.url=urls[index];cfg.timeout_ms=3500;cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.buffer_size=4096;cfg.disable_auto_redirect=true;
  auto http=esp_http_client_init(&cfg);Session session;session.ring=ring;session.token=token;bool started=false;
  if(http){
   esp_http_client_set_header(http,"Icy-MetaData","0");esp_http_client_set_header(http,"User-Agent","Portfolio-Skynet-Radio/0.4.10");
   if(esp_http_client_open(http,0)==ESP_OK){
    esp_http_client_fetch_headers(http);httpCode.store(esp_http_client_get_status_code(http));
    if(httpCode.load()==200 && current(session)){
     started=xTaskCreatePinnedToCore(decode,"radio-decode",24576,&session,2,nullptr,1)==pdPASS;
     esp_http_client_set_timeout_ms(http,300);
     unsigned long lastData=millis();
     uint8_t packet[4096];
     while(started&&current(session)&&!session.decoderDone.load()){
      uint32_t space=CAPACITY-(session.write.load()-session.read.load());
      if(space<4096){delay(10);continue;}
      int n=esp_http_client_read(http,(char*)packet,sizeof(packet));
      if(n<=0){if(current(session) && !esp_http_client_is_complete_data_received(http) && millis()-lastData<8000){delay(1);continue;}break;}
      lastData=millis();
      uint32_t offset=session.write.load()&(CAPACITY-1);size_t first=std::min((size_t)n,(size_t)(CAPACITY-offset));memcpy(ring+offset,packet,first);if((size_t)n>first)memcpy(ring,packet+first,n-first);session.write.fetch_add(n);
     }
    }
   }
   esp_http_client_close(http);esp_http_client_cleanup(http);
  }
  session.networkDone.store(true);
  while(started&&!session.decoderDone.load())delay(10);
  running.store(false);
  if(active.load()&&generation.load()==token){
   if(decoded.load())failures=0;
   if(++failures<3){state.store(1);for(unsigned n=0;n<25 && active.load() && generation.load()==token;++n)delay(10);}
   else{state.store(3);active.store(false);}
  }
  else if(!active.load())state.store(0);
 }
}
}
bool radioInit(){return xTaskCreatePinnedToCore(run,"radio-network",16384,nullptr,1,&task,0)==pdPASS;}
void radioSelect(int direction){if(!task)return;pauseReasons.store(0);station.store((station.load()+direction+5)%5);generation.fetch_add(1);active.store(true);state.store(1);xTaskNotifyGive(task);}
void radioStop(){pauseReasons.store(0);active.store(false);generation.fetch_add(1);}
bool radioActive(){return active.load()||running.load();}
String radioStatus(){
 const char *labels[]={"STOPPED","CONNECTING...","PLAYING","STREAM UNAVAILABLE","DECODER UNAVAILABLE"};
 auto center=[](String text){while(text.length()<38){text=" "+text;if(text.length()<38)text+=' ';}return text;};
 String top="SKYNET // RADIO";while(top.length()<33)top+=' ';top+=String(station.load()+1)+" / 5";
 int volume=audioVolume(),bars=volume*10/100;String meter="VOLUME [";
 for(int i=0;i<10;++i)meter+=i<bars?'#':'.';meter+="] "+String(volume)+"/100";
 String name=names[station.load()];name.toUpperCase();
 return top+"\n--------------------------------------\n"+center(pauseReasons.load()?"PAUSED FOR SKYNET":labels[state.load()])+"\n"+center(name)+"\n"+center(meter)+"\n\nLeft/Right: station  Up/Down: volume\nEsc: background   /radio off: stop";
}
String radioDiagnostics(){return "RADIO_BUFFER {\"bytes\":"+String(buffered.load())+",\"underruns\":"+String(underruns.load())+",\"decoded\":"+String(decoded.load())+",\"http\":"+String(httpCode.load())+",\"paused\":"+String(pauseReasons.load())+",\"output\":"+(audioRadioPlaying()?"true":"false")+"}";}

int radioStation(){return station.load()+1;}
void radioSetStation(int oneBased){int next=constrain(oneBased,1,5)-1;if(next==station.load())return;int direction=next-station.load();if(radioActive())radioSelect(direction);else station.store(next);}

int radioState(){return state.load();}

bool radioPause(unsigned reason){
 if(!radioActive() && !pauseReasons.load())return false;
 pauseReasons.fetch_or(reason);
 // Release the radio TLS/decoder allocations before TTS or microphone capture.
 active.store(false);generation.fetch_add(1);
 unsigned long began=millis();
 while(running.load() && millis()-began<5000)delay(5);
 return true;
}
void radioResume(unsigned reason){
 unsigned previous=pauseReasons.fetch_and(~reason);
 if(!(previous&reason) || (previous&~reason))return;
 if(!radioActive())radioSelect(0); // reconnect a stream that dropped during speech
}
unsigned radioPaused(){return pauseReasons.load();}
