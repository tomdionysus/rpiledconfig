# RPi LED Config: privacy policy

RPi LED Config is an Android app that sets up the WiFi of a Raspberry Pi running the
RPiLEDConfig service. It collects no personal data, and has no server to send any to.

## What the app does not do

- It has no accounts, advertising, analytics or crash reporting.
- A release of the app has no permission to use the internet, so it cannot send anything
  to the developer or to anyone else.

## What the app uses on your phone, and why

| Permission | What for |
| --- | --- |
| Camera | To read the blinking LED of the Raspberry Pi you chose. Frames are read on the phone as they arrive. They are not recorded, stored or sent anywhere. |
| Bluetooth | To find the Raspberry Pi and talk to it. |
| Location | On Android 11 and earlier, Android requires it for Bluetooth scanning. On every version, Android requires it to tell an app the name of the WiFi network the phone is on, which the app uses only to mark that network in the list. The location itself is never read. |
| WiFi state | To read that network name. |

## What goes to the Raspberry Pi

The WiFi network name and passphrase you enter go to the Raspberry Pi you paired with, over
Bluetooth, encrypted under a key made for that pairing. The Pi saves them in its own network
settings so that it can join the network. They go nowhere else, and the app does not keep
them.

The one-time key read from the LED is held in memory for the pairing and is not saved. The
only thing the app saves is the LED speed you chose, on your phone.

## Children

The app is not directed at children and collects nothing from anyone.

## Changes and contact

A change to this policy is published at this address. Questions go to the project's issue
tracker: https://github.com/tomdionysus/rpiledconfig/issues
