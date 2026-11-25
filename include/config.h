#pragma once

constexpr const char *kDefaultLocator = "JO62";

constexpr const char *kPortalName = "HamProp-Setup";
constexpr int kBootPin = 9;

constexpr uint32_t kRefreshMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kRetryMs = 30UL * 1000UL;

constexpr uint32_t kMaxSondeAgeS = 3 * 3600;
constexpr float kMaxSondeKm = 1500.0f;

constexpr const char *kIonoUrl = "https://prop.kc2g.com/api/stations.json";
constexpr const char *kSolarUrl = "https://www.hamqsl.com/solarxml.php";
