#pragma once
#include <Preferences.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// JSON settings live in separate NVS keys. EEPROM is retained only for legacy
// migration and the non-overlapping scalar/statistics records.
static bool settingsStoreReady = false;
static bool settingsStoreError = false;
static unsigned settingsMigrationRejected = 0;
static constexpr size_t SETTINGS_JSON_MAX = 4096;

static bool validSettingsJson(const String& json) {
  if (json.length() == 0 || json.length() > SETTINGS_JSON_MAX) return false;
  DynamicJsonDocument doc(4096);
  return !deserializeJson(doc, json) && doc.is<JsonObject>();
}

static void initializeSettingsStore() {
  Preferences prefs;
  if (!prefs.begin("kotelcfg", false)) { settingsStoreError = true; return; }
  if (prefs.getUInt("schema", 0) == 1) {
    settingsMigrationRejected = prefs.getUInt("rejected", 0);
    settingsStoreReady = true;
    prefs.end();
    return;
  }
  uint8_t* legacy = static_cast<uint8_t*>(malloc(EEPROM_SIZE));
  bool ok = legacy != nullptr;
  const char* backupPath = "/settings-legacy-v1.bin";
  if (ok && SPIFFS.exists(backupPath)) {
    File backup = SPIFFS.open(backupPath, "r");
    ok = backup && backup.size() == EEPROM_SIZE &&
         backup.read(legacy, EEPROM_SIZE) == EEPROM_SIZE;
    backup.close();
  } else if (ok) {
    ok = EEPROM.begin(EEPROM_SIZE);
    if (ok) {
      for (size_t i = 0; i < EEPROM_SIZE; ++i) legacy[i] = EEPROM.read(i);
      EEPROM.end();
      // A separate file avoids filling the small NVS partition with a second 5 KB blob.
      File backup = SPIFFS.open("/settings-legacy-v1.tmp", "w");
      ok = backup && backup.write(legacy, EEPROM_SIZE) == EEPROM_SIZE;
      backup.close();
      if (ok) ok = SPIFFS.rename("/settings-legacy-v1.tmp", backupPath);
    }
  }
  struct LegacySection { const char* key; int address; int writtenLimit; };
  const LegacySection sections[] = {
    {"auto", EEPROM_ADDR_AUTO, 200}, {"mqtt", EEPROM_ADDR_MQTT, 180},
    {"sensors", EEPROM_ADDR_SENSORS, 200}, {"wifi", EEPROM_ADDR_WIFI, 200},
    {"ntp", EEPROM_ADDR_NTP, 150}, {"ml", EEPROM_ADDR_ML, 96},
    {"relay", EEPROM_ADDR_RELAY, 200}, {"comfort", EEPROM_ADDR_COMFORT, 250},
    {"update", EEPROM_ADDR_UPDATE, 1024}
  };
  if (ok) for (const auto& section : sections) {
    String json;
    int len = 0;
    memcpy(&len, legacy + section.address, sizeof(len));
    if (legacy[EEPROM_ADDR_MAGIC] == EEPROM_MAGIC && len > 0 &&
        len <= section.writtenLimit && section.address + 4 + len <= EEPROM_SIZE) {
      json.reserve(len);
      for (int i = 0; i < len; ++i) json += char(legacy[section.address + 4 + i]);
    }
    if (!validSettingsJson(json)) {
      json = "{}"; // Never import a truncated/partially parsed document.
      ++settingsMigrationRejected;
    }
    // Keep already migrated keys if a previous boot was interrupted.
    if (!prefs.isKey(section.key)) ok = (prefs.putString(section.key, json) == json.length()) && ok;
  }
  free(legacy);
  if (ok) ok = prefs.putUInt("rejected", settingsMigrationRejected) == sizeof(uint32_t);
  if (ok) ok = prefs.putUInt("schema", 1) == sizeof(uint32_t);
  settingsStoreReady = ok;
  settingsStoreError = !ok;
  prefs.end();
}

// Two independent files per section. A failed/truncated write never destroys
// the newest valid copy. NVS remains a read fallback for existing installations.
static const char* settingsStoreLastError = "";
struct SettingsFileHeader {
  uint32_t magic, generation, length, crc;
};
static uint32_t settingsCrc(const String& json, uint32_t generation) {
  uint32_t crc = 0xffffffffUL;
  for (size_t i = 0; i < json.length() + 4; ++i) {
    crc ^= i < 4 ? uint8_t(generation >> (i * 8)) : uint8_t(json[i - 4]);
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1)));
  }
  return ~crc;
}
static String settingsSlotPath(const char* key, unsigned slot) {
  return String("/cfg-") + key + (slot ? "-1.bin" : "-0.bin");
}
static bool readSettingsSlot(const char* key, unsigned slot,
                             SettingsFileHeader& header, String& json) {
  File f = SPIFFS.open(settingsSlotPath(key, slot).c_str(), "r");
  if (!f) return false;
  bool ok = f.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) == sizeof(header)
         && header.magic == 0x4b434647UL && header.length > 0
         && header.length <= SETTINGS_JSON_MAX
         && f.size() == sizeof(header) + header.length;
  json = "";
  if (ok) {
    json.reserve(header.length);
    for (size_t i = 0; i < header.length; ++i) json += char(f.read());
    ok = settingsCrc(json, header.generation) == header.crc && validSettingsJson(json);
  }
  f.close();
  return ok;
}
static int newestSettingsSlot(const char* key, SettingsFileHeader (&h)[2], String (&j)[2]) {
  bool a = readSettingsSlot(key, 0, h[0], j[0]);
  bool b = readSettingsSlot(key, 1, h[1], j[1]);
  if (!a) return b ? 1 : -1;
  if (!b) return 0;
  return int32_t(h[1].generation - h[0].generation) > 0 ? 1 : 0;
}
static String readSettingsJson(const char* key) {
  SettingsFileHeader h[2] = {};
  String j[2];
  int newest = newestSettingsSlot(key, h, j);
  if (newest >= 0) return j[newest];
  Preferences prefs;
  // A migration failure in another section must not hide valid NVS settings.
  if (!prefs.begin("kotelcfg", true)) {
    settingsStoreError = true;
    settingsStoreLastError = "nvs_open";
    return String();
  }
  String json = prefs.getString(key, "");
  prefs.end();
  if (!validSettingsJson(json)) {
    settingsStoreError = true;
    settingsStoreLastError = "no_valid_copy";
    return String();
  }
  return json;
}
static bool writeSettingsJson(const char* key, const String& json) {
  settingsStoreLastError = "invalid_json";
  if (!validSettingsJson(json)) { settingsStoreError = true; return false; }
  SettingsFileHeader h[2] = {};
  String j[2];
  int newest = newestSettingsSlot(key, h, j);
  // Avoid flash wear when this exact value is already durably stored.
  if (newest >= 0 && j[newest] == json) { settingsStoreLastError = ""; return true; }
  unsigned target = newest == 0 ? 1 : 0;
  SettingsFileHeader next = {0x4b434647UL, newest >= 0 ? h[newest].generation + 1 : 1,
                            uint32_t(json.length()), 0};
  next.crc = settingsCrc(json, next.generation);
  settingsStoreLastError = "file_open";
  File f = SPIFFS.open(settingsSlotPath(key, target).c_str(), "w");
  if (!f) { settingsStoreError = true; return false; }
  settingsStoreLastError = "file_write";
  bool ok = f.write(reinterpret_cast<const uint8_t*>(&next), sizeof(next)) == sizeof(next);
  if (ok) ok = f.write(reinterpret_cast<const uint8_t*>(json.c_str()), json.length()) == json.length();
  f.flush();
  f.close();
  SettingsFileHeader verify = {};
  String restored;
  if (ok) {
    settingsStoreLastError = "file_verify";
    ok = readSettingsSlot(key, target, verify, restored)
      && verify.generation == next.generation && restored == json;
  }
  if (!ok) { settingsStoreError = true; return false; }
  settingsStoreLastError = "";
  return true;
}
