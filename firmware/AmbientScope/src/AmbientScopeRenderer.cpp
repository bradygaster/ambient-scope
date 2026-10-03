#include "AmbientScopeRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace copilot {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr uint16_t kDeepBlue = 0x0824;
constexpr uint16_t kGrid = 0x18e7;
constexpr uint16_t kCyan = 0x07ff;
constexpr uint16_t kMint = 0x67f3;
constexpr uint16_t kAmber = 0xfde0;
constexpr uint16_t kRed = 0xf986;

uint16_t dim565(uint16_t color, unsigned shift) {
  if (shift == 0) return color;
  const unsigned red = ((color >> 11) & 31u) >> shift;
  const unsigned green = ((color >> 5) & 63u) >> shift;
  const unsigned blue = (color & 31u) >> shift;
  return static_cast<uint16_t>((red << 11) | (green << 5) | blue);
}

}

AmbientScopeRenderer::AmbientScopeRenderer(
    uint16_t* pixels, uint16_t* persistence, int width, int height)
    : pixels_(pixels), persistenceBuffer_(persistence),
      width_(width), height_(height) {}

bool AmbientScopeRenderer::begin() {
  if (!pixels_ || !persistenceBuffer_ || width_ <= 0 || height_ <= 0 ||
      static_cast<uint64_t>(width_) * height_ >
          std::numeric_limits<size_t>::max() / sizeof(uint16_t)) {
    error_ = "Ambient Scope requires two valid frame buffers.";
    return false;
  }
  centerX_ = width_ / 2;
  centerY_ = height_ / 2;
  radius_ = std::min(width_, height_) / 2 - 2;
  begun_ = true;
  reset();
  return true;
}

void AmbientScopeRenderer::reset() {
  if (!begun_) return;
  frozen_ = false;
  view_ = AmbientScopeView::Balanced;
  range_ = 1;
  persistence_ = 2;
  held_ = {};
  std::memset(waveformHistory_, 0, sizeof(waveformHistory_));
  historyHead_ = 0;
  historyCount_ = 0;
  lastStoredSequence_ = 0;
  std::memset(persistenceBuffer_, 0,
              static_cast<size_t>(width_) * height_ * sizeof(uint16_t));
}

void AmbientScopeRenderer::tap() {
  if (!begun_) return;
  frozen_ = !frozen_;
}

void AmbientScopeRenderer::swipeUp() {
  if (!begun_) return;
  view_ = static_cast<AmbientScopeView>(
      (static_cast<unsigned>(view_) + 1u) % 3u);
}

void AmbientScopeRenderer::swipeDown() {
  if (!begun_) return;
  range_ = static_cast<uint8_t>((range_ + 1u) % 3u);
  persistence_ = static_cast<uint8_t>((persistence_ + 1u) % 4u);
}

void AmbientScopeRenderer::pixel(int x, int y, uint16_t color) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
  const int dx = x - centerX_;
  const int dy = y - centerY_;
  if (dx * dx + dy * dy > radius_ * radius_) return;
  pixels_[static_cast<size_t>(y) * width_ + x] = color;
}

void AmbientScopeRenderer::line(
    int x0, int y0, int x1, int y1, uint16_t color) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  for (;;) {
    pixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    const int twice = error * 2;
    if (twice >= dy) { error += dy; x0 += sx; }
    if (twice <= dx) { error += dx; y0 += sy; }
  }
}

void AmbientScopeRenderer::circle(
    int cx, int cy, int radius, uint16_t color) {
  int x = radius;
  int y = 0;
  int error = 1 - radius;
  while (x >= y) {
    pixel(cx + x, cy + y, color); pixel(cx + y, cy + x, color);
    pixel(cx - y, cy + x, color); pixel(cx - x, cy + y, color);
    pixel(cx - x, cy - y, color); pixel(cx - y, cy - x, color);
    pixel(cx + y, cy - x, color); pixel(cx + x, cy - y, color);
    ++y;
    if (error < 0) error += 2 * y + 1;
    else { --x; error += 2 * (y - x) + 1; }
  }
}

void AmbientScopeRenderer::fillCircle(
    int cx, int cy, int radius, uint16_t color) {
  for (int y = -radius; y <= radius; ++y) {
    const int extent = static_cast<int>(
        std::sqrt(std::max(0, radius * radius - y * y)));
    line(cx - extent, cy + y, cx + extent, cy + y, color);
  }
}

void AmbientScopeRenderer::drawWaveform(
    const AmbientScopeSnapshot& snapshot) {
  const int left = 38;
  const int right = width_ - 39;
  const int center = centerY_ + 12;
  const float rangeScale[] = {0.65f, 0.92f, 1.2f};
  const float amplitude = 91.0f * rangeScale[range_];
  line(left, center, right, center, kGrid);
  line(left, center - 46, right, center - 46, dim565(kGrid, 1));
  line(left, center + 46, right, center + 46, dim565(kGrid, 1));
  for (int division = 0; division <= 8; ++division) {
    const int x = left + (right - left) * division / 8;
    line(x, center - 82, x, center + 82, dim565(kGrid, 1));
  }
  const uint8_t trails = std::min<uint8_t>(persistence_, historyCount_);
  for (uint8_t trail = trails; trail > 0; --trail) {
    const uint8_t historyIndex =
        static_cast<uint8_t>((historyHead_ + 4u - trail) % 4u);
    int previousX = left;
    int previousY = center;
    const uint16_t color = dim565(kCyan, std::min<unsigned>(3, trail + 1));
    for (size_t index = 0; index < AmbientScopeSnapshot::kWaveformSamples;
         ++index) {
      const int x = left + static_cast<int>(
          index * (right - left) /
          (AmbientScopeSnapshot::kWaveformSamples - 1));
      const int y = center - static_cast<int>(
          std::max(-1.0f, std::min(1.0f,
              waveformHistory_[historyIndex][index])) * amplitude);
      line(previousX, previousY, x, y, color);
      previousX = x;
      previousY = y;
    }
  }
  int previousX = left;
  int previousY = center;
  for (size_t index = 0; index < AmbientScopeSnapshot::kWaveformSamples;
       ++index) {
    const int x = left + static_cast<int>(
        index * (right - left) /
        (AmbientScopeSnapshot::kWaveformSamples - 1));
    const int y = center - static_cast<int>(
        std::max(-1.0f, std::min(1.0f, snapshot.waveform[index])) *
        amplitude);
    line(previousX, previousY - 1, x, y - 1, dim565(kCyan, 1));
    line(previousX, previousY, x, y, kMint);
    previousX = x;
    previousY = y;
  }
}

void AmbientScopeRenderer::drawSpectrum(
    const AmbientScopeSnapshot& snapshot) {
  const float baseRadius =
      radius_ * (view_ == AmbientScopeView::Spectrum ? 0.52f : 0.78f);
  const float maximum =
      radius_ * (view_ == AmbientScopeView::Spectrum ? 0.36f : 0.16f);
  for (size_t band = 0; band < AmbientScopeSnapshot::kSpectrumBands; ++band) {
    const float angle = -kPi * 0.5f +
        band * 2.0f * kPi / AmbientScopeSnapshot::kSpectrumBands;
    const float value = snapshot.spectrum[band];
    const float outer = baseRadius + maximum * value;
    const int x0 = centerX_ + static_cast<int>(std::cos(angle) * baseRadius);
    const int y0 = centerY_ + static_cast<int>(std::sin(angle) * baseRadius);
    const int x1 = centerX_ + static_cast<int>(std::cos(angle) * outer);
    const int y1 = centerY_ + static_cast<int>(std::sin(angle) * outer);
    const uint16_t color =
        band < 7 ? kCyan : band < 16 ? kMint : kAmber;
    line(x0, y0, x1, y1, color);
    fillCircle(x1, y1, value > 0.55f ? 2 : 1, color);
  }
  circle(centerX_, centerY_, static_cast<int>(baseRadius), kGrid);
}

void AmbientScopeRenderer::drawReadout(
    const AmbientScopeSnapshot& snapshot) {
  if (snapshot.state == AmbientScopeState::MicError) {
    circle(centerX_, centerY_, radius_ - 8, kRed);
    circle(centerX_, centerY_, radius_ - 18, dim565(kRed, 1));
    line(centerX_ - 44, centerY_ - 44,
         centerX_ + 44, centerY_ + 44, kRed);
    line(centerX_ + 44, centerY_ - 44,
         centerX_ - 44, centerY_ + 44, kRed);
    return;
  }
  const uint16_t stateColor =
      snapshot.state == AmbientScopeState::Clip ? kRed :
      snapshot.state == AmbientScopeState::Quiet ? dim565(kCyan, 1) : kMint;
  circle(centerX_, centerY_, radius_ - 8, stateColor);
  if (snapshot.state == AmbientScopeState::Clip)
    circle(centerX_, centerY_, radius_ - 13, dim565(kRed, 1));
  if (frozen_) {
    circle(centerX_, centerY_, radius_ - 18, kAmber);
    fillCircle(centerX_, centerY_, 4, kAmber);
  }
}

void AmbientScopeRenderer::render(
    uint32_t, const AmbientScopeSnapshot& snapshot) {
  if (!begun_) return;
  if (!frozen_ || held_.sequence == 0) held_ = snapshot;
  const AmbientScopeSnapshot& shown = frozen_ ? held_ : snapshot;

  const size_t count = static_cast<size_t>(width_) * height_;
  std::fill(pixels_, pixels_ + count, static_cast<uint16_t>(0));
  for (int radius = 40; radius < radius_; radius += 40)
    circle(centerX_, centerY_, radius, dim565(kDeepBlue, 1));
  circle(centerX_, centerY_, radius_ - 1, kGrid);

  if (shown.state != AmbientScopeState::MicError) {
    if (view_ != AmbientScopeView::Waveform) drawSpectrum(shown);
    if (view_ != AmbientScopeView::Spectrum) drawWaveform(shown);
  }
  drawReadout(shown);
  if (!frozen_ && shown.available && shown.sequence != lastStoredSequence_) {
    std::memcpy(waveformHistory_[historyHead_], shown.waveform,
                sizeof(shown.waveform));
    historyHead_ = static_cast<uint8_t>((historyHead_ + 1u) % 4u);
    historyCount_ = std::min<uint8_t>(4, historyCount_ + 1u);
    lastStoredSequence_ = shown.sequence;
  }
}

}  // namespace copilot
