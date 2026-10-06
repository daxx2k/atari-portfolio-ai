#include "PortfolioVision.h"
#include "PortfolioCamera.h"
#include "esp_camera.h"
#include "mbedtls/base64.h"
#include <cstring>

bool visionRequested(const String &prompt) {
  String text=prompt;text.toLowerCase();text.trim();
  if(text=="/look" || text.startsWith("/look "))return true;
  text.replace("\xe2\x80\x99", "'"); // Curly apostrophe from speech transcription.
  String normalized=" ";bool space=true;
  for(char c:text) {
    if(c=='\'' || c=='`')continue;
    if((c>='a'&&c<='z') || (c>='0'&&c<='9')) { normalized+=c;space=false; }
    else if(!space) { normalized+=' ';space=true; }
  }
  if(!space)normalized+=' ';
  // Avoid a common non-visual question while accepting polite surrounding words.
  if(normalized.indexOf(" that mean ")>=0 || normalized.indexOf(" this mean ")>=0)return false;
  const char *phrases[]={" whats that "," what is that "," whats this "," what is this ",
    " what am i looking at "," what do you see "," describe what you see ",
    " describe the scene "," identify this object "};
  for(const char *phrase:phrases)if(normalized.indexOf(phrase)>=0)return true;
  return false;
}

bool visionCapture(char *&url,size_t &jpegBytes,unsigned long &captureMs) {
  url=nullptr;jpegBytes=0;unsigned long began=millis();
  if(!cameraReady())return false;
  // Discard both queued frames so this question gets a newly captured image.
  for(unsigned i=0;i<2;++i) {
    camera_fb_t *old=esp_camera_fb_get();
    if(!old)return false;
    esp_camera_fb_return(old);
  }
  camera_fb_t *fb=esp_camera_fb_get();
  if(!fb)return false;
  bool valid=fb->format==PIXFORMAT_JPEG && fb->len>=4 && fb->len<=128*1024 &&
    fb->buf[0]==0xff && fb->buf[1]==0xd8 &&
    fb->buf[fb->len-2]==0xff && fb->buf[fb->len-1]==0xd9;
  if(valid) {
    constexpr const char *prefix="data:image/jpeg;base64,";
    size_t head=strlen(prefix),encoded=4*((fb->len+2)/3),written=0;
    url=(char*)ps_malloc(head+encoded+1);
    if(url) {
      memcpy(url,prefix,head);
      int error=mbedtls_base64_encode((unsigned char*)url+head,encoded+1,&written,fb->buf,fb->len);
      if(error || written!=encoded) { free(url);url=nullptr; }
      else { url[head+written]=0;jpegBytes=fb->len; }
    }
  }
  esp_camera_fb_return(fb);captureMs=millis()-began;
  return url!=nullptr;
}

bool visionAttach(cJSON *request,const String &prompt,const char *url) {
  cJSON *input=cJSON_GetObjectItemCaseSensitive(request,"input");
  if(!cJSON_IsArray(input)) {
    cJSON_DeleteItemFromObjectCaseSensitive(request,"input");
    input=cJSON_AddArrayToObject(request,"input");
    cJSON *message=cJSON_CreateObject();
    if(!input || !message) { cJSON_Delete(message);return false; }
    cJSON_AddStringToObject(message,"role","user");
    cJSON_AddStringToObject(message,"content",prompt.c_str());
    cJSON_AddItemToArray(input,message);
  }
  int size=cJSON_GetArraySize(input);
  cJSON *message=cJSON_GetArrayItem(input,size-1);
  if(!message)return false;
  cJSON_DeleteItemFromObjectCaseSensitive(message,"content");
  cJSON *content=cJSON_AddArrayToObject(message,"content"),*text=cJSON_CreateObject(),*image=cJSON_CreateObject();
  if(!content || !text || !image) { cJSON_Delete(text);cJSON_Delete(image);return false; }
  String question=prompt;
  if(question=="/look")question="Describe the main object and scene in this camera photograph.";
  else if(question.startsWith("/look "))question=question.substring(6);
  cJSON_AddStringToObject(text,"type","input_text");
  cJSON_AddStringToObject(text,"text",question.c_str());
  cJSON_AddStringToObject(image,"type","input_image");
  cJSON_AddStringToObject(image,"detail","low");
  // Reference the PSRAM buffer rather than making another large heap copy.
  cJSON *reference=cJSON_CreateStringReference(url);
  if(!reference) { cJSON_Delete(text);cJSON_Delete(image);return false; }
  cJSON_AddItemToObject(image,"image_url",reference);
  cJSON_AddItemToArray(content,text);cJSON_AddItemToArray(content,image);
  return cJSON_GetObjectItemCaseSensitive(text,"text") && cJSON_GetObjectItemCaseSensitive(image,"type");
}
