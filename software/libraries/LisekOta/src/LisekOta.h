// Firmware updates and version readout over Bluetooth LE (web flasher:
// software/web-flasher, any Chromium browser).
//
// The device advertises as "lisek-R-xxxx" (receiver) or "lisek-T-xxxx"
// (transmitter) while it is on (until hideAfter()), with its role, state, id
// and firmware version in the advertisement (fleet view). Anyone can read
// those; it only
// installs images signed with the project key (ECDSA P-256, public half in
// lisek_pubkey.h) and built for its own role.
//
// Rollback: a freshly installed image boots "pending". It must be confirmed
// over BLE (the web flasher does it right after the reboot) and the sketch
// must have called markHealthy() (radio module answered). If either is
// missing after CONFIRM_TIMEOUT_MS, or the image crashes or resets before
// that, the bootloader goes back to the previous image.
#pragma once
#include <Arduino.h>

#ifndef LISEK_VERSION
#define LISEK_VERSION "dev"
#endif

namespace LisekOta {

// role: "receiver" or "transmitter". Call at the very start of setup().
void begin(const char *role);

// The sketch's own check passed (radio module answers). Required before an
// installed update can be confirmed.
void markHealthy();

// Transmitter id / channel the device is set to, shown in the fleet view.
void setId(uint8_t id);

// Settings changed from the web panel ("set key=value" over BLE). The handler
// runs on the Bluetooth task: store the value and return true, or return
// false to reject it. Only the panel sends these, so nothing on the device
// itself can change them by accident.
void onSetting(bool (*handler)(const String &key, const String &value));

// Four bits shown in the fleet view (advertised and in the info JSON as
// "user"); meaning is per role. Receiver: bit 0 = binary LED mode, bits 1-3 =
// number of channels - 1. Transmitter: index into 0/10/30/60 hide minutes.
void setUserBits(uint8_t value);

// Stop advertising this many minutes after power-on (0 = never), so phones
// cannot find the device by its Bluetooth signal during the game. Not while a
// client is connected, an update is in progress or an update awaits
// confirmation. Visible again only after a restart.
void hideAfter(uint32_t minutes);

// True while an update is being received (the sketch can show it on LEDs).
bool updating();

// Bytes received / total of the update in progress.
uint32_t progress(uint32_t *total);

}  // namespace LisekOta
