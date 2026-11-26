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

// Open-Meteo: surface and 850 hPa data for the tropo ducting estimate (lat, lon)
constexpr const char *kTropoUrlFmt =
    "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
    "&current=temperature_2m,relative_humidity_2m,surface_pressure"
    "&hourly=temperature_850hPa,relative_humidity_850hPa,geopotential_height_850hPa"
    "&forecast_hours=1&timezone=UTC";
