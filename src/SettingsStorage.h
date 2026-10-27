#pragma once
#include <stdint.h>
#include "TouchCal.h"
#include "Theme.h"
#include "AppProtocol.h"

struct Settings {
  bool demo;            // demo/test telemetry (default ON until real protocol exists)
  bool bleScan;         // passive BLE scan list
  bool phone;           // iPhone link (Bluetooth media keys)
  bool invert;          // panel colour inversion
  uint8_t rotation;     // 1 or 3 (landscape)
  uint8_t backlightPct; // 10..100
  bool hwConfirmed;     // user accepted the hardware test for this profile
  char targetAddr[18];  // chosen scooter (BLE address), empty = none
  uint8_t targetType;
  char targetName[20];
  uint8_t theme;        // colour theme index
  uint8_t bootStyle;    // 0 classic, 1 name, 2 image, 3 minimal
  char bootText[16];    // owner text for the boot screen
  bool bootImgValid;    // 128x64 image stored
  bool limValid;        // limits saved before the 20 km/h cap (restore data)
  uint16_t limRaw[3];
  uint16_t limWf;
  Settings() : demo(true), bleScan(false), phone(false), invert(false), rotation(1), backlightPct(80), hwConfirmed(false), targetType(0), theme(0), bootStyle(0), bootImgValid(false), limValid(false), limWf(0) { bootText[0] = 0; targetAddr[0] = 0; targetName[0] = 0; limRaw[0] = limRaw[1] = limRaw[2] = 0; }
};

class SettingsStorage {
 public:
  void begin();
  Settings& get() { return s_; }
  void save();
  bool loadBootImg(uint8_t* out1024);
  void saveBootImg(const uint8_t* in1024);
  bool loadCal(uint8_t rotation, TouchCal& out);
  void saveCal(uint8_t rotation, const TouchCal& cal);
 private:
  Settings s_;
};
