#include "../firmware/AmbientScope/src/AmbientScopeRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr int kSize = 392;
constexpr int kFrames = 120;
constexpr float kPi = 3.14159265358979323846f;

void writeFrame(const std::vector<uint16_t>& pixels) {
  static std::vector<uint8_t> rgb(static_cast<size_t>(kSize) * kSize * 3);
  for (size_t index = 0; index < pixels.size(); ++index) {
    const uint16_t pixel = pixels[index];
    rgb[index * 3] = static_cast<uint8_t>(((pixel >> 11) & 31) * 255 / 31);
    rgb[index * 3 + 1] =
        static_cast<uint8_t>(((pixel >> 5) & 63) * 255 / 63);
    rgb[index * 3 + 2] = static_cast<uint8_t>((pixel & 31) * 255 / 31);
  }
  std::fwrite(rgb.data(), 1, rgb.size(), stdout);
}

}

int main() {
  const size_t pixelCount = static_cast<size_t>(kSize) * kSize;
  std::vector<uint16_t> pixels(pixelCount);
  std::vector<uint16_t> persistence(pixelCount);
  copilot::AmbientScopeRenderer renderer(
      pixels.data(), persistence.data(), kSize, kSize);
  if (!renderer.begin()) return 1;

  copilot::AmbientScopeSnapshot snapshot;
  snapshot.available = true;
  snapshot.state = copilot::AmbientScopeState::Live;
  for (int frame = 0; frame < kFrames; ++frame) {
    const float time = frame / 20.0f;
    const float beatPhase = std::fmod(time, 1.2f);
    const float beat = std::exp(-beatPhase * beatPhase * 14.0f);
    const float frequency = 2.4f + 0.65f * std::sin(time * 0.8f);
    const float amplitude = 0.48f + beat * 0.28f;
    for (size_t index = 0;
         index < copilot::AmbientScopeSnapshot::kWaveformSamples; ++index) {
      const float x = index /
          static_cast<float>(copilot::AmbientScopeSnapshot::kWaveformSamples);
      snapshot.waveform[index] =
          amplitude *
          (0.64f * std::sin(2.0f * kPi * (frequency * x + time * 0.72f)) +
           0.24f * std::sin(2.0f * kPi * (frequency * 2.05f * x - time * 0.33f)) +
           0.12f * std::sin(2.0f * kPi * (frequency * 4.1f * x + time * 0.18f)));
    }
    for (size_t band = 0;
         band < copilot::AmbientScopeSnapshot::kSpectrumBands; ++band) {
      const float b = static_cast<float>(band);
      const float lowDistance = b - (3.0f + 1.4f * std::sin(time * 0.7f));
      const float midDistance = b - (11.0f + 2.0f * std::sin(time * 0.43f + 1.0f));
      const float highDistance = b - (19.0f + 1.5f * std::sin(time * 0.91f + 2.0f));
      snapshot.spectrum[band] = std::clamp(
          0.18f +
          0.48f * std::exp(-lowDistance * lowDistance / 9.0f) +
          0.62f * std::exp(-midDistance * midDistance / 14.0f) +
          0.40f * std::exp(-highDistance * highDistance / 8.0f) +
          beat * (0.12f + 0.16f * std::sin(b * 1.7f + time * 4.0f)),
          0.0f, 1.0f);
    }
    snapshot.sequence++;
    snapshot.state = copilot::AmbientScopeState::Live;
    renderer.render(static_cast<uint32_t>(frame * 50), snapshot);
    writeFrame(pixels);
  }
  return std::ferror(stdout) ? 1 : 0;
}
