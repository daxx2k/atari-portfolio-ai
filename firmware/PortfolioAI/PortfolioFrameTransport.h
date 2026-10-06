#pragma once
#include <Arduino.h>
extern uint16_t portfolioFrameGapUs;

// The Portfolio has a single-byte UART receiver, without a FIFO. Draining
// each byte before the pause provides a real gap on the wire; pausing after
// queuing a burst does not. USB diagnostics do not need this pacing.
inline bool portfolioWriteFrame(Stream &out,const uint8_t *data,size_t bytes,bool paced,uint16_t requestedGap=0xffff) {
  const uint16_t gap=requestedGap==0xffff?portfolioFrameGapUs:requestedGap;
  size_t sent=0;
  unsigned long started=millis();
  while(sent<bytes && (unsigned long)(millis()-started)<15000) {
    size_t count=paced?1:min((size_t)32,bytes-sent);
    size_t written=out.write(data+sent,count);
    if(written>count) return false;
    sent+=written;
    if(paced && written) {
      out.flush();
      if(gap>=1000) delay(gap/1000);
      if(gap%1000) delayMicroseconds(gap%1000);
    }
    else if(!written) delay(1);
  }
  out.flush();
  return sent==bytes;
}
