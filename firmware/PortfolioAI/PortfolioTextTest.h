#pragma once
#include "PortfolioText.h"
inline void portfolioDisplayTest(Stream &out){
  unsigned passed=0,total=0;
  auto check=[&](bool ok){++total;if(ok)++passed;};
  String full;for(int i=0;i<40;++i)full+='x';
  check(portfolioWrap(full+"\nNext")==full+"\nNext");
  check(portfolioWrap(full.substring(0,39)+" word")==full.substring(0,39)+"\nword");
  check(portfolioWrap("alpha   beta\t gamma")=="alpha beta gamma");
  check(portfolioWrap("A\r\nB")=="A\nB");
  check(portfolioWrap("A\n\nB")=="A\n\nB");
  check(portfolioWrap(full+full+" tail")==full+"\n"+full+"\ntail");
  check(portfolioWrap("It's \xe2\x80\x94 20\xc2\xb0" "C\xe2\x80\xa6 Caf\xc3\xa9")=="It's - 20 degrees C... Cafe");
  check(portfolioWrap("a\x1b[31mb\x1b[0mc")=="abc");
  String streamed;PortfolioText layout([&](char c){streamed+=c;});
  layout.append("Hello wor");layout.append("ld. Next wo");layout.append("rd");layout.flush();
  check(streamed==portfolioWrap("Hello world. Next word"));
  for(unsigned width=1;width<=40;++width){
    String text=portfolioWrap("Short words mixed with a_very_long_identifier_without_spaces and blank\n\nlines.",width);unsigned col=0;bool ok=true;
    for(char c:text){if(c=='\n')col=0;else if(++col>width)ok=false;}
    check(ok);
  }
  out.printf("DISPLAY_TEST {\"passed\":%u,\"total\":%u}\n",passed,total);
}
