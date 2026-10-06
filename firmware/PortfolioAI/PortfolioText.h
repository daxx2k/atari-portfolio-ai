#pragma once
#include <Arduino.h>
#include <functional>

// One conversion and word layout for serial text and the conversation bitmap.
inline String portfolioAscii(const String &text) {
  String out;out.reserve(text.length());
  for(unsigned i=0;i<text.length();++i){
    uint8_t c=text[i];
    if(c==27){
      if(i+1<text.length()&&text[i+1]=='['){i+=2;while(i<text.length()&&(uint8_t)text[i]<0x40)++i;}
      else if(i+1<text.length()&&text[i+1]==']'){i+=2;while(i<text.length()&&text[i]!=7&&text[i]!=27)++i;if(i+1<text.length()&&text[i]==27&&text[i+1]=='\\')++i;}
      continue;
    }
    if(c=='\r'){if(i+1==text.length()||text[i+1]!='\n')out+='\n';continue;}
    if(c=='\t'){out+=' ';continue;}
    if(c=='\n'||(c>=32&&c<=126)){out+=(char)c;continue;}
    uint32_t cp=0;unsigned n=0;
    if(c>=0xc2&&c<=0xdf){cp=c&31;n=1;}
    else if(c>=0xe0&&c<=0xef){cp=c&15;n=2;}
    else if(c>=0xf0&&c<=0xf4){cp=c&7;n=3;}
    else continue;
    bool ok=i+n<text.length();
    for(unsigned k=1;ok&&k<=n;++k){uint8_t d=text[i+k];if((d&0xc0)!=0x80)ok=false;else cp=(cp<<6)|(d&63);}
    if(!ok){out+='?';continue;}i+=n;
    if(cp==0xa0)out+=' ';
    else if(cp==0xb0)out+=" degrees ";
    else if(cp==0x2018||cp==0x2019)out+='\'';
    else if(cp==0x201c||cp==0x201d)out+='"';
    else if(cp==0x2013||cp==0x2014||cp==0x2212)out+='-';
    else if(cp==0x2026)out+="...";
    else if(cp==0x2022)out+='-';
    else if(cp>=0xc0&&cp<=0xc5)out+='A';
    else if(cp>=0xe0&&cp<=0xe5)out+='a';
    else if(cp>=0xc8&&cp<=0xcb)out+='E';
    else if(cp>=0xe8&&cp<=0xeb)out+='e';
    else if(cp>=0xcc&&cp<=0xcf)out+='I';
    else if(cp>=0xec&&cp<=0xef)out+='i';
    else if(cp>=0xd2&&cp<=0xd6)out+='O';
    else if(cp>=0xf2&&cp<=0xf6)out+='o';
    else if(cp>=0xd9&&cp<=0xdc)out+='U';
    else if(cp>=0xf9&&cp<=0xfc)out+='u';
    else if(cp==0xc7)out+='C';else if(cp==0xe7)out+='c';
    else if(cp==0xd1)out+='N';else if(cp==0xf1)out+='n';
    else out+='?';
  }
  return out;
}

class PortfolioText {
  String word;
  unsigned column=0,width;
  bool gap=false;
  std::function<void(char)> emit;
  void newline(){emit('\n');column=0;gap=false;}
public:
  explicit PortfolioText(std::function<void(char)> sink,unsigned columns=40):width(columns),emit(sink){}
  void reset(){word="";column=0;gap=false;}
  void flush(){
    if(word.isEmpty())return;
    if(column && column+(gap?1:0)+word.length()>width)newline();
    if(gap&&column){emit(' ');++column;}
    for(char c:word){if(column==width)newline();emit(c);++column;}
    word="";gap=false;
  }
  void append(const String &text){
    for(char c:text){
      if(c=='\n'){flush();newline();}
      else if(c==' '||c=='\t'){flush();gap=true;}
      else word+=c;
    }
  }
};

inline String portfolioWrap(const String &text,unsigned width=40) {
  String out;PortfolioText layout([&](char c){out+=c;},width);
  layout.append(portfolioAscii(text));layout.flush();return out;
}
