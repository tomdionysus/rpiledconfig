# Releasing to Google Play

The app is `com.tomcully.rpiledconfig`. That name is permanent once a build is uploaded.

## The upload key

Google Play holds the key that signs what users install (Play App Signing). The project signs
what it uploads with an upload key, which Play can replace if it is lost.

- The key is a PKCS12 keystore outside the repository: `~/.android/rpiledconfig-upload.jks`.
- `app/android/keystore.properties`, which git ignores, names it and holds its password.
  `app/android/app/build.gradle` reads it. Without that file a release is signed with the
  debug key, which installs on a test phone and which Play refuses.
- Both exist on one machine only. Back both up somewhere private.

To make one on another machine, write the properties file by hand:

    keytool -genkeypair -keystore ~/.android/rpiledconfig-upload.jks -storetype PKCS12 \
        -alias upload -keyalg RSA -keysize 4096 -validity 10000 \
        -dname "CN=T. Cully, O=RPiLEDConfig"

    storeFile=/Users/you/.android/rpiledconfig-upload.jks
    storePassword=...
    keyAlias=upload
    keyPassword=...

## Building

From `app/android`:

    ./gradlew bundleRelease

The bundle is `app/build/outputs/bundle/release/app-release.aab`. Do not run `./gradlew clean`:
React Native's native clean step fails once its generated code has gone. To start again, delete
`app/.cxx`, `app/build`, and `.cxx` and `build` in `../modules/led-reader/android`.

Every upload needs a higher `versionCode` (`app/android/app/build.gradle`, major * 10000 +
minor * 100 + patch; `scripts/version-check.mjs` checks it against the version).

Before uploading, check these on the bundle or an APK built with it:

- Every 64-bit library is aligned to 16 KB.
- The merged manifest has no `INTERNET` permission, and the build has no `com.google.mlkit`,
  `com.google.android.gms`, `com.google.android.datatransport` or `com.google.firebase` classes
  (`app/build.gradle` excludes them). The privacy policy depends on both.

## Store listing

Graphics are in `app/store/`, made by `scripts/make-icons.sh`: `icon-512.png` and
`feature-graphic.png`. Play also needs at least two phone screenshots, taken from the running
app.

Short description:

    Set up a Raspberry Pi's WiFi by reading its blinking LED with your camera.

Full description:

    RPi LED Config sets up the WiFi of a Raspberry Pi with no keyboard, screen or network, using
    only what someone in the room has: sight of its LED and Bluetooth range.

    Choose your Pi from the list and its LED starts blinking a one-time key. Point the camera at
    it for a few seconds. The app and the Pi prove to each other that they hold the key, without
    sending it, and everything after that is encrypted. Pick the network the Pi should join and
    give its passphrase.

    The Pi needs the free RPiLEDConfig service installed:
    https://github.com/tomdionysus/rpiledconfig

    The app has no accounts and no ads, collects nothing, and has no permission to use the
    internet.

    Raspberry Pi is a trademark of Raspberry Pi Ltd. This app is not made or endorsed by
    Raspberry Pi Ltd.

## App content forms

| Form | Answer |
| --- | --- |
| Privacy policy | The public URL of `docs/privacy.md`. |
| Ads | None. |
| App access | Some functionality is restricted: it needs a Raspberry Pi running the service. Give the reviewer the instructions below and a video of a whole pairing. |
| Content rating | Utility. No user-generated content, no sharing between users. |
| Target audience | 18 and over. |
| News, government, health, financial | None of these. |
| Data safety: collected or shared | Nothing. The WiFi passphrase goes to the user's own Pi over Bluetooth and never to the developer or a third party, which Play's definition of collection does not cover. |
| Data safety: encrypted in transit | Yes. |
| Data safety: deletion | Not applicable: nothing is collected. |

The first launch explains each permission before Android asks (`src/screens/Permissions.tsx`).

Location: the app declares `ACCESS_FINE_LOCATION`, in the foreground only. On Android 11 and
earlier Android needs it for Bluetooth scanning, and on every version to tell an app the
phone's network name. The location is never read.

Instructions for the reviewer:

    This app configures a Raspberry Pi and does nothing without one. A video of a whole pairing
    is at <URL>. To test it yourself you need a Raspberry Pi 4 or 5 running Raspberry Pi OS with
    the rpiledconfig package installed and setup.always set to true. Open the app, choose Find
    the Pi, tap the Pi, and point the camera at the Pi's activity LED.

## Tracks

1. Internal testing: confirm the build Play signs and delivers still pairs.
2. Closed testing: a personal account created after November 2023 needs 12 opted-in testers
   for 14 days in a row before it can apply for production access.
3. Production: apply, and roll out in stages.

Play's rules change, so what the Play Console shows wins over this file.
