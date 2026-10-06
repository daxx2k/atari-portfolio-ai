#include "PortfolioSetup.h"
#include "cJSON.h"

namespace {
cJSON *pending=nullptr;String problem;unsigned long started=0;uint16_t transferCRC=0xffff;
const char *fields[]={"name","city","country","timezone","tz_rule","ssid","wifi_password","api_key","model","voice","robot","volume","radio_station","frame_gap_us"};
const char *keys[]={"name","city","country","timezone","tz_rule","ssid","wifi","key","model","voice","robot","volume","station","framegap"};
const unsigned limits[]={80,80,2,80,80,32,63,512,80,3,3,3,1,4};
void reset(){cJSON_Delete(pending);pending=nullptr;problem="";started=0;}
const char *zoneRule(const String &zone) {
  if(zone=="UTC")return "UTC0";
  if(zone=="Europe/London")return "GMT0BST,M3.5.0/1,M10.5.0/2";
  if(zone=="Europe/Paris"||zone=="Europe/Rome"||zone=="Europe/Berlin")return "CET-1CEST,M3.5.0/2,M10.5.0/3";
  if(zone=="America/New_York")return "EST5EDT,M3.2.0/2,M11.1.0/2";
  if(zone=="America/Chicago")return "CST6CDT,M3.2.0/2,M11.1.0/2";
  if(zone=="America/Denver")return "MST7MDT,M3.2.0/2,M11.1.0/2";
  if(zone=="America/Los_Angeles")return "PST8PDT,M3.2.0/2,M11.1.0/2";
  if(zone=="Asia/Tokyo")return "JST-9";
  if(zone=="Asia/Kolkata")return "IST-5:30";
  if(zone=="Asia/Singapore")return "SGT-8";
  if(zone=="Australia/Sydney")return "AEST-10AEDT,M10.1.0/2,M4.1.0/3";
  return nullptr;
}
}
void setupExpire(){if(started&&millis()-started>15000)reset();}
bool setupLine(const String &line,Preferences &settings,bool busy,String &result,bool &changed) {
  result="";changed=false;setupExpire();
  if(!line.startsWith("@SETUP "))return false;
  String value=line.substring(7);
  if(value=="BEGIN") {
    reset();started=millis();pending=cJSON_CreateObject();transferCRC=0xffff;
    if(busy)problem="Exit /talk before applying settings.";
    if(!pending)problem="Not enough memory.";
    return true;
  }
  if(value=="END" || value.startsWith("END ")) {
    if(!started)problem="Settings transfer missing or timed out.";
    String code=value.substring(4);bool validCRC=code.length()==4;
    for(char c:code)if(!isxdigit((unsigned char)c))validCRC=false;
    if(!validCRC || (uint16_t)strtoul(code.c_str(),nullptr,16)!=transferCRC)
      problem="Settings transfer checksum failed. Retry /reload.";
    if(problem.isEmpty()) {
      cJSON *zone=cJSON_GetObjectItemCaseSensitive(pending,"timezone");
      if(!zone)problem="Include timezone in AISETUP.TXT, such as Europe/London.";
      else {
        const char *rule=zoneRule(zone->valuestring);
        if(rule) { cJSON_DeleteItemFromObjectCaseSensitive(pending,"tz_rule");cJSON_AddStringToObject(pending,"tz_rule",rule); }
        else if(!cJSON_GetObjectItemCaseSensitive(pending,"tz_rule"))problem="Unsupported timezone. Use UTC or supply tz_rule.";
      }
    }
    if(problem.isEmpty()) {
      for(unsigned i=0;i<9;++i) {
        cJSON *field=cJSON_GetObjectItemCaseSensitive(pending,fields[i]);
        if(field && settings.getString(keys[i],"")!=field->valuestring) {
          if(!settings.putString(keys[i],field->valuestring)) { problem="Settings storage failed. Retry setup.";break; }
          changed=true;
        }
      }
      for(unsigned i=0;i<9;++i) {
        cJSON *field=cJSON_GetObjectItemCaseSensitive(pending,fields[i]);
        if(field && settings.getString(keys[i],"")!=field->valuestring)problem="Settings read-back failed. Retry setup.";
      }
    }
    if(problem.isEmpty()){
      for(unsigned i=9;i<14;++i){auto *f=cJSON_GetObjectItemCaseSensitive(pending,fields[i]);if(!f)continue;
        String data=f->valuestring;bool stored=true;
        if(i<11){bool value=data=="on";if(settings.getBool(keys[i],true)!=value){stored=settings.putBool(keys[i],value)>0;changed=true;}}
        else if(i==13){uint16_t value=data.toInt();if(settings.getUShort(keys[i],4000)!=value){stored=settings.putUShort(keys[i],value)>0;changed=true;}}
        else {int value=data.toInt();int fallback=i==11?40:1;if(settings.getInt(keys[i],fallback)!=value){stored=settings.putInt(keys[i],value)>0;changed=true;}}
        if(!stored)problem="Settings storage failed. Retry setup.";
        bool matches=i<11 ? settings.getBool(keys[i],true)==(data=="on") :
          i==13 ? settings.getUShort(keys[i],4000)==data.toInt() :
          settings.getInt(keys[i],i==11?40:1)==data.toInt();
        if(!matches)problem="Settings read-back failed. Retry setup.";
      }
    }
    result=problem.isEmpty()?"AISETUP.TXT applied. Credentials hidden.\nUse /location, /time, /weather or /news.":"Setup rejected: "+problem;
    reset();return true;
  }
  if(!started)return true; // Never echo a stray secret-bearing line.
  String checked=line+"\n";
  for(char c:checked) { transferCRC^=(uint8_t)c;for(unsigned i=0;i<8;++i)transferCRC=(transferCRC>>1)^((transferCRC&1)?0xa001:0); }
  if(!problem.isEmpty())return true;
  if(value.isEmpty() || value.startsWith("#") || value.startsWith(";"))return true;
  int equal=value.indexOf('=');
  if(equal<1) { problem="Expected name=value lines.";return true; }
  String field=value.substring(0,equal);field.trim();
  String data=value.substring(equal+1); // Preserve spaces and '=' in credentials.
  unsigned index=14;for(unsigned i=0;i<14;++i)if(field==fields[i])index=i;
  if(index==14) { problem="Unknown setting name.";return true; }
  if(cJSON_GetObjectItemCaseSensitive(pending,field.c_str())) { problem="Duplicate setting name.";return true; }
  if(data.length()>limits[index] || (data.isEmpty() && index!=0 && index!=6)) {
    problem="Empty or oversized setting.";return true;
  }
  for(char c:data)if(c<32 || c>126)problem="Use plain ASCII settings.";
  if(index==2 && (data.length()!=2 || data[0]<'A'||data[0]>'Z'||data[1]<'A'||data[1]>'Z'))
    problem="country must be a two-letter uppercase code, such as GB.";
  if(index==9||index==10){if(data!="on"&&data!="off")problem="voice and robot must be on or off.";}
  if(index>=11){bool numeric=!data.isEmpty();for(char c:data)if(c<'0'||c>'9')numeric=false;
    int v=data.toInt();if(!numeric||(index==11&&(v<0||v>100))||(index==12&&(v<1||v>5))||(index==13&&(v<0||v>4000)))problem="Option outside allowed range.";}
  if(problem.isEmpty())cJSON_AddStringToObject(pending,field.c_str(),data.c_str());
  return true;
}
