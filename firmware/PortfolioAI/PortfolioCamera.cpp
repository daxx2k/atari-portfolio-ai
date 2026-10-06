#include "PortfolioCamera.h"
#include "PortfolioFrameTransport.h"
#include "esp_camera.h"
#include "img_converters.h"
#include <cstring>

namespace {
esp_err_t cameraError=ESP_FAIL;
unsigned long frameCount=0;
constexpr unsigned WIDTH=240, HEIGHT=64, BYTES=WIDTH*HEIGHT/8;
uint8_t bitmap[BYTES];
constexpr uint8_t bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
uint16_t crc16(const uint8_t *data,size_t count) {
  uint16_t crc=0xffff;
  for (size_t i=0;i<count;++i) {
    crc^=data[i];
    for (unsigned bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1)?0xa001:0);
  }
  return crc;
}
bool makeBitmap(unsigned width,unsigned height) {
  if (cameraError!=ESP_OK) return false;
  camera_fb_t *fb=esp_camera_fb_get();
  if (!fb) return false;
  size_t rgbSize=(size_t)fb->width*fb->height*3;
  uint8_t *rgb=(uint8_t*)ps_malloc(rgbSize);
  bool ok=rgb && fmt2rgb888(fb->buf,fb->len,fb->format,rgb);
  if (ok) {
    memset(bitmap,0,sizeof(bitmap));
    // Natural proportions: central panoramic crop matches the 240x64 LCD.
    unsigned cropHeight=(unsigned)fb->width*height/width;
    cropHeight=min(cropHeight,(unsigned)fb->height);
    unsigned top=((unsigned)fb->height-cropHeight)/2;
    for (unsigned y=0;y<height;++y) {
      unsigned sy=top+y*cropHeight/height;
      for (unsigned x=0;x<width;++x) {
        unsigned sx=x*(unsigned)fb->width/width;
        const uint8_t *p=rgb+((size_t)sy*fb->width+sx)*3;
        // Espressif converter supplies BGR byte order for PIXFORMAT_RGB888.
        unsigned luminance=(29u*p[0]+150u*p[1]+77u*p[2])>>8;
        unsigned threshold=8u+16u*bayer[y&3][x&3];
        if (luminance<threshold) bitmap[y*(width/8)+x/8]|=0x80u>>(x&7);
      }
    }
  }
  free(rgb); esp_camera_fb_return(fb);
  return ok;
}
}

bool cameraInit() {
  camera_config_t c={};
  c.pin_d0=45; c.pin_d1=47; c.pin_d2=48; c.pin_d3=46;
  c.pin_d4=42; c.pin_d5=40; c.pin_d6=39; c.pin_d7=21;
  c.pin_xclk=38; c.pin_pclk=41; c.pin_vsync=17; c.pin_href=18;
  c.pin_sccb_sda=-1; c.pin_sccb_scl=-1; c.sccb_i2c_port=0;
  c.pin_pwdn=-1; c.pin_reset=-1;
  c.xclk_freq_hz=20000000;
  c.ledc_timer=LEDC_TIMER_0; c.ledc_channel=LEDC_CHANNEL_0;
  c.pixel_format=PIXFORMAT_JPEG; c.frame_size=FRAMESIZE_VGA;
  c.jpeg_quality=14; c.fb_count=2;
  c.fb_location=CAMERA_FB_IN_PSRAM; c.grab_mode=CAMERA_GRAB_LATEST;
  cameraError=esp_camera_init(&c);
  return cameraError==ESP_OK;
}
bool cameraReady() { return cameraError==ESP_OK; }
int cameraErrorCode() { return cameraError; }
unsigned long cameraFrameCount() { return frameCount; }
void cameraSendFrame(Stream &output,bool paced,bool lowResolution) {
  unsigned long started=millis();
  unsigned width=lowResolution?120:WIDTH,height=lowResolution?32:HEIGHT;
  unsigned bytes=width*height/8;
  bool ok=makeBitmap(width,height);
  uint16_t crc=ok?crc16(bitmap,bytes):0;
  uint16_t gap=lowResolution?500:portfolioFrameGapUs;
  uint8_t header[12]={'P','F',ok?(lowResolution?(uint8_t)'L':(uint8_t)'C'):(uint8_t)'E','1',
    ok?(uint8_t)width:(uint8_t)0,0,ok?(uint8_t)height:(uint8_t)0,0,
    ok?(uint8_t)bytes:(uint8_t)0,ok?(uint8_t)(bytes>>8):(uint8_t)0,
    (uint8_t)crc,(uint8_t)(crc>>8)};
  bool sent=portfolioWriteFrame(output,header,sizeof(header),paced,4000);
  if (ok && sent) {
    sent=portfolioWriteFrame(output,bitmap,bytes,paced,gap);
    if(sent) ++frameCount;
  }
  if(paced) Serial.printf("CAMERA_UART capture=%s sent=%s elapsed_ms=%lu gap_us=%u mode=%s\n",ok?"ok":"failed",sent?"ok":"failed",millis()-started,(unsigned)gap,lowResolution?"live":"detail");
}
void cameraSendJPEG(Stream &output) {
  camera_fb_t *fb=cameraReady()?esp_camera_fb_get():nullptr;
  if (!fb) { output.println("ERROR camera capture unavailable"); return; }
  output.printf("JPEG %u\n",(unsigned)fb->len);
  portfolioWriteFrame(output,fb->buf,fb->len,false);
  output.write('\n');output.flush();
  esp_camera_fb_return(fb);
}
