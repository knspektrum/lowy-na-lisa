// Firmware updates and version readout over Bluetooth LE (web flasher:
// software/web-flasher, any Chromium browser).
//
// The device advertises as "lisek-R-xxxx" (receiver) or "lisek-T-xxxx"
// (transmitter) all the time it is on, with its role, state, id and firmware
// version in the advertisement (fleet view). Anyone can read those; it only
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

// True while an update is being received (the sketch can show it on LEDs).
bool updating();

// Bytes received / total of the update in progress.
uint32_t progress(uint32_t *total);

}  // namespace LisekOta
