#include <cassert>
#include "../src/SettingsStore.h"
int main() {
 nvs["auto"]="{\"setpoint\":42}";
 settingsStoreReady=false;
 assert(readSettingsJson("auto")==nvs["auto"]); // Partial migration cannot hide existing data.
 nvsAvailable=false;
 const String a="{\"setpoint\":51}", b="{\"setpoint\":52}";
 assert(writeSettingsJson("auto",a)); // NVS failure does not block durable file saves.
 assert(readSettingsJson("auto")==a);
 auto count=disk;assert(writeSettingsJson("auto",a));assert(disk==count); // No redundant writes.
 shortWrite=true;assert(!writeSettingsJson("auto",b));shortWrite=false;
 assert(readSettingsJson("auto")==a); // Power loss / out of space retains last good copy.
 corruptWrite=true;assert(!writeSettingsJson("auto",b));corruptWrite=false;
 assert(readSettingsJson("auto")==a); // Successful-length write with wrong bytes rejected.
 assert(writeSettingsJson("auto",b));assert(readSettingsJson("auto")==b);
 settingsStoreReady=false;assert(readSettingsJson("auto")==b); // Simulated reboot.
 disk["/cfg-auto-1.bin"].back()^=1;
 assert(readSettingsJson("auto")==a); // Latest slot corruption falls back to previous.
 disk["/cfg-auto-1.bin"].back()^=1; // Restore payload, corrupt generation instead.
 disk["/cfg-auto-1.bin"][4]^=1;
 assert(readSettingsJson("auto")==a);
 fileFail=true;assert(!writeSettingsJson("auto",b));fileFail=false;
 assert(readSettingsJson("auto")==a);
 assert(!writeSettingsJson("auto","invalid"));
 assert(writeSettingsJson("auto",b));assert(readSettingsJson("auto")==b);
}
