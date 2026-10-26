#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <cstring>
using String = std::string;
static std::map<String,std::vector<uint8_t>> disk;
static std::map<String,String> nvs;
static bool nvsAvailable=true, fileFail=false, shortWrite=false, corruptWrite=false;
struct File {
 String path; bool good=false; size_t pos=0;
 explicit operator bool() const {return good;}
 size_t size() {return disk[path].size();}
 size_t read(uint8_t* p,size_t n) {auto& d=disk[path];n=std::min(n,d.size()-pos);memcpy(p,d.data()+pos,n);pos+=n;return n;}
 int read() {uint8_t c=0;return read(&c,1)?c:-1;}
 size_t write(const uint8_t* p,size_t n) {size_t k=shortWrite?n/2:n;auto& d=disk[path];d.insert(d.end(),p,p+k);if(corruptWrite && k)d.back()^=1;return k;}
 void flush() {} void close() {}
};
struct MockFS {
 bool exists(const char* p) {return disk.count(p);}
 File open(const char* p,const char* mode) {if(fileFail)return {};if(*mode=='w')disk[p].clear();return {p,disk.count(p)>0,0};}
 bool rename(const char* a,const char* b) {disk[b]=disk[a];disk.erase(a);return true;}
} SPIFFS;
struct Preferences {
 bool begin(const char*,bool) {return nvsAvailable;}
 void end() {}
 unsigned getUInt(const char*,unsigned d) {return d;}
 size_t putUInt(const char*,unsigned) {return 4;}
 bool isKey(const char* k) {return nvs.count(k);}
 String getString(const char* k,const char* fallback) {return nvs.count(k)?nvs[k]:fallback;}
 size_t putString(const char* k,const String& v) {if(!nvsAvailable)return 0;nvs[k]=v;return v.size();}
};
struct MockEEPROM {bool begin(size_t){return true;} void end(){} uint8_t read(size_t){return 0;} } EEPROM;
#define EEPROM_SIZE 5120
#define EEPROM_ADDR_MAGIC 0
#define EEPROM_MAGIC 0xAA
#define EEPROM_ADDR_AUTO 100
#define EEPROM_ADDR_MQTT 300
#define EEPROM_ADDR_SENSORS 500
#define EEPROM_ADDR_WIFI 700
#define EEPROM_ADDR_NTP 900
#define EEPROM_ADDR_ML 1100
#define EEPROM_ADDR_RELAY 1300
#define EEPROM_ADDR_COMFORT 1500
#define EEPROM_ADDR_UPDATE 1800
