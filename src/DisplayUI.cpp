#include "DisplayUI.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <esp_system.h>
#include "BoardConfig.h"
#include "Format.h"
#include "ScooterProtocol.h"
#include "Diagnostics.h"

namespace {
const int16_t kW = 320;
const int16_t kH = 240;
const uint32_t kHoldMs = 1500;

DisplayUI::Btn mk(int16_t x, int16_t y, int16_t w, int16_t h) {
  DisplayUI::Btn b; b.x = x; b.y = y; b.w = w; b.h = h; return b;
}
DisplayUI::Btn bottomBtn(int i) { return mk((int16_t)(8 + i * 104), 206, 96, 30); }
DisplayUI::Btn dashBtn(int i) { return mk((int16_t)(6 + i * 80), 206, 72, 30); }
DisplayUI::Btn gridBtn(int i) { return mk((int16_t)(6 + (i % 4) * 79), (int16_t)(36 + (i / 4) * 44), 74, 40); }
DisplayUI::Btn holdBtn() { return mk(40, 150, 240, 40); }
bool inBtn(const DisplayUI::Btn& b, int16_t x, int16_t y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}
}  // namespace

// ------------------------------------------------------------------ lifecycle

void DisplayUI::begin(const UiContext& ctx) {
  c_ = ctx;
  memset(cache_, 0, sizeof(cache_));
  memset(cacheCol_, 0, sizeof(cacheCol_));
  calMsg_[0] = 0;
  tft_.init();
  applyTheme(c_.store->get().theme);
  bootImgOk_ = c_.store->loadBootImg(bootImg_);
  applyDisplaySettings();
  splash();

  const Settings& s = c_.store->get();
  hwPage_ = 0;
  if (!cal_.valid) startCal(s.hwConfirmed ? Screen::Dash : Screen::Hw);
  else if (!s.hwConfirmed) go(Screen::Hw);
  else go(Screen::Dash);
}

void DisplayUI::go(Screen s) {
  if (screen_ == Screen::Ctrl && s != Screen::Ctrl) c_.ble->setWritesUnlocked(false);  // leaving CTRL relocks
  screen_ = s;
  full_ = true;
  gest_ = GestureDetector();
  hold_ = HoldTracker();
}

void DisplayUI::setBacklight(uint8_t pct) {
  static bool attached = false;
  if (!attached) {
    ledcSetup(0, 5000, 8);
    ledcAttachPin(board::kBacklightPin, 0);
    attached = true;
  }
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  ledcWrite(0, (uint32_t)pct * 255u / 100u);
}

void DisplayUI::applyDisplaySettings() {
  const Settings& s = c_.store->get();
  tft_.setRotation(s.rotation);
  tft_.invertDisplay(s.invert);
  setBacklight(s.backlightPct);
  TouchCal tc;
  if (c_.store->loadCal(s.rotation, tc)) cal_ = tc; else cal_ = TouchCal();
}

void DisplayUI::applyTheme(uint8_t idx) {
  if (idx >= kThemeCount) idx = 0;
  const ThemeDef& t = kThemes[idx];
  cBg_ = tft_.color565(t.bg.r, t.bg.g, t.bg.b);
  cPanel_ = tft_.color565(t.panel.r, t.panel.g, t.panel.b);
  cNeon_ = tft_.color565(t.neon.r, t.neon.g, t.neon.b);
  cGlow_ = tft_.color565(t.glow.r, t.glow.g, t.glow.b);
  cDim_ = tft_.color565(t.dim.r, t.dim.g, t.dim.b);
  cWarn_ = tft_.color565(t.warn.r, t.warn.g, t.warn.b);
  cOk_ = tft_.color565(t.ok.r, t.ok.g, t.ok.b);
  cBad_ = tft_.color565(t.bad.r, t.bad.g, t.bad.b);
  cText_ = tft_.color565(t.text.r, t.text.g, t.text.b);
}

void DisplayUI::splash() {
  const Settings& s = c_.store->get();
  uint8_t style = s.bootStyle;
  if (style == 2 && !bootImgOk_) style = 0;  // no stored image: classic screen
  tft_.fillScreen(cBg_);
  tft_.setTextPadding(0);
  tft_.setTextDatum(MC_DATUM);
  if (style == 0) {
    tft_.setTextColor(cNeon_, cBg_);
    tft_.setTextSize(2);
    tft_.drawString("NOVA", 160, 80, 4);
    tft_.setTextSize(1);
    tft_.setTextColor(cText_, cBg_);
    tft_.drawString("RIDE OS", 160, 130, 4);
  } else if (style == 1) {
    const char* name = s.bootText[0] ? s.bootText : "NOVA";
    tft_.setTextColor(cNeon_, cBg_);
    tft_.setTextSize(strlen(name) <= 8 ? 2 : 1);
    tft_.drawString(name, 160, 80, 4);
    tft_.setTextSize(1);
    tft_.setTextColor(cDim_, cBg_);
    tft_.drawString("RIDE OS", 160, 130, 2);
  } else if (style == 2) {
    tft_.drawBitmap(96, 30, bootImg_, app::kBootImgW, app::kBootImgH, cNeon_);
    tft_.setTextColor(cText_, cBg_);
    tft_.drawString(s.bootText[0] ? s.bootText : "NOVA RIDE OS", 160, 125, 4);
  }
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString(NOVA_FW_VERSION " / " NOVA_PROFILE_NAME, 160, 205, 2);
  for (int x = 40; x <= 280; x += 8) {
    tft_.drawFastHLine(40, 160, x - 40, cNeon_);
    delay(6);
  }
  delay(250);
}

void DisplayUI::toggleInvert() {
  Settings& s = c_.store->get();
  s.invert = !s.invert;
  tft_.invertDisplay(s.invert);
  c_.store->save();
  full_ = true;
}

void DisplayUI::flipRotation() {
  Settings& s = c_.store->get();
  s.rotation = (s.rotation == 1) ? 3 : 1;
  c_.store->save();
  applyDisplaySettings();
  const Screen ret = (screen_ == Screen::Cal) ? calReturn_ : screen_;
  if (cal_.valid) go(ret); else startCal(ret);
}

void DisplayUI::onBootButton(bool longPress) {
  if (longPress) flipRotation(); else toggleInvert();
}

void DisplayUI::startCal(Screen returnTo) {
  calReturn_ = returnTo;
  screen_ = Screen::Cal;
  full_ = true;
  calStep_ = 0;
  calTouching_ = false;
  calWaitRelease_ = true;  // require a release before the first target counts
  gest_ = GestureDetector();
}

// ------------------------------------------------------------------ drawing helpers

void DisplayUI::txt(uint8_t slot, int16_t x, int16_t y, uint8_t font, const char* s,
                    uint16_t fg, uint8_t datum, int16_t padW) {
  if (slot >= 16) return;
  if (strcmp(cache_[slot], s) == 0 && cacheCol_[slot] == fg) return;
  tft_.setTextColor(fg, cBg_);
  tft_.setTextDatum(datum);
  tft_.setTextPadding(padW);
  tft_.drawString(s, x, y, font);
  tft_.setTextPadding(0);
  strlcpy(cache_[slot], s, sizeof(cache_[slot]));
  cacheCol_[slot] = fg;
}

void DisplayUI::drawBtn(const Btn& b, const char* label, bool enabled, uint16_t accent) {
  tft_.fillRoundRect(b.x, b.y, b.w, b.h, 6, cPanel_);
  tft_.drawRoundRect(b.x, b.y, b.w, b.h, 6, enabled ? accent : cDim_);
  tft_.setTextColor(enabled ? cText_ : cDim_, cPanel_);
  tft_.setTextDatum(MC_DATUM);
  tft_.setTextPadding(0);
  tft_.drawString(label, b.x + b.w / 2, b.y + b.h / 2, 2);
}

void DisplayUI::drawHeader(const char* title) {
  tft_.fillScreen(cBg_);
  tft_.setTextDatum(TC_DATUM);
  tft_.setTextPadding(0);
  tft_.setTextColor(cNeon_, cBg_);
  tft_.drawString(title, 160, 3, 4);
  tft_.drawFastHLine(0, 32, kW, cNeon_);
  tft_.drawFastHLine(0, 33, kW, cGlow_);
}

void DisplayUI::drawFull() {
  memset(cache_, 0, sizeof(cache_));
  memset(cacheCol_, 0, sizeof(cacheCol_));
  switch (screen_) {
    case Screen::Cal: drawCal(); break;
    case Screen::Hw: drawHw(); break;
    case Screen::Dash: drawDash(); break;
    case Screen::Music: drawMusic(); break;
    case Screen::Msg: drawMsg(); break;
    case Screen::Settings: drawSettings(); break;
    case Screen::Safety: drawSafety(); break;
    case Screen::Diag: drawDiag(); break;
    case Screen::Ctrl: drawCtrl(); break;
    case Screen::Update: drawUpdate(); break;
  }
}

// ------------------------------------------------------------------ main update

void DisplayUI::update(const Telemetry& t, const TouchState& ts, uint32_t now) {
  const applink::Applied ap = applink::applyPending();
  if (ap.theme) { applyTheme(c_.store->get().theme); full_ = true; }
  if (ap.boot) bootImgOk_ = c_.store->loadBootImg(bootImg_);
  if (ap.boot || ap.preview) { splash(); full_ = true; }
  if (screen_ == Screen::Cal) { tickCal(ts, now); return; }

  int16_t mx = 0, my = 0;
  const bool touching = ts.down && cal_.valid;
  if (touching) cal_.map(ts.rawX, ts.rawY, kW, kH, mx, my);

  if (full_) { drawFull(); full_ = false; }
  const GestureEvent ev = gest_.update(touching, mx, my, now);

  switch (screen_) {
    case Screen::Hw: tickHw(touching, mx, my, ev); break;
    case Screen::Dash: tickDash(t, touching, mx, my, ev, now); break;
    case Screen::Music: tickMusic(ev); break;
    case Screen::Msg: tickMsg(ev); break;
    case Screen::Settings: tickSettings(ev); break;
    case Screen::Safety: tickSafety(touching, mx, my, ev, now); break;
    case Screen::Diag: tickDiag(ev, now); break;
    case Screen::Ctrl: tickCtrl(touching, mx, my, ev, now); break;
    case Screen::Update: tickUpdate(touching, mx, my, ev, now); break;
    default: break;
  }
}

// ------------------------------------------------------------------ calibration

void DisplayUI::drawCalTarget() {
  int16_t tx = 20, ty = 20;
  if (calStep_ == 1) { tx = kW - 20; ty = 20; }
  if (calStep_ == 2) { tx = kW - 20; ty = kH - 20; }
  tft_.drawFastHLine(tx - 14, ty, 29, cNeon_);
  tft_.drawFastVLine(tx, ty - 14, 29, cNeon_);
  tft_.drawCircle(tx, ty, 7, cNeon_);
  tft_.fillCircle(tx, ty, 2, cText_);
}

void DisplayUI::drawCal() {
  tft_.fillScreen(cBg_);
  tft_.setTextPadding(0);
  tft_.setTextDatum(MC_DATUM);
  tft_.setTextColor(cText_, cBg_);
  char b[32];
  snprintf(b, sizeof(b), "TOUCH CALIBRATION %u/3", (unsigned)(calStep_ + 1));
  tft_.drawString(b, 160, 100, 2);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString("Tap the centre of the crosshair", 160, 122, 2);
  tft_.drawString("BOOT: invert (short) / flip (long)", 160, 142, 2);
  if (calMsg_[0]) {
    tft_.setTextColor(cBad_, cBg_);
    tft_.drawString(calMsg_, 160, 170, 2);
  }
  drawCalTarget();
}

void DisplayUI::tickCal(const TouchState& ts, uint32_t now) {
  if (full_) { drawCal(); full_ = false; }
  if (calWaitRelease_) { if (!ts.down) calWaitRelease_ = false; return; }
  if (!ts.down) { calTouching_ = false; return; }
  if (!calTouching_) { calTouching_ = true; calT0_ = now; calSumX_ = 0; calSumY_ = 0; calN_ = 0; }
  calSumX_ += ts.rawX; calSumY_ += ts.rawY; ++calN_;
  if (now - calT0_ < 150 || calN_ <= 0) return;

  calPts_[calStep_].x = (int16_t)(calSumX_ / calN_);
  calPts_[calStep_].y = (int16_t)(calSumY_ / calN_);
  calTouching_ = false;
  calWaitRelease_ = true;
  if (++calStep_ < 3) { full_ = true; return; }

  TouchCal tc;
  if (TouchCal::compute(calPts_[0], calPts_[1], calPts_[2], kW, kH, 20, tc)) {
    cal_ = tc;
    c_.store->saveCal(c_.store->get().rotation, tc);
    calMsg_[0] = 0;
    Diag::log("cal ok swap=%d", tc.swapXY ? 1 : 0);
    go(calReturn_);
  } else {
    strlcpy(calMsg_, "Calibration failed - try again", sizeof(calMsg_));
    Diag::log("cal failed");
    calStep_ = 0;
    full_ = true;
  }
}

// ------------------------------------------------------------------ hardware test

void DisplayUI::hwButtons() {
  const char* a = ""; const char* b = ""; const char* c = "";
  switch (hwPage_) {
    case 0: case 1: a = "INVERT"; b = "FLIP"; c = "NEXT"; break;
    case 2: a = "CALIBRATE"; b = "CLEAR"; c = "NEXT"; break;
    default: a = "BACK"; b = ""; c = "ACCEPT"; break;
  }
  if (a[0]) drawBtn(bottomBtn(0), a, true, cNeon_);
  if (b[0]) drawBtn(bottomBtn(1), b, true, cNeon_);
  if (c[0]) drawBtn(bottomBtn(2), c, true, hwPage_ == 3 ? cOk_ : cNeon_);
}

void DisplayUI::drawHw() {
  tft_.fillScreen(cBg_);
  tft_.setTextPadding(0);
  tft_.setTextDatum(TL_DATUM);
  tft_.setTextColor(cText_, cBg_);
  char h[48];
  switch (hwPage_) {
    case 0: {
      snprintf(h, sizeof(h), "HW 1/4 COLORS  %s", NOVA_PROFILE_NAME);
      tft_.drawString(h, 4, 4, 2);
      const uint16_t cols[8] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK, TFT_CYAN, TFT_MAGENTA, TFT_YELLOW};
      const char* names[8] = {"RED", "GRN", "BLU", "WHT", "BLK", "CYN", "MAG", "YEL"};
      for (int i = 0; i < 8; ++i) tft_.fillRect(i * 40, 24, 40, 126, cols[i]);
      tft_.drawRect(4 * 40, 24, 40, 126, cDim_);
      tft_.setTextDatum(TC_DATUM);
      for (int i = 0; i < 8; ++i) tft_.drawString(names[i], i * 40 + 20, 154, 2);
      tft_.setTextDatum(TL_DATUM);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString("Negative/odd colours? tap INVERT", 4, 174, 2);
      tft_.drawString("RED looks blue? flash *_swaprb build", 4, 190, 2);
      break;
    }
    case 1: {
      tft_.drawString("HW 2/4 ORIENTATION", 4, 4, 2);
      tft_.drawRect(0, 0, kW, kH, cNeon_);
      tft_.drawRect(1, 1, kW - 2, kH - 2, cGlow_);
      tft_.drawLine(0, 0, kW - 1, kH - 1, cGlow_);
      tft_.drawLine(kW - 1, 0, 0, kH - 1, cGlow_);
      tft_.setTextColor(cText_, cBg_);
      tft_.drawString("TL", 8, 24, 2);
      tft_.setTextDatum(TR_DATUM); tft_.drawString("TR", kW - 8, 24, 2);
      tft_.setTextDatum(TL_DATUM); tft_.drawString("BL", 8, 180, 2);
      tft_.setTextDatum(TR_DATUM); tft_.drawString("BR", kW - 8, 180, 2);
      tft_.setTextDatum(MC_DATUM);
      tft_.setTextColor(cNeon_, cBg_);
      tft_.drawString("NOVA 123", 160, 100, 4);
      char d[32];
      snprintf(d, sizeof(d), "%dx%d rot %u", (int)tft_.width(), (int)tft_.height(),
               (unsigned)c_.store->get().rotation);
      tft_.setTextColor(cText_, cBg_);
      tft_.drawString(d, 160, 132, 2);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString("Text must read normally", 160, 152, 2);
      break;
    }
    case 2:
      tft_.drawString("HW 3/4 TOUCH", 4, 4, 2);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString("Touch the screen: dot must follow finger", 4, 24, 2);
      tft_.drawString("Wrong? CALIBRATE again (BOOT = flip)", 4, 44, 2);
      break;
    default:
      tft_.drawString("HW 4/4 ACCEPT", 4, 4, 2);
      tft_.setTextColor(cDim_, cBg_);
      tft_.drawString("Colours, orientation, touch all OK?", 4, 34, 2);
      tft_.drawString("ACCEPT stores this profile on the device.", 4, 54, 2);
      tft_.setTextColor(cWarn_, cBg_);
      tft_.drawString("Scooter link: NOT SUPPORTED", 4, 94, 2);
      tft_.drawString("Dashboard starts in DEMO mode.", 4, 114, 2);
      break;
  }
  hwButtons();
}

void DisplayUI::tickHw(bool touching, int16_t mx, int16_t my, const GestureEvent& ev) {
  Settings& s = c_.store->get();
  if (hwPage_ == 2 && touching && my < 200) {
    tft_.fillCircle(mx, my, 3, cNeon_);
    char b[44];
    snprintf(b, sizeof(b), "map %3d,%3d  rot %u", (int)mx, (int)my, (unsigned)s.rotation);
    txt(0, 8, 176, 2, b, cText_, TL_DATUM, 300);
  }
  if (ev.type != GestureType::Tap) return;
  int idx = -1;
  for (int i = 0; i < 3; ++i) if (inBtn(bottomBtn(i), ev.x, ev.y)) idx = i;
  if (idx < 0) return;
  switch (hwPage_) {
    case 0: case 1:
      if (idx == 0) toggleInvert();
      else if (idx == 1) flipRotation();
      else { ++hwPage_; full_ = true; }
      break;
    case 2:
      if (idx == 0) startCal(Screen::Hw);
      else if (idx == 1) full_ = true;
      else { hwPage_ = 3; full_ = true; }
      break;
    default:
      if (idx == 0) { hwPage_ = 0; full_ = true; }
      else if (idx == 2) { s.hwConfirmed = true; c_.store->save(); go(Screen::Dash); }
      break;
  }
}

// ------------------------------------------------------------------ dashboard

namespace {
const int kGcx = 88, kGcy = 126, kGr0 = 66, kGr1 = 80, kSegs = 27;   // speed ring: 270 degrees, 27 segments
const uint32_t kModeHoldMs = 800;
const float kPowerBarMaxW = 1000.0f;   // full scale of the power bar
const char* const kModeLabel[4] = {"WALK", "ECO", "DRIVE", "SPORT"};
DisplayUI::Btn setBtn() { return mk(266, 3, 48, 22); }
}  // namespace

void DisplayUI::drawSeg(int i, bool on) {
  const float k = 0.0174533f;
  const float a0 = (135.0f + 10.0f * (float)i) * k;
  const float a1 = (135.0f + 10.0f * (float)i + 9.0f) * k;
  const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
  const int x0 = kGcx + (int)lroundf((float)kGr0 * c0), y0 = kGcy + (int)lroundf((float)kGr0 * s0);
  const int x1 = kGcx + (int)lroundf((float)kGr1 * c0), y1 = kGcy + (int)lroundf((float)kGr1 * s0);
  const int x2 = kGcx + (int)lroundf((float)kGr1 * c1), y2 = kGcy + (int)lroundf((float)kGr1 * s1);
  const int x3 = kGcx + (int)lroundf((float)kGr0 * c1), y3 = kGcy + (int)lroundf((float)kGr0 * s1);
  const uint16_t col = on ? (i >= 22 ? cWarn_ : cNeon_) : cPanel_;
  tft_.fillTriangle(x0, y0, x1, y1, x2, y2, col);
  tft_.fillTriangle(x0, y0, x2, y2, x3, y3, col);
}

void DisplayUI::modeState(uint32_t now, int& active, bool& enabled) {
  const bool demo = c_.store->get().demo;
  const bool linked = !demo && c_.ble->state() == BleState::Connected;
  xbot::Readback rb;
  const bool have = linked && c_.ble->getReadback(rb, now);
  active = -1;
  if (demo) active = (int)c_.ctrl->mode();
  else if (have && rb.modeValid) active = (int)xbot::internalFromModeIndex(rb.modeIdx);
  enabled = demo || (linked && have && rb.modeValid);
}

void DisplayUI::drawModeButtons() {
  int active;
  bool en;
  modeState(millis(), active, en);
  modeKey_ = active * 2 + (en ? 1 : 0);
  for (int i = 0; i < 4; ++i) {
    const Btn b = dashBtn(i);
    drawBtn(b, kModeLabel[i], en, i == active ? cOk_ : cNeon_);
    if (i == active && en) tft_.drawRoundRect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, 5, cOk_);
  }
  holdModeDrawn_ = 0;
}

void DisplayUI::drawDash() {
  tft_.fillScreen(cBg_);
  tft_.drawFastHLine(0, 28, kW, cNeon_);
  tft_.drawFastHLine(0, 29, kW, cGlow_);
  drawBtn(setBtn(), "SET", true, cNeon_);
  for (int i = 0; i < kSegs; ++i) drawSeg(i, false);
  segOn_ = 0;
  tft_.setTextPadding(0);
  tft_.setTextColor(cDim_, cBg_);
  tft_.setTextDatum(MC_DATUM);
  tft_.drawString("km/h", kGcx, 166, 4);
  tft_.setTextDatum(TR_DATUM);
  tft_.drawString("W", 314, 58, 4);
  tft_.drawRoundRect(183, 80, 124, 10, 3, cDim_);   // power bar
  tft_.fillRect(184, 81, 122, 8, cPanel_);
  tft_.drawRoundRect(183, 139, 124, 12, 3, cDim_);  // battery bar
  tft_.fillRect(184, 140, 122, 10, cPanel_);
  battW_ = -1;
  pwrW_ = -1;
  drawModeButtons();
}

void DisplayUI::tickDash(const Telemetry& t, bool touching, int16_t mx, int16_t my,
                         const GestureEvent& ev, uint32_t now) {
  // header: BLE state and data source
  const BleState bs = c_.ble->state();
  const char* bt = "BLE OFF";
  uint16_t bc = cDim_;
  switch (bs) {
    case BleState::Scanning: bt = "BLE SCAN"; bc = cNeon_; break;
    case BleState::CandidateSeen: bt = "BLE:SCOOTER?"; bc = cWarn_; break;
    case BleState::Connecting: bt = "BLE CONN..."; bc = cWarn_; break;
    case BleState::Connected: bt = "BLE LINK"; bc = cOk_; break;
    default: break;
  }
  txt(0, 6, 7, 2, bt, bc, TL_DATUM, 112);
  if (t.isDemo) txt(1, 200, 7, 2, "DEMO", cWarn_, TC_DATUM, 56);
  else if (t.speedValid || t.battPctValid || t.voltValid) txt(1, 200, 7, 2, "LIVE", cOk_, TC_DATUM, 56);
  else txt(1, 200, 7, 2, "NO DATA", cBad_, TC_DATUM, 64);
  const PhoneLink pl = c_.phone->info().link;
  if (pl == PhoneLink::Connected) txt(13, 124, 7, 2, "PHONE", cOk_, TL_DATUM, 46);
  else if (pl == PhoneLink::Advertising) txt(13, 124, 7, 2, "PH ..", cDim_, TL_DATUM, 46);
  else txt(13, 124, 7, 2, " ", cDim_, TL_DATUM, 46);

  // mode name inside the ring
  char mname[12];
  mname[0] = 0;
  if (t.modeValid) {
    size_t n = 0;
    while (t.mode[n] && t.mode[n] != ' ' && n < sizeof(mname) - 1) { mname[n] = t.mode[n]; ++n; }
    mname[n] = 0;
  }
  txt(3, kGcx, 70, 2, mname[0] ? mname : "--", mname[0] ? cOk_ : cDim_, MC_DATUM, 70);

  // speed in the middle of the ring
  char n[12], b[24];
  if (t.speedValid) {
    fmtFixed(n, sizeof(n), true, t.speedKmh, 0);
    txt(4, kGcx, 118, (t.speedKmh >= 99.5f) ? 7 : 8, n, cText_, MC_DATUM, 112);
  } else if (strcmp(cache_[4], "--") != 0) {
    tft_.fillRect(kGcx - 56, 82, 112, 76, cBg_);
    tft_.fillRect(kGcx - 28, 114, 24, 8, cDim_);
    tft_.fillRect(kGcx + 4, 114, 24, 8, cDim_);
    strlcpy(cache_[4], "--", sizeof(cache_[4]));
  }

  // eased ring fill, 0..35 km/h
  const float target = t.speedValid ? fminf(t.speedKmh / 35.0f, 1.0f) : 0.0f;
  gauge_ += (target - gauge_) * 0.3f;
  int on = (int)(gauge_ * (float)kSegs + 0.5f);
  if (on > kSegs) on = kSegs;
  while (segOn_ < on) drawSeg(segOn_++, true);
  while (segOn_ > on) drawSeg(--segOn_, false);

  // temperatures under the speed
  char t1[8], t2[8];
  fmtFixed(t1, sizeof(t1), t.tempValid, t.tempC, 0);
  fmtFixed(t2, sizeof(t2), t.motorTempValid, t.motorTempC, 0);
  snprintf(b, sizeof(b), "CTRL %s%s  MOT %s%s", t1, t.tempValid ? "C" : "", t2, t.motorTempValid ? "C" : "");
  txt(12, kGcx, 186, 2, b, cDim_, TC_DATUM, 122);

  // power (big) with bar, battery current, battery, voltage
  if (t.powerValid) { fmtFixed(n, sizeof(n), true, (float)t.powerW, 0); txt(5, 292, 30, 6, n, cText_, TR_DATUM, 104); }
  else txt(5, 292, 40, 4, "--", cDim_, TR_DATUM, 104);
  {
    int pw = 0;
    uint16_t pc = cNeon_;
    if (t.powerValid) {
      const float a = fabsf((float)t.powerW);
      pw = (int)(fminf(a / kPowerBarMaxW, 1.0f) * 122.0f);
      pc = t.powerW < 0 ? cOk_ : (a > 600.0f ? cWarn_ : cNeon_);
    }
    if (pw != pwrW_ || pc != pwrCol_) {
      tft_.fillRect(184, 81, 122, 8, cPanel_);
      if (pw > 0) tft_.fillRect(184, 81, pw, 8, pc);
      pwrW_ = pw;
      pwrCol_ = pc;
    }
  }
  fmtFixed(n, sizeof(n), t.currentValid, t.currentA, 1);
  snprintf(b, sizeof(b), "BATT %s A", n);
  txt(11, 184, 94, 2, b, t.currentValid ? cText_ : cDim_, TL_DATUM, 130);
  uint16_t col = cDim_;
  if (t.battPctValid) col = t.battPct > 50 ? cOk_ : (t.battPct > 20 ? cWarn_ : cBad_);
  if (t.battPctValid) snprintf(b, sizeof(b), "%u%%", (unsigned)t.battPct); else snprintf(b, sizeof(b), "--");
  txt(6, 184, 112, 4, b, col, TL_DATUM, 62);
  const int bw = t.battPctValid ? (int)t.battPct * 122 / 100 : 0;
  if (bw != battW_ || col != battCol_) {
    tft_.fillRect(184, 140, 122, 10, cPanel_);
    if (bw > 0) tft_.fillRect(184, 140, bw, 10, col);
    battW_ = bw;
    battCol_ = col;
  }
  fmtFixed(n, sizeof(n), t.voltValid, t.volts, 1);
  snprintf(b, sizeof(b), t.voltValid ? "%sV" : "%s", n);
  txt(7, 306, 112, 4, b, t.voltValid ? cText_ : cDim_, TR_DATUM, 100);

  // trip / total / status (or mode-change feedback)
  fmtFixed(n, sizeof(n), t.tripValid, t.tripKm, 1);
  snprintf(b, sizeof(b), "TRIP %s km", n);
  txt(8, 184, 156, 2, b, cDim_, TL_DATUM, 130);
  fmtFixed(n, sizeof(n), t.totalValid, t.totalKm, 0);
  snprintf(b, sizeof(b), "TOTAL %s km", n);
  txt(9, 184, 171, 2, b, cDim_, TL_DATUM, 130);
  const WriteStatus ws = c_.ble->writeStatus();
  const bool recent = ws.state != WriteState::Idle && ws.kind == WriteKind::Mode &&
                      (ws.state == WriteState::Pending || now - ws.doneMs < 4000);
  if (recent) {
    if (ws.state == WriteState::Pending) txt(10, 184, 186, 2, "MODE: SENDING...", cWarn_, TL_DATUM, 130);
    else if (ws.state == WriteState::Confirmed) txt(10, 184, 186, 2, "MODE: CONFIRMED", cOk_, TL_DATUM, 130);
    else if (ws.state == WriteState::NotConfirmed) txt(10, 184, 186, 2, "MODE: NOT CONFIRMED", cBad_, TL_DATUM, 130);
    else { snprintf(b, sizeof(b), "MODE: %s", ws.note); txt(10, 184, 186, 2, b, cBad_, TL_DATUM, 130); }
  } else if (t.faultValid && t.faultCode != 0) {
    snprintf(b, sizeof(b), "FAULT %u", (unsigned)t.faultCode);
    txt(10, 184, 186, 2, b, cBad_, TL_DATUM, 130);
  } else {
    txt(10, 184, 186, 2, t.faultValid ? "STATUS OK" : "STATUS --", cDim_, TL_DATUM, 130);
  }

  // mode buttons: a short deliberate hold (0.8 s) changes the mode
  int active;
  bool en;
  modeState(now, active, en);
  if (active * 2 + (en ? 1 : 0) != modeKey_) drawModeButtons();
  int inside = -1;
  if (touching && en) for (int i = 0; i < 4; ++i) if (inBtn(dashBtn(i), mx, my)) inside = i;
  if (inside != holdModeIdx_) { holdModeIdx_ = inside; holdMode_ = HoldTracker(); if (holdModeDrawn_ != 0) drawModeButtons(); }
  if (inside >= 0 && inside != active) {
    if (holdMode_.update(true, now, kModeHoldMs)) {
      const bool demo = c_.store->get().demo;
      if (demo) c_.ctrl->setMode((RideMode)inside, true);
      else c_.ble->requestWriteOnce(WriteKind::Mode, xbot::modeIndexFromInternal((uint8_t)inside));
      drawModeButtons();
    } else {
      const Btn bb = dashBtn(inside);
      const int w = (int)(holdMode_.progress(now, kModeHoldMs) * (float)(bb.w - 8));
      if (w != holdModeDrawn_) {
        if (w > holdModeDrawn_) tft_.fillRect(bb.x + 4 + holdModeDrawn_, bb.y + bb.h - 6, w - holdModeDrawn_, 3, cNeon_);
        holdModeDrawn_ = w;
      }
    }
  }

  if (ev.type == GestureType::SwipeUp) { go(Screen::Safety); return; }
  if (ev.type == GestureType::Tap && inBtn(setBtn(), ev.x, ev.y)) go(Screen::Settings);
}

// ------------------------------------------------------------------ music / messages

namespace {
DisplayUI::Btn musicBtn(int i) { return mk((int16_t)(8 + i * 104), 136, 96, 44); }
}  // namespace

void DisplayUI::drawMusic() {
  drawHeader("MUSIC");
  const MediaInfo& mi = c_.phone->info();
  musicKey_ = (int)mi.link;
  tft_.setTextPadding(0);
  tft_.setTextDatum(TC_DATUM);
  const char* st = "PHONE: OFF";
  const char* st2 = "Settings > PHONE ON";
  uint16_t sc = cDim_;
  if (mi.link == PhoneLink::Connected) { st = "PHONE: CONNECTED"; sc = cOk_; st2 = "Controls act on the playing app"; }
  else if (mi.link == PhoneLink::Advertising) { st = "PHONE: WAITING"; sc = cWarn_; st2 = "Pair NOVA RIDE in iPhone Bluetooth"; }
  tft_.setTextColor(sc, cBg_);
  tft_.drawString(st, 160, 40, 2);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString(st2, 160, 58, 2);
  tft_.setTextColor(cText_, cBg_);
  tft_.drawString(mi.trackValid ? mi.title : "--", 160, 80, 4);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString(mi.trackValid ? mi.artist : "Title needs AMS (next step)", 160, 112, 2);
  const bool en = c_.phone->available();
  drawBtn(musicBtn(0), "PREV", en, cNeon_);
  drawBtn(musicBtn(1), "PLAY/PAUSE", en, cNeon_);
  drawBtn(musicBtn(2), "NEXT", en, cNeon_);
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  drawBtn(bottomBtn(2), "MSG", true, cNeon_);
}

void DisplayUI::tickMusic(const GestureEvent& ev) {
  if ((int)c_.phone->info().link != musicKey_) drawMusic();
  if (ev.type != GestureType::Tap) return;
  if (inBtn(bottomBtn(0), ev.x, ev.y)) { go(Screen::Dash); return; }
  if (inBtn(bottomBtn(2), ev.x, ev.y)) { go(Screen::Msg); return; }
  if (!c_.phone->available()) return;  // keys only go out with a live iPhone link
  if (inBtn(musicBtn(0), ev.x, ev.y)) c_.phone->prev();
  else if (inBtn(musicBtn(1), ev.x, ev.y)) c_.phone->playPause();
  else if (inBtn(musicBtn(2), ev.x, ev.y)) c_.phone->next();
}

void DisplayUI::drawMsg() {
  drawHeader("MESSAGES");
  tft_.setTextPadding(0);
  tft_.setTextDatum(TC_DATUM);
  tft_.setTextColor(cWarn_, cBg_);
  tft_.drawString("NOT AVAILABLE", 160, 70, 4);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString("Needs iOS notifications (ANCS).", 160, 114, 2);
  tft_.drawString("Next step. Nothing is stored.", 160, 134, 2);
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
}

void DisplayUI::tickMsg(const GestureEvent& ev) {
  if (ev.type == GestureType::Tap && inBtn(bottomBtn(0), ev.x, ev.y)) go(Screen::Dash);
}

// ------------------------------------------------------------------ settings

void DisplayUI::drawSettings() {
  drawHeader("SETTINGS");
  const Settings& s = c_.store->get();
  char l[16];
  snprintf(l, sizeof(l), "DEMO %s", s.demo ? "ON" : "OFF");        drawBtn(gridBtn(0), l, true, cNeon_);
  snprintf(l, sizeof(l), "BLE %s", s.bleScan ? "ON" : "OFF");      drawBtn(gridBtn(1), l, true, cNeon_);
  snprintf(l, sizeof(l), "PHONE %s", s.phone ? "ON" : "OFF");      drawBtn(gridBtn(2), l, true, cNeon_);
  snprintf(l, sizeof(l), "INV %s", s.invert ? "ON" : "OFF");       drawBtn(gridBtn(3), l, true, cNeon_);
  drawBtn(gridBtn(4), "FLIP", true, cNeon_);
  snprintf(l, sizeof(l), "BRT %u%%", (unsigned)s.backlightPct);    drawBtn(gridBtn(5), l, true, cNeon_);
  snprintf(l, sizeof(l), "THEME %u", (unsigned)(s.theme + 1));     drawBtn(gridBtn(6), l, true, cNeon_);
  snprintf(l, sizeof(l), "BOOT %u", (unsigned)(s.bootStyle + 1));  drawBtn(gridBtn(7), l, true, cNeon_);
  drawBtn(gridBtn(8), "HW TEST", true, cNeon_);
  drawBtn(gridBtn(9), "CALIB", true, cNeon_);
  drawBtn(gridBtn(10), "DIAG", true, cNeon_);
  drawBtn(gridBtn(11), "MUSIC", true, cNeon_);
  drawBtn(gridBtn(12), "CTRL", true, cNeon_);
  drawBtn(gridBtn(13), "UPDATE", true, cNeon_);
  drawBtn(gridBtn(14), "SPLASH", true, cNeon_);
  drawBtn(gridBtn(15), "BACK", true, cOk_);
}

void DisplayUI::tickSettings(const GestureEvent& ev) {
  if (ev.type != GestureType::Tap) return;
  int idx = -1;
  for (int i = 0; i < 16; ++i) if (inBtn(gridBtn(i), ev.x, ev.y)) idx = i;
  if (idx < 0) return;
  Settings& s = c_.store->get();
  switch (idx) {
    case 0: s.demo = !s.demo; c_.store->save(); full_ = true; break;
    case 1: s.bleScan = !s.bleScan; c_.ble->setEnabled(s.bleScan); c_.store->save(); full_ = true; break;
    case 2: s.phone = !s.phone; c_.phone->setEnabled(s.phone); c_.store->save(); full_ = true; break;
    case 3: toggleInvert(); break;
    case 4: flipRotation(); break;
    case 5: {
      const uint8_t steps[4] = {25, 50, 75, 100};
      uint8_t next = steps[0];
      for (int i = 0; i < 4; ++i) if (steps[i] > s.backlightPct) { next = steps[i]; break; }
      s.backlightPct = next; setBacklight(next); c_.store->save(); full_ = true;
      break;
    }
    case 6: s.theme = (uint8_t)((s.theme + 1) % kThemeCount); applyTheme(s.theme); c_.store->save(); full_ = true; break;
    case 7: s.bootStyle = (uint8_t)((s.bootStyle + 1) % app::kBootStyles); c_.store->save(); splash(); full_ = true; break;
    case 8: hwPage_ = 0; go(Screen::Hw); break;
    case 9: startCal(Screen::Settings); break;
    case 10: go(Screen::Diag); break;
    case 11: go(Screen::Music); break;
    case 12: go(Screen::Ctrl); break;
    case 13: go(Screen::Update); break;
    case 14: splash(); full_ = true; break;
    default: go(Screen::Dash); break;
  }
}

// ------------------------------------------------------------------ firmware update (phone app)

void DisplayUI::drawUpdate() {
  drawHeader("UPDATE");
  tft_.drawRoundRect(19, 112, 282, 14, 4, cDim_);
  tft_.fillRect(21, 114, 278, 10, cPanel_);
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  updKey_ = -1;
  updBarW_ = -1;
  holdDrawn_ = 0;
  hold_ = HoldTracker();
}

void DisplayUI::tickUpdate(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  const applink::Status st = applink::status(now);
  const bool busy = st.otaState == app::OTA_RECV || st.otaState == app::OTA_VERIFY || st.otaState == app::OTA_DONE;
  const bool phoneOn = c_.phone->enabled();
  const Btn hb = holdBtn();

  const int key = (st.windowOpen ? 1 : 0) + (busy ? 2 : 0) + (phoneOn ? 4 : 0);
  if (key != updKey_) {
    updKey_ = key;
    tft_.fillRect(0, 140, kW, 64, cBg_);
    if (!busy) drawBtn(hb, st.windowOpen ? "CLOSE WINDOW" : "HOLD 1.5s: OPEN WINDOW", phoneOn || st.windowOpen, cNeon_);
    holdDrawn_ = 0;
    hold_ = HoldTracker();
  }

  char b[48];
  const char* l1 = "WINDOW CLOSED";
  uint16_t c1 = cDim_;
  if (st.otaState == app::OTA_RECV) { l1 = "RECEIVING UPDATE"; c1 = cWarn_; }
  else if (st.otaState == app::OTA_VERIFY) { l1 = "VERIFYING"; c1 = cWarn_; }
  else if (st.otaState == app::OTA_DONE) { l1 = "UPDATE OK - REBOOTING"; c1 = cOk_; }
  else if (st.otaState == app::OTA_ERROR) { snprintf(b, sizeof(b), "ERROR: %s", applink::errName(st.otaErr)); l1 = b; c1 = cBad_; }
  else if (st.windowOpen) { l1 = "WINDOW OPEN"; c1 = cOk_; }
  txt(0, 160, 42, 4, l1, c1, TC_DATUM, 310);

  char b2[48];
  const char* l2;
  uint16_t c2 = cDim_;
  if (!phoneOn && !st.windowOpen) { l2 = "Turn PHONE ON in Settings first"; c2 = cWarn_; }
  else if (busy && st.total) { snprintf(b2, sizeof(b2), "%lu / %lu KB", (unsigned long)(st.received / 1024), (unsigned long)(st.total / 1024)); l2 = b2; c2 = cText_; }
  else if (st.windowOpen) { snprintf(b2, sizeof(b2), "Open the app > Update  (%us)", (unsigned)(st.windowLeftMs / 1000)); l2 = b2; c2 = cText_; }
  else { l2 = "Hold the button to allow an update"; }
  txt(1, 160, 80, 2, l2, c2, TC_DATUM, 310);

  const int w = st.total ? (int)((uint64_t)st.received * 278u / st.total) : 0;
  if (w != updBarW_) {
    tft_.fillRect(21, 114, 278, 10, cPanel_);
    if (w > 0) tft_.fillRect(21, 114, w, 10, cNeon_);
    updBarW_ = w;
  }

  if (!busy && !st.windowOpen && phoneOn) {
    if (hold_.update(touching && inBtn(hb, mx, my), now, kHoldMs)) {
      applink::openWindow(120000);  // two minutes
    } else {
      const int pw = (int)(hold_.progress(now, kHoldMs) * (float)(hb.w - 8));
      if (pw != holdDrawn_) {
        if (pw > holdDrawn_) tft_.fillRect(hb.x + 4 + holdDrawn_, hb.y + hb.h - 8, pw - holdDrawn_, 4, cNeon_);
        else tft_.fillRect(hb.x + 4 + pw, hb.y + hb.h - 8, holdDrawn_ - pw, 4, cPanel_);
        holdDrawn_ = pw;
      }
    }
  }
  if (ev.type != GestureType::Tap) return;
  if (inBtn(bottomBtn(0), ev.x, ev.y)) { if (!busy) applink::closeWindow(); go(Screen::Dash); return; }
  if (!busy && st.windowOpen && inBtn(hb, ev.x, ev.y)) applink::closeWindow();
}

// ------------------------------------------------------------------ 20 km/h safety screen

void DisplayUI::drawSafety() {
  drawHeader("20 KM/H LIMIT");
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  safetyKey_ = 255;  // body is (re)drawn by tickSafety
}

void DisplayUI::drawSafetyBody(LimitStatus st, bool armed) {
  tft_.fillRect(0, 36, kW, 166, cBg_);
  const char* stat = "";
  const char* l1 = ""; const char* l2 = ""; const char* l3 = "";
  uint16_t col = cDim_;
  switch (st) {
    case LimitStatus::Confirmed:
      col = cOk_; stat = "CONFIRMED 20 KM/H"; l1 = "Controller acknowledged."; break;
    case LimitStatus::DemoSimulated:
      if (armed) { col = cWarn_; stat = "DEMO: 20 SIMULATED"; l1 = "DEMO ONLY - nothing was sent."; l2 = "Scooter limit: NOT CHANGED"; l3 = "Tap below to clear."; }
      else { col = cDim_; stat = "DEMO: NOT ARMED"; l1 = "DEMO ONLY - nothing is sent."; l2 = "Scooter limit: UNKNOWN"; l3 = "Hold button to simulate 20."; }
      break;
    case LimitStatus::NotSupported:
      col = cBad_; stat = "NOT CONFIRMED"; l1 = "Controller link not supported."; l2 = "No command is ever sent."; l3 = "Scooter limit: UNKNOWN"; break;
    default:
      col = cWarn_; stat = "UNKNOWN"; l1 = "No link / no confirmation."; l2 = "Limit state: UNKNOWN"; break;
  }
  tft_.setTextPadding(0);
  tft_.setTextDatum(TC_DATUM);
  tft_.setTextColor(col, cBg_);
  tft_.drawString(stat, 160, 42, 4);
  tft_.setTextColor(cText_, cBg_);
  tft_.drawString(l1, 160, 80, 2);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString(l2, 160, 100, 2);
  tft_.drawString(l3, 160, 120, 2);
  const bool en = (st == LimitStatus::DemoSimulated);
  const char* lbl = armed ? "CLEAR DEMO LIMIT" : (en ? "HOLD 1.5s TO ARM (DEMO)" : "UNAVAILABLE");
  drawBtn(holdBtn(), lbl, en, armed ? cWarn_ : cNeon_);
  holdDrawn_ = 0;
  hold_ = HoldTracker();
}

void DisplayUI::tickSafety(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  const bool demo = c_.store->get().demo;
  const bool real = !demo && c_.ble->state() == BleState::Connected;
  if (real != safetyWasReal_) {  // switched between the real and the local-demo view
    safetyWasReal_ = real;
    safetyKey_ = 255;
    safetyRealKey_ = 0;
    tft_.fillRect(0, 36, kW, 170, cBg_);
    hold_ = HoldTracker();
    holdLock_ = HoldTracker();
  }
  if (real) { tickSafetyReal(touching, mx, my, ev, now); return; }
  const LimitStatus st = c_.safety->status(demo);
  const bool armed = c_.safety->demoArmed();
  const uint8_t key = (uint8_t)((uint8_t)st * 2 + (armed ? 1 : 0));
  if (key != safetyKey_) { safetyKey_ = key; drawSafetyBody(st, armed); }

  const Btn hb = holdBtn();
  const bool canHold = (st == LimitStatus::DemoSimulated) && !armed;
  if (canHold) {
    const bool inside = touching && inBtn(hb, mx, my);
    if (hold_.update(inside, now, kHoldMs)) {
      c_.safety->requestQuickLimit(demo);  // local demo only, nothing is transmitted
      safetyKey_ = 255;
    } else {
      const int w = (int)(hold_.progress(now, kHoldMs) * (float)(hb.w - 8));
      if (w != holdDrawn_) {
        if (w > holdDrawn_) tft_.fillRect(hb.x + 4 + holdDrawn_, hb.y + hb.h - 8, w - holdDrawn_, 4, cNeon_);
        else tft_.fillRect(hb.x + 4 + w, hb.y + hb.h - 8, holdDrawn_ - w, 4, cPanel_);
        holdDrawn_ = w;
      }
    }
  }
  if (ev.type == GestureType::Tap) {
    if (inBtn(bottomBtn(0), ev.x, ev.y)) { go(Screen::Dash); return; }
    if (armed && inBtn(hb, ev.x, ev.y)) { c_.safety->clearDemo(); safetyKey_ = 255; }
  }
}

// ------------------------------------------------------------------ 20 km/h cap + lock on the real scooter

uint32_t DisplayUI::safetyRealKey(uint32_t now) {
  xbot::Readback rb;
  const bool have = c_.ble->getReadback(rb, now);
  const WriteStatus ws = c_.ble->writeStatus();
  const bool showWs = ws.state != WriteState::Idle && (ws.state == WriteState::Pending || now - ws.doneMs < 8000);
  const LimitSave sv = c_.ble->limitSave();
  uint32_t k = 2166136261u;
  const uint32_t parts[11] = {have ? 1u : 0u, rb.lockValid ? 1u : 0u, rb.locked ? 1u : 0u,
                              (uint32_t)(rb.limValid[0] ? rb.limKmh[0] : 99), (uint32_t)(rb.limValid[1] ? rb.limKmh[1] : 99),
                              (uint32_t)(rb.limValid[2] ? rb.limKmh[2] : 99), sv.valid ? 1u : 0u,
                              showWs ? (uint32_t)ws.state : 0u, (uint32_t)strlen(ws.note), 0u, 1u};
  for (int i = 0; i < 11; ++i) k = (k ^ parts[i]) * 16777619u;
  return k | 1u;
}

void DisplayUI::drawSafetyReal(uint32_t now) {
  safetyRealKey_ = safetyRealKey(now);
  holdDrawn_ = 0;
  lockDrawn_ = 0;
  hold_ = HoldTracker();
  holdLock_ = HoldTracker();
  xbot::Readback rb;
  const bool have = c_.ble->getReadback(rb, now);
  const int cap = have ? xbot::limitsCapped(rb, 20) : -1;
  const LimitSave sv = c_.ble->limitSave();
  const WriteStatus ws = c_.ble->writeStatus();

  tft_.fillRect(0, 36, kW, 170, cBg_);
  tft_.setTextPadding(0);
  tft_.setTextDatum(TC_DATUM);
  const char* stat = "UNKNOWN";
  uint16_t col = cWarn_;
  if (cap == 1) { stat = "20 KM/H ACTIVE"; col = cOk_; }
  else if (cap == 0) { stat = "NOT LIMITED"; col = cWarn_; }
  tft_.setTextColor(col, cBg_);
  tft_.drawString(stat, 160, 42, 4);

  char b[44];
  if (rb.limValid[0] && rb.limValid[1] && rb.limValid[2])
    snprintf(b, sizeof(b), "Sport %d Drive %d Eco %d km/h", rb.limKmh[0], rb.limKmh[1], rb.limKmh[2]);
  else if (rb.wfValid && rb.limHas[0] && rb.limHas[1] && rb.limHas[2])
    snprintf(b, sizeof(b), "wf %u raw %u/%u/%u", (unsigned)rb.wheelFactor, (unsigned)rb.limRaw[0], (unsigned)rb.limRaw[1], (unsigned)rb.limRaw[2]);
  else snprintf(b, sizeof(b), "Limits: not read yet");
  tft_.setTextColor(cText_, cBg_);
  tft_.drawString(b, 160, 80, 2);
  tft_.setTextColor(cDim_, cBg_);
  tft_.drawString(rb.lockValid ? (rb.locked ? "Scooter: LOCKED" : "Scooter: UNLOCKED") : "Scooter lock: --", 160, 100, 2);

  const bool show = ws.state != WriteState::Idle && (ws.state == WriteState::Pending || now - ws.doneMs < 8000);
  const char* l3 = "Hold a button to send";
  uint16_t c3 = cDim_;
  char wb[40];
  if (show && ws.state == WriteState::Pending) { l3 = "SENDING..."; c3 = cWarn_; }
  else if (show && ws.state == WriteState::Confirmed) { l3 = "CONFIRMED by scooter"; c3 = cOk_; }
  else if (show && ws.state == WriteState::NotConfirmed) { l3 = "NOT CONFIRMED"; c3 = cBad_; }
  else if (show && ws.state == WriteState::Refused) { snprintf(wb, sizeof(wb), "REFUSED: %s", ws.note); l3 = wb; c3 = cBad_; }
  tft_.setTextColor(c3, cBg_);
  tft_.drawString(l3, 160, 120, 2);

  const bool canLimit = (cap == 1) ? sv.valid : have;
  const char* lbl = (cap == 1) ? (sv.valid ? "HOLD 1.5s: RESTORE LIMITS" : "NO SAVED LIMITS") : "HOLD 1.5s: LIMIT TO 20";
  drawBtn(holdBtn(), lbl, canLimit, cNeon_);
  drawBtn(bottomBtn(2), rb.lockValid && rb.locked ? "UNLOCK" : "LOCK", rb.lockValid, cNeon_);
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
}

void DisplayUI::tickSafetyReal(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  if (safetyRealKey(now) != safetyRealKey_) drawSafetyReal(now);

  xbot::Readback rb;
  const bool have = c_.ble->getReadback(rb, now);
  const int cap = have ? xbot::limitsCapped(rb, 20) : -1;
  const LimitSave sv = c_.ble->limitSave();
  const Btn hb = holdBtn();
  const Btn lb = bottomBtn(2);

  const bool canLimit = (cap == 1) ? sv.valid : have;
  if (canLimit) {
    if (hold_.update(touching && inBtn(hb, mx, my), now, kHoldMs)) {
      c_.ble->requestWriteOnce(WriteKind::LimitCap, cap == 1 ? 0 : 1);  // 1 = cap at 20, 0 = restore
    } else {
      const int w = (int)(hold_.progress(now, kHoldMs) * (float)(hb.w - 8));
      if (w != holdDrawn_) {
        if (w > holdDrawn_) tft_.fillRect(hb.x + 4 + holdDrawn_, hb.y + hb.h - 8, w - holdDrawn_, 4, cNeon_);
        else tft_.fillRect(hb.x + 4 + w, hb.y + hb.h - 8, holdDrawn_ - w, 4, cPanel_);
        holdDrawn_ = w;
      }
    }
  }
  if (rb.lockValid) {
    if (holdLock_.update(touching && inBtn(lb, mx, my), now, kHoldMs)) {
      c_.ble->requestWriteOnce(WriteKind::Lock, rb.locked ? 0 : 1);  // 1 = lock, 0 = unlock
    } else {
      const int w = (int)(holdLock_.progress(now, kHoldMs) * (float)(lb.w - 8));
      if (w != lockDrawn_) {
        if (w > lockDrawn_) tft_.fillRect(lb.x + 4 + lockDrawn_, lb.y + lb.h - 6, w - lockDrawn_, 3, cNeon_);
        else tft_.fillRect(lb.x + 4 + w, lb.y + lb.h - 6, lockDrawn_ - w, 3, cPanel_);
        lockDrawn_ = w;
      }
    }
  }
  if (ev.type == GestureType::Tap && inBtn(bottomBtn(0), ev.x, ev.y)) go(Screen::Dash);
}

// ------------------------------------------------------------------ diagnostics

void DisplayUI::drawDiag() {
  drawHeader("DIAGNOSTICS");
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  lastDiagMs_ = 0;
}

void DisplayUI::tickDiag(const GestureEvent& ev, uint32_t now) {
  if (ev.type == GestureType::Tap) {
    if (inBtn(bottomBtn(0), ev.x, ev.y)) { go(Screen::Dash); return; }
    if (ev.y >= 148 && ev.y < 202) {
      Settings& st = c_.store->get();
      BleTarget cur = c_.ble->target();
      BleTarget nt;  // empty = scan mode
      bool change = false;
      if (cur.valid()) {
        if (ev.y < 166) change = true;  // tap the target line to forget it
      } else {
        BleSeen d;
        if (c_.ble->get((uint8_t)((ev.y - 148) / 18), d)) {
          strlcpy(nt.addr, d.addr, sizeof(nt.addr));
          strlcpy(nt.name, d.name, sizeof(nt.name));
          nt.type = d.addrType;
          change = true;
        }
      }
      if (change) {
        c_.ble->setTarget(nt);
        strlcpy(st.targetAddr, nt.addr, sizeof(st.targetAddr));
        strlcpy(st.targetName, nt.name, sizeof(st.targetName));
        st.targetType = nt.type;
        c_.store->save();
        lastDiagMs_ = 0;
      }
    }
  }
  if (lastDiagMs_ != 0 && now - lastDiagMs_ < 500) return;
  lastDiagMs_ = now ? now : 1;
  char b[48];
  snprintf(b, sizeof(b), "NOVA RIDE OS v%s %s", NOVA_FW_VERSION, NOVA_PROFILE_NAME);
  txt(0, 8, 40, 2, b, cText_, TL_DATUM, 304);
  snprintf(b, sizeof(b), "Heap %u/%u  up %lus", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
           (unsigned long)(now / 1000UL));
  txt(1, 8, 58, 2, b, cDim_, TL_DATUM, 304);
  const BleState bs = c_.ble->state();
  const char* bn = bs == BleState::Off ? "off" : bs == BleState::Connected ? "linked" : bs == BleState::Connecting ? "connecting" : "scan";
  snprintf(b, sizeof(b), "BLE %s  seen %u  reset %d", bn, (unsigned)c_.ble->count(), (int)esp_reset_reason());
  txt(2, 8, 76, 2, b, cDim_, TL_DATUM, 304);

  const BleTarget tg = c_.ble->target();
  const BleLinkInfo li = c_.ble->linkInfo();
  if (tg.valid()) snprintf(b, sizeof(b), "Target %.16s", tg.name[0] ? tg.name : tg.addr);
  else snprintf(b, sizeof(b), "No target - tap a device below");
  txt(3, 8, 94, 2, b, tg.valid() ? cText_ : cWarn_, TL_DATUM, 304);
  if (bs == BleState::Connected)
    snprintf(b, sizeof(b), "Link %s  key %02X  var %u", li.profile, (unsigned)li.key, (unsigned)li.variant);
  else if (li.refused) snprintf(b, sizeof(b), "REFUSED: variant %u not decoded", (unsigned)li.variant);
  else if (bs == BleState::Connecting) snprintf(b, sizeof(b), "%s", li.step[0] ? li.step : "Connecting...");
  else snprintf(b, sizeof(b), "Not connected (read-only build)");
  txt(4, 8, 112, 2, b, bs == BleState::Connected ? cOk_ : cDim_, TL_DATUM, 304);
  if (li.triage[0]) snprintf(b, sizeof(b), "%s", li.triage);
  else snprintf(b, sizeof(b), "ntf %lu frm %lu ver %s", (unsigned long)li.notifies, (unsigned long)li.frames,
                li.version[0] ? li.version : "--");
  txt(5, 8, 130, 2, b, li.triage[0] ? cWarn_ : cDim_, TL_DATUM, 304);

  if (tg.valid()) {
    txt(6, 8, 148, 2, "Tap here to forget target", cDim_, TL_DATUM, 304);
    txt(7, 8, 166, 2, " ", cDim_, TL_DATUM, 304);
    txt(8, 8, 184, 2, " ", cDim_, TL_DATUM, 304);
  } else {
    for (uint8_t i = 0; i < 3; ++i) {
      BleSeen d;
      if (c_.ble->get(i, d)) {
        snprintf(b, sizeof(b), "%c%-14.14s %4d", d.candidate ? '*' : ' ', d.name[0] ? d.name : d.addr, (int)d.rssi);
        txt((uint8_t)(6 + i), 8, (int16_t)(148 + i * 18), 2, b, d.candidate ? cWarn_ : cText_, TL_DATUM, 304);
      } else {
        txt((uint8_t)(6 + i), 8, (int16_t)(148 + i * 18), 2, i == 0 ? " no devices seen (BLE on?)" : " ", cDim_, TL_DATUM, 304);
      }
    }
  }
}

// ------------------------------------------------------------------ ride controls

namespace {
DisplayUI::Btn limMinus(int j) { return mk(196, (int16_t)(38 + j * 30), 52, 26); }
DisplayUI::Btn limPlus(int j) { return mk(256, (int16_t)(38 + j * 30), 52, 26); }
DisplayUI::Btn togBtn(int i) { return mk((int16_t)(8 + i * 104), 132, 96, 40); }
const char* const kLimName[3] = {"ECO", "DRIVE", "SPORT"};  // row j shows mode index 2 - j (eco, drive, sport)
const uint32_t kStatusShowMs = 8000;
}  // namespace

// Everything that changes what CTRL shows, folded into one number (redraw only on change).
uint32_t DisplayUI::ctrlKey(uint32_t now) {
  const bool demo = c_.store->get().demo;
  const bool linked = !demo && c_.ble->state() == BleState::Connected;
  xbot::Readback rb;
  const bool have = linked && c_.ble->getReadback(rb, now);
  const WriteStatus ws = c_.ble->writeStatus();
  const bool showWs = ws.state != WriteState::Idle && (ws.state == WriteState::Pending || now - ws.doneMs < kStatusShowMs);
  uint32_t k = 2166136261u;
  const uint32_t parts[16] = {
      demo ? 1u : 0u, linked ? 1u : 0u, (linked && c_.ble->writesUnlocked()) ? 1u : 0u, have ? 1u : 0u,
      (have && rb.lightValid) ? (rb.light ? 2u : 1u) : 0u, (have && rb.zeroValid) ? (rb.zero ? 2u : 1u) : 0u,
      (uint32_t)(have && rb.limValid[0] ? rb.limKmh[0] : 99), (uint32_t)(have && rb.limValid[1] ? rb.limKmh[1] : 99),
      (uint32_t)(have && rb.limValid[2] ? rb.limKmh[2] : 99), showWs ? (uint32_t)ws.state : 0u,
      (uint32_t)demoLim_[0], (uint32_t)demoLim_[1], (uint32_t)demoLim_[2],
      (c_.ctrl->light() ? 1u : 0u) | (c_.ctrl->zeroStart() ? 2u : 0u) | (c_.ctrl->hazard() ? 4u : 0u), (uint32_t)strlen(ws.note), 1u};
  for (int i = 0; i < 16; ++i) k = (k ^ parts[i]) * 16777619u;
  return k | 1u;
}

void DisplayUI::drawCtrl() {
  const uint32_t now = millis();
  ctrlKey_ = ctrlKey(now);
  holdDrawn_ = 0;
  drawHeader("RIDE CONTROLS");
  const bool demo = c_.store->get().demo;
  const bool linked = !demo && c_.ble->state() == BleState::Connected;
  const bool unlocked = linked && c_.ble->writesUnlocked();
  xbot::Readback rb;
  const bool have = linked && c_.ble->getReadback(rb, now);
  const bool active = demo || unlocked;
  const ScooterControl& c = *c_.ctrl;

  // speed limit per mode, like the sliders in the X-BOT app (1 km/h steps)
  char l[24];
  for (int j = 0; j < 3; ++j) {
    const int which = 2 - j;
    const int y = 38 + j * 30;
    tft_.setTextPadding(0);
    tft_.setTextDatum(TL_DATUM);
    tft_.setTextColor(cText_, cBg_);
    tft_.drawString(kLimName[j], 8, y + 6, 2);
    bool known = false;
    int v = 0;
    if (demo) { known = true; v = demoLim_[which]; }
    else if (have && rb.limValid[which]) { known = true; v = rb.limKmh[which]; }
    if (known) snprintf(l, sizeof(l), "%d km/h", v); else snprintf(l, sizeof(l), "-- km/h");
    tft_.setTextColor(known ? cOk_ : cDim_, cBg_);
    tft_.drawString(l, 72, y + 2, 4);
    drawBtn(limMinus(j), "-", active && known, cNeon_);
    drawBtn(limPlus(j), "+", active && known, cNeon_);
  }

  bool on0 = false, on1 = false, on2 = false, k0 = false, k1 = false;
  if (demo) { on0 = c.light(); on1 = c.zeroStart(); on2 = c.hazard(); k0 = k1 = true; }
  else if (have) { on0 = rb.light; k0 = rb.lightValid; on1 = rb.zero; k1 = rb.zeroValid; }
  snprintf(l, sizeof(l), "LIGHT %s", k0 ? (on0 ? "ON" : "OFF") : "--");
  drawBtn(togBtn(0), l, active && k0, on0 ? cOk_ : cNeon_);
  snprintf(l, sizeof(l), "ZERO %s", k1 ? (on1 ? "ON" : "OFF") : "--");
  drawBtn(togBtn(1), l, active && k1, on1 ? cOk_ : cNeon_);
  if (demo) {
    snprintf(l, sizeof(l), "HAZARD %s", on2 ? "ON" : "OFF");
    drawBtn(togBtn(2), l, true, on2 ? cWarn_ : cNeon_);
  } else {
    drawBtn(togBtn(2), "HAZARD N/A", false, cNeon_);  // no register known for it
  }

  tft_.setTextPadding(0);
  tft_.setTextDatum(TC_DATUM);
  const char* s1 = "";
  uint16_t c1 = cDim_;
  char buf[40];
  if (demo) { s1 = "DEMO: simulated, nothing is sent"; c1 = cWarn_; }
  else if (!linked) { s1 = "NOT SUPPORTED: no scooter link"; c1 = cBad_; }
  else {
    const WriteStatus ws = c_.ble->writeStatus();
    const bool show = ws.state != WriteState::Idle && (ws.state == WriteState::Pending || now - ws.doneMs < kStatusShowMs);
    if (show && ws.state == WriteState::Pending) { s1 = "SENDING..."; c1 = cWarn_; }
    else if (show && ws.state == WriteState::Confirmed) { s1 = "CONFIRMED by scooter"; c1 = cOk_; }
    else if (show && ws.state == WriteState::NotConfirmed) { s1 = "NOT CONFIRMED"; c1 = cBad_; }
    else if (show && ws.state == WriteState::Refused) { snprintf(buf, sizeof(buf), "REFUSED: %s", ws.note); s1 = buf; c1 = cBad_; }
    else if (unlocked) { s1 = "UNLOCKED: tap to send"; c1 = cWarn_; }
    else { s1 = "LOCKED: hold UNLOCK 1.5 s"; c1 = cDim_; }
  }
  tft_.setTextColor(c1, cBg_);
  tft_.drawString(s1, 160, 182, 2);
  drawBtn(bottomBtn(0), "BACK", true, cNeon_);
  if (linked) drawBtn(bottomBtn(2), unlocked ? "LOCK" : "UNLOCK", true, unlocked ? cWarn_ : cNeon_);
}

void DisplayUI::tickCtrl(bool touching, int16_t mx, int16_t my, const GestureEvent& ev, uint32_t now) {
  const bool demo = c_.store->get().demo;
  const bool linked = !demo && c_.ble->state() == BleState::Connected;
  const bool unlocked = linked && c_.ble->writesUnlocked();

  if (ctrlKey(now) != ctrlKey_) drawCtrl();  // state changed: redraw

  // Unlock needs a deliberate 1.5 s hold.
  if (linked && !unlocked) {
    const Btn ub = bottomBtn(2);
    const bool inside = touching && inBtn(ub, mx, my);
    if (hold_.update(inside, now, kHoldMs)) {
      c_.ble->setWritesUnlocked(true);
      drawCtrl();
      return;
    }
    const int w = (int)(hold_.progress(now, kHoldMs) * (float)(ub.w - 8));
    if (w != holdDrawn_) {
      if (w > holdDrawn_) tft_.fillRect(ub.x + 4 + holdDrawn_, ub.y + ub.h - 6, w - holdDrawn_, 3, cNeon_);
      else tft_.fillRect(ub.x + 4 + w, ub.y + ub.h - 6, holdDrawn_ - w, 3, cPanel_);
      holdDrawn_ = w;
    }
  }

  if (ev.type != GestureType::Tap) return;
  if (inBtn(bottomBtn(0), ev.x, ev.y)) { go(Screen::Dash); return; }
  if (linked && unlocked && inBtn(bottomBtn(2), ev.x, ev.y)) { c_.ble->setWritesUnlocked(false); drawCtrl(); return; }
  if (!demo && !unlocked) return;

  xbot::Readback rb;
  const bool have = !demo && c_.ble->getReadback(rb, now);
  for (int j = 0; j < 3; ++j) {
    const int which = 2 - j;
    int delta = 0;
    if (inBtn(limMinus(j), ev.x, ev.y)) delta = -1;
    else if (inBtn(limPlus(j), ev.x, ev.y)) delta = 1;
    if (delta == 0) continue;
    const int mx2 = xbot::limitMaxKmh(xbot::limitRegForIndex(which));
    if (demo) {
      int v = demoLim_[which] + delta;
      if (v < 6) v = 6;
      if (v > mx2) v = mx2;
      demoLim_[which] = v;
      drawCtrl();
    } else if (have && rb.limValid[which]) {
      const int v = rb.limKmh[which] + delta;
      if (v >= 6 && v <= mx2) c_.ble->requestWrite(WriteKind::ModeLimit, xbot::encodeLimitReq(which, v));
    }
  }
  if (demo) {
    ScooterControl& c = *c_.ctrl;
    bool changed = false;
    if (inBtn(togBtn(0), ev.x, ev.y)) { c.setLight(!c.light(), true); changed = true; }
    if (inBtn(togBtn(1), ev.x, ev.y)) { c.setZeroStart(!c.zeroStart(), true); changed = true; }
    if (inBtn(togBtn(2), ev.x, ev.y)) { c.setHazard(!c.hazard(), true); changed = true; }
    if (changed) drawCtrl();
    return;
  }
  if (have) {
    if (inBtn(togBtn(0), ev.x, ev.y) && rb.lightValid) c_.ble->requestWrite(WriteKind::Light, rb.light ? 0 : 1);
    if (inBtn(togBtn(1), ev.x, ev.y) && rb.zeroValid) c_.ble->requestWrite(WriteKind::ZeroStart, rb.zero ? 0 : 1);
  }
}

