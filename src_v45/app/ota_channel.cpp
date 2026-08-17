#include "ota_channel.h"
#include "../config/ota_urls.h"
#include "../config/version.h"
#include <HTTPClient.h>
#include <WiFi.h>

void OtaChannelV45::begin() {}

const char* OtaChannelV45::channel() const { return OTA_CHANNEL; }

bool OtaChannelV45::check(String& latestOut, String& errOut) {
  latestOut = "";
  errOut = "";
  if (WiFi.status() != WL_CONNECTED) {
    errOut = "wifi";
    return false;
  }
  HTTPClient http;
  if (!http.begin(OTA_VERSION_URL)) {
    errOut = "begin";
    return false;
  }
  http.setTimeout(4000);
  const int code = http.GET();
  if (code != 200) {
    errOut = String("http_") + code;
    http.end();
    return false;
  }
  latestOut = http.getString();
  latestOut.trim();
  http.end();
  return latestOut.length() > 0 && latestOut != FIRMWARE_VERSION;
}
