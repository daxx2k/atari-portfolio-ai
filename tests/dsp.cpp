#include "../firmware/PortfolioAI/AudioDSP.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include <cstdlib>
double run(bool speech,int amplitude){
 SourceLeveler level(speech);int16_t block[512];double energy=0;unsigned measured=0;
 for(unsigned k=0;k<400;++k){
  for(unsigned j=0;j<512;++j)block[j]=(int16_t)(amplitude*sin(2*AUDIO_PI*220*(k*512+j)/AUDIO_RATE));
  level.process(block,512);
  for(auto x:block){assert(std::abs((int)x)<=(speech?24000:32768));if(k>300){energy+=(double)x*x;++measured;}}
 }
 return sqrt(energy/measured);
}
int main(){
 double voice=run(true,5000),music=run(false,14000),quiet=run(false,2000);
 assert(std::abs(voice-6000)<30 && std::abs(music-5000)<30);
 assert(std::abs(quiet-2000/sqrt(2))<3); // quiet music is not boosted
 SourceLeveler voiceMeter(true),musicMeter(false);int16_t block[512]={};
 for(int k=0;k<100;++k){voiceMeter.process(block,512);musicMeter.process(block,512);for(auto x:block)assert(x==0);}
 for(auto source: {true,false})for(int volume: {0,5,40,100}){
  SourceLeveler level(source);float master=volume/100.f;
  for(int k=0;k<50;++k){for(auto &x:block)x=(k&1)?32767:-32768;level.process(block,512);applyOutputVolume(block,512,volume,master);for(auto x:block){assert(std::abs((int)x)<=(source?24000:32768));if(!volume)assert(x==0);}}
 }
 printf("PASS: steady speech %.0f RMS, loud radio %.0f RMS, quiet radio unchanged; silence, peaks and 0..100 verified\n",voice,music);
}
