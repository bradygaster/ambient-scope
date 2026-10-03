#pragma once

#include <cstdint>

#include "AudioAnalyzer.h"

namespace copilot {

enum class AmbientScopeView : uint8_t {
  Waveform,
  Spectrum,
  Balanced,
};

class AmbientScopeRenderer {
 public:
  AmbientScopeRenderer(uint16_t* pixels, uint16_t* persistence,
                       int width, int height);

  bool begin();
  const char* error() const { return error_; }
  void render(uint32_t milliseconds, const AmbientScopeSnapshot& snapshot);
  void reset();
  void tap();
  void swipeUp();
  void swipeDown();

  bool frozen() const { return frozen_; }
  AmbientScopeView view() const { return view_; }
  uint8_t range() const { return range_; }
  uint8_t persistence() const { return persistence_; }

 private:
  void pixel(int x, int y, uint16_t color);
  void line(int x0, int y0, int x1, int y1, uint16_t color);
  void circle(int cx, int cy, int radius, uint16_t color);
  void fillCircle(int cx, int cy, int radius, uint16_t color);
  void drawWaveform(const AmbientScopeSnapshot& snapshot);
  void drawSpectrum(const AmbientScopeSnapshot& snapshot);
  void drawReadout(const AmbientScopeSnapshot& snapshot);

  uint16_t* pixels_;
  uint16_t* persistenceBuffer_;
  int width_;
  int height_;
  int centerX_ = 0;
  int centerY_ = 0;
  int radius_ = 0;
  bool begun_ = false;
  bool frozen_ = false;
  AmbientScopeView view_ = AmbientScopeView::Balanced;
  uint8_t range_ = 1;
  uint8_t persistence_ = 2;
  float waveformHistory_[4][AmbientScopeSnapshot::kWaveformSamples] = {};
  uint8_t historyHead_ = 0;
  uint8_t historyCount_ = 0;
  uint32_t lastStoredSequence_ = 0;
  AmbientScopeSnapshot held_;
  const char* error_ = nullptr;
};

}  // namespace copilot
