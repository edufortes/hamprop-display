#pragma once
#include <LovyanGFX.hpp>
#include <math.h>

// Pixel-art hamster running in a wheel, drawn on a 40x40 grid of 6 px cells.
namespace hamster {

constexpr int kCell = 6;
constexpr int kGrid = 40;
constexpr int kSize = kGrid * kCell;
constexpr uint32_t kFrameMs = 90;

constexpr const char *kBody[10] = {
    "..............oo....", "....oooooooo..orro..", "..oobbbbbbbboorrro..",
    ".obbbbbbbbbbbbbbbbo.", ".obbbbbbbbbbbbbbebo.", "obbbbbbbbbbbbbbbbbpp",
    "obbbbbbbbbbbbbbbbbbo", "obbwwwwwwwwwwwwbbbo.", ".obbwwwwwwwwwwwwbo..",
    "..ooobbbbbbbbbbooo..",
};
constexpr const char *kLegs[4][2] = {
    {"...opo.......opo...", "....oo.........oo..."},
    {"....opo.....opo....", "....oo.........oo..."},
    {"......opo..opo.....", "......oo.......oo...."},
    {"....opo.....opo....", "....oo.........oo..."},
};

// Palette indices (the sprite is 4-bit).
enum : uint8_t { kBlack, kRim, kSpoke, kOutline, kBody_, kBelly, kEye, kNose, kEar, kGrey };

inline void initPalette(lgfx::LGFX_Sprite &s) {
  s.createPalette();
  s.setPaletteColor(kBlack, 0, 0, 0);
  s.setPaletteColor(kRim, 255, 200, 40);
  s.setPaletteColor(kSpoke, 150, 115, 30);
  s.setPaletteColor(kOutline, 70, 40, 20);
  s.setPaletteColor(kBody_, 235, 150, 60);
  s.setPaletteColor(kBelly, 255, 240, 220);
  s.setPaletteColor(kEye, 10, 10, 10);
  s.setPaletteColor(kNose, 255, 120, 140);
  s.setPaletteColor(kEar, 255, 170, 170);
  s.setPaletteColor(kGrey, 123, 125, 123);
}

inline uint8_t color(char c) {
  switch (c) {
    case 'o': return kOutline;
    case 'b': return kBody_;
    case 'w': return kBelly;
    case 'e': return kEye;
    case 'p': return kNose;
    case 'r': return kEar;
  }
  return kBlack;
}

inline void cell(lgfx::LGFX_Sprite &s, int x, int y, uint8_t c) {
  if (x < 0 || y < 0 || x >= kGrid || y >= kGrid) return;
  s.fillRect(x * kCell, y * kCell, kCell, kCell, c);
}

inline void draw(lgfx::LGFX_Sprite &s, uint32_t t) {
  const uint8_t rim = kRim, spoke = kSpoke;
  s.fillScreen(kBlack);

  for (int y = 0; y < kGrid; y++)
    for (int x = 0; x < kGrid; x++) {
      float d = hypotf(x + 0.5f - 20, y + 0.5f - 20);
      if (d >= 16 && d < 17.2f) cell(s, x, y, rim);
    }
  float ang = (t % 8) * 11.25f * PI / 180.0f;
  for (int k = 0; k < 4; k++) {
    float a = ang + k * PI / 2;
    for (float r = 1.5f; r < 16; r += 0.25f)
      cell(s, (int)roundf(19.5f + r * cosf(a)), (int)roundf(19.5f + r * sinf(a)), spoke);
  }
  for (int y = 19; y <= 20; y++)
    for (int x = 19; x <= 20; x++) cell(s, x, y, rim);

  int bob = t % 2, ox = 10, oy = 23 - bob;
  for (int j = 0; j < 12; j++) {
    const char *row = j < 10 ? kBody[j] : kLegs[t % 4][j - 10];
    for (int i = 0; row[i]; i++)
      if (row[i] != '.') cell(s, ox + i, oy + j + (j < 10 ? 0 : bob), color(row[i]));
  }
  for (int k = 0; k < 3; k++) {
    int x = 3 + ((t * 2 + k * 4) % 10);
    cell(s, x, 24 + k * 3, kGrey);
    cell(s, x + 1, 24 + k * 3, kGrey);
  }
}

}  // namespace hamster
