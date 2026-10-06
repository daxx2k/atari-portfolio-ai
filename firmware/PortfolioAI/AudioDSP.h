#pragma once
#include <cmath>
#include <cstdint>

constexpr unsigned AUDIO_RATE = 24000;
// Voice sounded about 20% quieter than music: compensate by 1 / 0.8.
constexpr double SPEECH_BALANCE_GAIN = 1.25;
constexpr unsigned CUE_SAMPLES = 5280; // 220 ms
constexpr double AUDIO_PI = 3.14159265358979323846;

// Same pure DSP functions are used by firmware and the host validation harness.
inline int16_t cyberCueSample(unsigned index, bool received) {
  if (index>=CUE_SAMPLES) return 0;
  double t=(double)index/AUDIO_RATE,start=0,end=0,frequency=0;
  if (!received) {
    if (t<.085) {start=0;end=.085;frequency=880;}
    else if (t>=.120 && t<.205) {start=.120;end=.205;frequency=1320;}
  } else {
    if (t<.055) {start=0;end=.055;frequency=1320;}
    else if (t>=.070 && t<.125) {start=.070;end=.125;frequency=1760;}
    else if (t>=.140 && t<.205) {start=.140;end=.205;frequency=880;}
  }
  if (!frequency) return 0;
  double local=t-start;
  double envelope=std::fmin(1.0,local/.006)*std::fmin(1.0,(end-t)/.010);
  double phase=2*AUDIO_PI*frequency*local;
  // Soft, band-limited square-like beeps: classic 1980s computer sound.
  double wave=.80*std::sin(phase)+.16*std::sin(3*phase)+.04*std::sin(5*phase);
  return (int16_t)(6200*envelope*wave);
}

inline int16_t robotSample(int16_t input, uint32_t index, bool robot) {
  double gain = .70;
  if (robot) {
    double phase=2*AUDIO_PI*60*index/AUDIO_RATE;
    // Stronger metallic sidebands with a dry component for clear consonants.
    // Peak gain remains below one, with similar average level to the old voice.
    gain=.82*(.65+.30*std::sin(phase)+.05*std::sin(2*phase));
  }
  int32_t value = (int32_t)(input*gain);
  if (robot) value = (value/64)*64;
  return (int16_t)value;
}

// User volume is PCM gain, not a percentage of the ES8311 dB register.
// Codec stays at unity (driver 75 => register 0xBF), never +32 dB.
inline void applyOutputVolume(int16_t *samples, unsigned count, int target, float &current) {
  target=target<0?0:(target>100?100:target);
  const float destination=target/100.0f;
  for(unsigned i=0;i<count;++i){
    if(target==0)current=0; // exact silence on the next buffer
    else if(current<destination)current=std::fmin(destination,current+1.0f/480);
    else if(current>destination)current=std::fmax(destination,current-1.0f/480);
    samples[i]=(int16_t)(samples[i]*current);
  }
}
inline double speechGain(const int16_t *samples, unsigned count, bool robotic, void (*cooperate)()=nullptr) {
  uint64_t energy=0;int peak=0;
  if(!count)return 1;
  for(unsigned i=0;i<count;++i){
    // Integer analysis avoids two double-precision sine calls per sample.
    int value=samples[i];
    energy+=(int64_t)value*value;int magnitude=value<0?-value:value;
    if(magnitude>peak)peak=magnitude;
    if(cooperate && (i&4095)==4095)cooperate();
  }
  const double rmsFactor=robotic ? .82*std::sqrt(.65*.65+(.30*.30+.05*.05)/2) : .70;
  const double peakFactor=robotic ? .82 : .70;
  double rms=std::sqrt((double)energy/count)*rmsFactor;
  if(rms<100 || !peak)return 1; // do not boost silence/noise
  return std::fmin(3.0,std::fmin(3500.0/rms,20000.0/(peak*peakFactor)));
}
inline int16_t leveledSpeech(int16_t input, unsigned index, bool robotic, double gain) {
  double value=robotSample(input,index,robotic)*gain*SPEECH_BALANCE_GAIN;
  return (int16_t)std::fmax(-20000,std::fmin(20000,value));
}

// Independent source meters prevent a loud radio track setting speech gain.
// Ignore silence, move gain slowly, and never boost radio background noise.
class SourceLeveler {
 bool speech; double power=0,gain;
 public:
 explicit SourceLeveler(bool isSpeech):speech(isSpeech),gain(isSpeech?1.6:.7){}
 void reset(){power=0;gain=speech?1.6:.7;}
 void process(int16_t *pcm,unsigned count){
  if(!count)return;
  uint64_t sum=0;
  for(unsigned i=0;i<count;++i){int v=pcm[i];sum+=(int64_t)v*v;}
  const double mean=(double)sum/count;
  double desired=gain;
  if(mean>150.0*150){
   double alpha=count/(AUDIO_RATE*.75+count);
   power=power?power+(mean-power)*alpha:mean;
   desired=(speech?6000.0:5000.0)/std::sqrt(power);
   desired=std::fmax(speech?1.0:.25,std::fmin(speech?4.0:1.0,desired));
  }
  // Gain moves over roughly half a second; no per-syllable jumps.
  const double step=(double)count/(AUDIO_RATE*.5);
  double next=std::fmax(gain-step,std::fmin(gain+step,desired));
  const double delta=(next-gain)/count;
  for(unsigned i=0;i<count;++i){
   gain+=delta;double value=pcm[i]*gain;
   if(speech){
    double magnitude=std::fabs(value);
    // Smooth peak compression, with headroom below signed16-bit full scale.
    if(magnitude>18000)magnitude=18000+(magnitude-18000)/(1+(magnitude-18000)/6000);
    value=value<0?-magnitude:magnitude;
   }
   pcm[i]=(int16_t)value;
  }
  gain=next;
 }
};
