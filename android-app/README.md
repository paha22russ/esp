# Kotel Local (Android)

Android app wrapper for ESP boiler web UI with automatic local discovery.

## Features

- WebView loads the existing ESP UI unchanged.
- Auto-discovery order:
  1. Last known URL
  2. `http://kotel.local`
  3. NSD/mDNS discovery (`_http._tcp`)
  4. Local `/24` subnet scan with ESP API verification
- Manual URL override as fallback.

## Build

1. Install Android SDK (platform 35 + build-tools 35.0.0).
2. Set `ANDROID_SDK_ROOT` (or create `local.properties` with `sdk.dir=...`).
3. Run:

```bash
cd android-app
./gradlew clean assembleDebug
./gradlew assembleRelease
```

## Output artifacts

- `app/build/outputs/apk/debug/app-debug.apk`
- `app/build/outputs/apk/release/app-release-unsigned.apk`
