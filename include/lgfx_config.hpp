#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class Display : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel_;
  lgfx::Bus_SPI bus_;

public:
  Display() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.pin_sclk = 4;
      cfg.pin_mosi = 3;
      cfg.pin_miso = -1;
      cfg.pin_dc = 10;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = 1;
      cfg.pin_rst = 0;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.invert = true;
      cfg.rgb_order = false;
      panel_.config(cfg);
    }
    setPanel(&panel_);
  }
};
