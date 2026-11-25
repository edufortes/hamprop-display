#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <time.h>

#include "config.h"
#include "hamster.h"
#include "lgfx_config.hpp"

namespace {

Display tft;

struct Band {
  const char *label;
  float mhz;
};
constexpr Band kBands[] = {{"80", 3.6f},  {"40", 7.1f},  {"30", 10.12f}, {"20", 14.2f},
                           {"17", 18.1f}, {"15", 21.2f}, {"12", 24.9f},  {"10", 28.5f}};
constexpr int kBandCount = sizeof(kBands) / sizeof(kBands[0]);

enum class State : uint8_t { Closed, Marginal, Open };

struct Sonde {
  char code[8];
  float km, muf, fof2;
  int ageMin;
};
constexpr int kMaxSondes = 6;

struct Report {
  bool valid = false;
  float muf = 0;
  float fof2 = 0;
  float luf = 0;
  float sfi = 0;
  int kIndex = 0;
  int sondeCount = 0;
  float aIndex = 0, sunspots = 0, solarWind = 0;
  char xray[8] = "";
  Sonde sondes[kMaxSondes] = {};
  int sondeListCount = 0;
  time_t sondeTime = 0;
  State bands[kBandCount] = {};
};

Report report;
float gLat = 52.5f, gLon = 13.0f;
char gLocator[8] = "JO62";
Preferences prefs;

// Maidenhead locator (4 or 6 chars) to the center of the square.
bool locatorToLatLon(const char *loc, float &lat, float &lon) {
  size_t n = strlen(loc);
  if (n != 4 && n != 6) return false;
  char c[7];
  for (size_t i = 0; i < n; i++) c[i] = (i < 2 || i > 3) ? toupper(loc[i]) : loc[i];
  if (c[0] < 'A' || c[0] > 'R' || c[1] < 'A' || c[1] > 'R') return false;
  if (!isdigit(c[2]) || !isdigit(c[3])) return false;
  lon = (c[0] - 'A') * 20.0f - 180.0f + (c[2] - '0') * 2.0f;
  lat = (c[1] - 'A') * 10.0f - 90.0f + (c[3] - '0') * 1.0f;
  if (n == 6) {
    if (c[4] < 'A' || c[4] > 'X' || c[5] < 'A' || c[5] > 'X') return false;
    lon += (c[4] - 'A') * (5.0f / 60.0f) + 2.5f / 60.0f;
    lat += (c[5] - 'A') * (2.5f / 60.0f) + 1.25f / 60.0f;
  } else {
    lon += 1.0f;
    lat += 0.5f;
  }
  return true;
}

void applyLocator(const char *loc) {
  float la, lo;
  if (!locatorToLatLon(loc, la, lo)) return;
  strlcpy(gLocator, loc, sizeof gLocator);
  for (char *p = gLocator; *p; p++)
    if (p - gLocator < 2 || p - gLocator > 3) *p = toupper(*p);
  gLat = la;
  gLon = lo;
}
uint32_t nextFetch = 0;
int page = 0;
uint32_t lastPageChange = 0;
constexpr int kPageCount = 5;
uint32_t hamsterFrame = 0;
uint32_t lastHamsterMs = 0;
lgfx::LGFX_Sprite hamsterSprite(&tft);
constexpr uint32_t kPageTimeoutMs = 60UL * 1000UL;
bool fetchFailed = false;

constexpr uint16_t kColOpen = 0x07E0;
constexpr uint16_t kColMarginal = 0xFD20;
constexpr uint16_t kColClosed = 0x4208;

float haversineKm(float lat1, float lon1, float lat2, float lon2) {
  const float r = 6371.0f, d2r = PI / 180.0f;
  float dLat = (lat2 - lat1) * d2r, dLon = (lon2 - lon1) * d2r;
  float a = sinf(dLat / 2) * sinf(dLat / 2) +
            cosf(lat1 * d2r) * cosf(lat2 * d2r) * sinf(dLon / 2) * sinf(dLon / 2);
  return 2 * r * asinf(sqrtf(a));
}

float sunElevationDeg(time_t t, float lat, float lon) {
  struct tm u;
  gmtime_r(&t, &u);
  float dayOfYear = u.tm_yday + 1;
  float hours = u.tm_hour + u.tm_min / 60.0f;
  float gamma = 2 * PI / 365.0f * (dayOfYear - 1 + (hours - 12) / 24.0f);
  float decl = 0.006918f - 0.399912f * cosf(gamma) + 0.070257f * sinf(gamma) -
               0.006758f * cosf(2 * gamma) + 0.000907f * sinf(2 * gamma);
  float eqTime = 229.18f * (0.000075f + 0.001868f * cosf(gamma) - 0.032077f * sinf(gamma) -
                            0.014615f * cosf(2 * gamma) - 0.040849f * sinf(2 * gamma));
  float solarMin = hours * 60 + eqTime + 4 * lon;
  float hourAngle = (solarMin / 4 - 180) * PI / 180.0f;
  float latR = lat * PI / 180.0f;
  float sinEl = sinf(latR) * sinf(decl) + cosf(latR) * cosf(decl) * cosf(hourAngle);
  return asinf(constrain(sinEl, -1.0f, 1.0f)) * 180.0f / PI;
}

// Rough D-layer absorption estimate, not a measurement.
float estimateLuf(float sfi, int k, float elevDeg) {
  float s = elevDeg > 0 ? powf(sinf(elevDeg * PI / 180.0f), 0.75f) : 0.0f;
  float luf = 1.8f + (4.5f + 0.03f * (sfi - 70.0f)) * s;
  if (k >= 5) luf += 1.5f * (k - 4);
  return luf;
}

time_t parseIso(const char *s) {
  struct tm t = {};
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour,
                   &t.tm_min, &t.tm_sec) != 6)
    return 0;
  t.tm_year -= 1900;
  t.tm_mon -= 1;
  return mktime(&t);  // TZ is UTC
}

bool fetchIono(Report &r) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(20000);
  http.setUserAgent("esp32-hamprop/1.0");
  if (!http.begin(client, kIonoUrl)) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("iono HTTP %d\n", code);
    http.end();
    return false;
  }

  JsonDocument filter;
  JsonObject f = filter.add<JsonObject>();
  f["fof2"] = true;
  f["mufd"] = true;
  f["cs"] = true;
  f["time"] = true;
  f["station"]["code"] = true;
  f["station"]["latitude"] = true;
  f["station"]["longitude"] = true;

  JsonDocument doc;
  String body = http.getString();
  http.end();
  DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));
  body = String();
  if (err) {
    Serial.printf("iono JSON: %s\n", err.c_str());
    return false;
  }

  time_t now = time(nullptr);
  r.sondeListCount = 0;
  float wSum = 0, mufSum = 0, fSum = 0;
  int n = 0;
  time_t newest = 0;
  for (JsonObject s : doc.as<JsonArray>()) {
    if (s["mufd"].isNull() || s["fof2"].isNull()) continue;
    if ((s["cs"] | 0.0f) <= 0) continue;
    time_t ts = parseIso(s["time"] | "");
    if (!ts || now - ts > (time_t)kMaxSondeAgeS) continue;
    float d = haversineKm(gLat, gLon, s["station"]["latitude"].as<float>(),
                          s["station"]["longitude"].as<float>());
    if (d > kMaxSondeKm) continue;
    float w = 1.0f / ((d + 100.0f) * (d + 100.0f));
    wSum += w;
    mufSum += w * s["mufd"].as<float>();
    fSum += w * s["fof2"].as<float>();
    n++;
    if (ts > newest) newest = ts;
    Sonde cur;
    strlcpy(cur.code, s["station"]["code"] | "?", sizeof cur.code);
    cur.km = d;
    cur.muf = s["mufd"].as<float>();
    cur.fof2 = s["fof2"].as<float>();
    cur.ageMin = (now - ts) / 60;
    int slot = -1;
    if (r.sondeListCount < kMaxSondes) {
      slot = r.sondeListCount++;
    } else {
      int far = 0;
      for (int j = 1; j < kMaxSondes; j++)
        if (r.sondes[j].km > r.sondes[far].km) far = j;
      if (d < r.sondes[far].km) slot = far;
    }
    if (slot >= 0) r.sondes[slot] = cur;
    Serial.printf("sonde %s %.0f km muf %.1f fof2 %.2f\n", s["station"]["code"] | "?", d,
                  s["mufd"].as<float>(), s["fof2"].as<float>());
  }
  if (!n) return false;
  for (int i = 1; i < r.sondeListCount; i++)
    for (int j = i; j > 0 && r.sondes[j].km < r.sondes[j - 1].km; j--)
      std::swap(r.sondes[j], r.sondes[j - 1]);
  r.muf = mufSum / wSum;
  r.fof2 = fSum / wSum;
  r.sondeCount = n;
  r.sondeTime = newest;
  return true;
}

bool xmlValue(const String &xml, const char *tag, float &out) {
  String open = String("<") + tag + ">";
  int a = xml.indexOf(open);
  if (a < 0) return false;
  a += open.length();
  int b = xml.indexOf('<', a);
  if (b < 0) return false;
  out = xml.substring(a, b).toFloat();
  return true;
}

bool fetchSolar(Report &r) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setUserAgent("esp32-hamprop/1.0");
  if (!http.begin(client, kSolarUrl)) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("solar HTTP %d\n", code);
    http.end();
    return false;
  }
  String xml = http.getString();
  http.end();
  float sfi = 0, k = 0;
  if (!xmlValue(xml, "solarflux", sfi) || !xmlValue(xml, "kindex", k)) return false;
  r.sfi = sfi;
  r.kIndex = (int)k;
  float v;
  if (xmlValue(xml, "aindex", v)) r.aIndex = v;
  if (xmlValue(xml, "sunspots", v)) r.sunspots = v;
  if (xmlValue(xml, "solarwind", v)) r.solarWind = v;
  int xa = xml.indexOf("<xray>");
  if (xa >= 0) {
    xa += 6;
    int xb = xml.indexOf('<', xa);
    if (xb > xa) strlcpy(r.xray, xml.substring(xa, xb).c_str(), sizeof r.xray);
  }
  return true;
}

void classify(Report &r) {
  float elev = sunElevationDeg(time(nullptr), gLat, gLon);
  r.luf = estimateLuf(r.sfi, r.kIndex, elev);
  for (int i = 0; i < kBandCount; i++) {
    float f = kBands[i].mhz;
    State s;
    if (f > r.muf || f < r.luf)
      s = State::Closed;
    else if (f > 0.85f * r.muf || f < 1.3f * r.luf)
      s = State::Marginal;
    else
      s = State::Open;
    r.bands[i] = s;
  }
  r.valid = true;
}

void drawMessage(const char *l1, const char *l2 = nullptr) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(TFT_WHITE);
  tft.setFont(&fonts::Font4);
  tft.drawString(l1, 120, l2 ? 105 : 120);
  if (l2) {
    tft.setFont(&fonts::Font2);
    tft.drawString(l2, 120, 140);
  }
}

void drawReport(const Report &r) {
  tft.fillScreen(TFT_BLACK);
  const int cx = 120, cy = 120, rOut = 118, rIn = 82;
  for (int i = 0; i < kBandCount; i++) {
    float a0 = -90.0f + i * 45.0f + 1.5f, a1 = -90.0f + (i + 1) * 45.0f - 1.5f;
    uint16_t col = r.bands[i] == State::Open       ? kColOpen
                   : r.bands[i] == State::Marginal ? kColMarginal
                                                   : kColClosed;
    tft.fillArc(cx, cy, rOut, rIn, a0, a1, col);
    float mid = (a0 + a1) / 2 * PI / 180.0f;
    float rl = (rOut + rIn) / 2.0f;
    tft.setTextDatum(lgfx::middle_center);
    tft.setFont(&fonts::Font4);
    tft.setTextColor(r.bands[i] == State::Closed ? 0x8410 : TFT_BLACK);
    tft.drawString(kBands[i].label, cx + rl * cosf(mid), cy + rl * sinf(mid));
  }

  char buf[32];
  tft.setTextDatum(lgfx::middle_center);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(0x8410);
  snprintf(buf, sizeof buf, "%s MUF", gLocator);
  tft.drawString(buf, cx, 62);
  tft.setFont(&fonts::Font6);
  tft.setTextColor(TFT_CYAN);
  snprintf(buf, sizeof buf, "%.1f", r.muf);
  tft.drawString(buf, cx, 92);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(0x8410);
  tft.drawString("LUF~", cx - 34, 124);
  tft.drawString("foF2", cx + 34, 124);
  tft.setFont(&fonts::Font4);
  tft.setTextColor(TFT_ORANGE);
  snprintf(buf, sizeof buf, "%.1f", r.luf);
  tft.drawString(buf, cx - 34, 146);
  tft.setTextColor(TFT_GREEN);
  snprintf(buf, sizeof buf, "%.1f", r.fof2);
  tft.drawString(buf, cx + 34, 146);

  tft.setFont(&fonts::Font2);
  tft.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "SFI %.0f  K%d", r.sfi, r.kIndex);
  tft.drawString(buf, cx, 172);

  time_t now = time(nullptr);
  int ageMin = r.sondeTime ? (now - r.sondeTime) / 60 : -1;
  tft.setTextColor(fetchFailed ? TFT_RED : 0x8410);
  snprintf(buf, sizeof buf, fetchFailed ? "stale %dm" : "%d sondes %dm", r.sondeCount, ageMin);
  if (fetchFailed)
    snprintf(buf, sizeof buf, "stale, retrying");
  else
    snprintf(buf, sizeof buf, "%d sondes, %dm old", r.sondeCount, ageMin);
  tft.drawString(buf, cx, 190);
}

const char *stateName(State st) {
  return st == State::Open ? "OPEN" : st == State::Marginal ? "MARGINAL" : "closed";
}

uint16_t stateColor(State st) {
  return st == State::Open ? kColOpen : st == State::Marginal ? kColMarginal : 0x8410;
}

void drawTitle(const char *t, int idx) {
  char buf[32];
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(lgfx::middle_center);
  tft.setFont(&fonts::Font4);
  tft.setTextColor(TFT_CYAN);
  tft.drawString(t, 120, 26);
  tft.setFont(&fonts::Font2);
  tft.setTextColor(0x8410);
  snprintf(buf, sizeof buf, "%d/%d", idx + 1, kPageCount);
  tft.drawString(buf, 120, 222);
}

void drawSolarPage(const Report &r) {
  drawTitle("Solar", 1);
  char buf[32];
  tft.setFont(&fonts::Font4);
  tft.setTextColor(TFT_WHITE);
  snprintf(buf, sizeof buf, "SFI %.0f", r.sfi);
  tft.drawString(buf, 120, 62);
  snprintf(buf, sizeof buf, "A %.0f   K %d", r.aIndex, r.kIndex);
  tft.drawString(buf, 120, 94);
  snprintf(buf, sizeof buf, "SSN %.0f", r.sunspots);
  tft.drawString(buf, 120, 126);
  snprintf(buf, sizeof buf, "Wind %.0f km/s", r.solarWind);
  tft.drawString(buf, 120, 158);
  snprintf(buf, sizeof buf, "X-ray %s", r.xray[0] ? r.xray : "--");
  tft.drawString(buf, 120, 190);
}

void drawSondePage(const Report &r) {
  drawTitle("Ionosondes", 2);
  char buf[40];
  tft.setFont(&fonts::Font2);
  tft.setTextColor(0x8410);
  tft.drawString("km   MUF   foF2", 135, 52);
  tft.setTextColor(TFT_WHITE);
  for (int i = 0; i < r.sondeListCount; i++) {
    const Sonde &s = r.sondes[i];
    snprintf(buf, sizeof buf, "%s %4.0f %5.1f %5.2f", s.code, s.km, s.muf, s.fof2);
    tft.drawString(buf, 120, 76 + i * 22);
  }
}

void drawBandPage(const Report &r) {
  drawTitle("Bands", 3);
  char buf[40];
  tft.setFont(&fonts::Font2);
  for (int i = 0; i < kBandCount; i++) {
    snprintf(buf, sizeof buf, "%sm %5.1f  %s", kBands[i].label, kBands[i].mhz,
             stateName(r.bands[i]));
    tft.setTextColor(stateColor(r.bands[i]));
    tft.drawString(buf, 120, 56 + i * 20);
  }
}

void drawHamsterFrame() {
  if (!hamsterSprite.getBuffer()) {
    hamsterSprite.setColorDepth(4);
    if (!hamsterSprite.createSprite(hamster::kSize, hamster::kSize)) {
      Serial.printf("hamster sprite alloc failed, heap %u, largest %u\n", ESP.getFreeHeap(),
                    ESP.getMaxAllocHeap());
      return;
    }
    hamster::initPalette(hamsterSprite);
  }
  hamster::draw(hamsterSprite, hamsterFrame++);
  hamsterSprite.pushSprite(120 - hamster::kSize / 2, 120 - hamster::kSize / 2);
  lastHamsterMs = millis();
}

void drawHamsterPage() {
  tft.fillScreen(TFT_BLACK);
  drawHamsterFrame();
}

void drawPage() {
  if (page != 4) hamsterSprite.deleteSprite();
  if (!report.valid) return;
  switch (page) {
    case 1: drawSolarPage(report); break;
    case 2: drawSondePage(report); break;
    case 3: drawBandPage(report); break;
    case 4: drawHamsterPage(); break;
    default: drawReport(report);
  }
}

void refresh() {
  hamsterSprite.deleteSprite();  // free the hamster buffer before the TLS download
  Report next = report;
  uint32_t t0 = millis();
  bool ok = fetchIono(next);
  uint32_t t1 = millis();
  bool solarOk = fetchSolar(next);  // keeps previous SFI/K if it fails
  Serial.printf("fetch iono %s %lu ms, solar %s %lu ms, heap %u, rssi %d\n", ok ? "ok" : "FAIL",
                t1 - t0, solarOk ? "ok" : "FAIL", millis() - t1, ESP.getFreeHeap(), WiFi.RSSI());
  if (ok) {
    classify(next);
    report = next;
    fetchFailed = false;
    nextFetch = millis() + kRefreshMs;
  } else {
    fetchFailed = true;
    nextFetch = millis() + kRetryMs;
  }
  if (report.valid)
    drawPage();
  else
    drawMessage("No data", "retrying...");
}

void checkBootButton() {
  static uint32_t pressedAt = 0;
  bool down = digitalRead(kBootPin) == LOW;
  if (down) {
    if (!pressedAt) pressedAt = millis();
    if (millis() - pressedAt > 3000) {
      drawMessage("Reset Wi-Fi");
      prefs.begin("hamprop", false);
      prefs.clear();
      prefs.end();
      WiFiManager wm;
      wm.resetSettings();
      delay(800);
      ESP.restart();
    }
  } else if (pressedAt) {
    uint32_t held = millis() - pressedAt;
    pressedAt = 0;
    if (held > 40 && held < 1500) {
      page = (page + 1) % kPageCount;
      lastPageChange = millis();
      drawPage();
    }
  }
  if (page == 4 && millis() - lastHamsterMs >= hamster::kFrameMs) drawHamsterFrame();
  if (page != 0 && page != 4 && millis() - lastPageChange > kPageTimeoutMs) {
    page = 0;
    drawPage();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kBootPin, INPUT_PULLUP);
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  drawMessage("Connecting", "Wi-Fi...");

  prefs.begin("hamprop", true);
  applyLocator(prefs.getString("loc", kDefaultLocator).c_str());
  prefs.end();

  WiFiManager wm;
  WiFiManagerParameter locParam("loc", "Grid locator (e.g. JO62 or JO62qm)", gLocator, 6,
                                " pattern='[A-Ra-r]{2}[0-9]{2}([A-Xa-x]{2})?'");
  wm.addParameter(&locParam);
  wm.setSaveParamsCallback([&]() {
    applyLocator(locParam.getValue());
    prefs.begin("hamprop", false);
    prefs.putString("loc", gLocator);
    prefs.end();
  });
  wm.setAPCallback([](WiFiManager *) {
    drawMessage("Setup Wi-Fi", kPortalName);
  });
  wm.setConfigPortalTimeout(300);
  if (!wm.autoConnect(kPortalName)) ESP.restart();

  WiFi.setSleep(false);
  setenv("TZ", "UTC0", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  drawMessage("Syncing time");
  uint32_t tn = millis();
  for (int i = 0; i < 40 && time(nullptr) < 1700000000; i++) delay(250);
  Serial.printf("ntp %lu ms\n", millis() - tn);

  drawMessage("Fetching", "propagation data");
  refresh();
}

void loop() {
  checkBootButton();
  if ((int32_t)(millis() - nextFetch) >= 0) {
    if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    refresh();
  }
  delay(50);
}
