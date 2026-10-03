#include "../firmware/AmbientScope/src/AmbientScopeRenderer.h"
#include "../firmware/AmbientScope/src/AudioAnalyzer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr float kPi = 3.14159265358979323846f;

void fillStereo(std::vector<int16_t>& samples, float frequency,
                float amplitude, float dc = 0.0f) {
  const size_t frames = samples.size() / 2;
  for (size_t frame = 0; frame < frames; ++frame) {
    const float wave = frequency > 0
        ? std::sin(2.0f * kPi * frequency * frame /
                   copilot::AudioAnalyzer::kSampleRate)
        : 0.0f;
    const int16_t value = static_cast<int16_t>(
        std::clamp((dc + wave * amplitude) * 32767.0f, -32767.0f, 32767.0f));
    samples[frame * 2] = value;
    samples[frame * 2 + 1] = value;
  }
}

uint64_t checksum(const uint16_t* pixels, size_t count) {
  uint64_t value = 1469598103934665603ULL;
  for (size_t index = 0; index < count; ++index) {
    value ^= pixels[index];
    value *= 1099511628211ULL;
  }
  return value;
}

}

int main() {
  constexpr size_t frames = 256;
  std::vector<int16_t> samples(frames * 2);
  copilot::AudioAnalyzer analyzer;
  analyzer.reset();

  fillStereo(samples, 0, 0, 0.28f);
  for (int block = 0; block < 80; ++block)
    analyzer.processInterleaved(samples.data(), frames, 2);
  auto snapshot = analyzer.snapshot();
  assert(snapshot.rms < 0.01f);
  assert(snapshot.noiseFloor < 0.01f);
  assert(snapshot.state == copilot::AmbientScopeState::Quiet);

  fillStereo(samples, 1000.0f, 0.18f);
  for (int block = 0; block < 8; ++block)
    analyzer.processInterleaved(samples.data(), frames, 2);
  snapshot = analyzer.snapshot();
  assert(snapshot.state == copilot::AmbientScopeState::Live);
  assert(snapshot.rms > 0.08f);
  assert(std::abs(snapshot.dominantHz - 1000.0f) <= 150.0f);
  assert(snapshot.mid > snapshot.low);
  assert(snapshot.mid > snapshot.high);
  assert(snapshot.automaticGain >= 0.6f && snapshot.automaticGain <= 28.0f);

  const float loudGain = snapshot.automaticGain;
  fillStereo(samples, 250.0f, 0.012f);
  for (int block = 0; block < 120; ++block)
    analyzer.processInterleaved(samples.data(), frames, 2);
  snapshot = analyzer.snapshot();
  assert(snapshot.automaticGain > loudGain);
  assert(snapshot.automaticGain <= 28.0f);
  assert(std::abs(snapshot.dominantHz - 250.0f) <= 150.0f);
  assert(snapshot.low >= snapshot.mid);

  std::fill(samples.begin(), samples.end(), 0);
  samples[20] = 32767;
  samples[21] = 32767;
  analyzer.processInterleaved(samples.data(), frames, 2);
  snapshot = analyzer.snapshot();
  assert(snapshot.state == copilot::AmbientScopeState::Clip);
  assert(snapshot.clipCount > 0);
  assert(snapshot.peak > 0.9f);

  constexpr int size = 392;
  constexpr size_t pixelCount = static_cast<size_t>(size) * size;
  constexpr uint16_t guard = 0x6d5a;
  std::vector<uint16_t> pixels(pixelCount + 16, guard);
  std::vector<uint16_t> persistence(pixelCount + 16, guard);
  copilot::AmbientScopeRenderer renderer(
      pixels.data() + 8, persistence.data() + 8, size, size);
  assert(renderer.begin());
  renderer.render(0, snapshot);
  for (int index = 0; index < 8; ++index) {
    assert(pixels[index] == guard);
    assert(persistence[index] == guard);
    assert(pixels[pixelCount + 8 + index] == guard);
    assert(persistence[pixelCount + 8 + index] == guard);
  }
  const uint64_t liveFrame = checksum(pixels.data() + 8, pixelCount);
  renderer.tap();
  assert(renderer.frozen());
  const uint64_t heldFrame = checksum(pixels.data() + 8, pixelCount);
  fillStereo(samples, 3000.0f, 0.2f);
  analyzer.processInterleaved(samples.data(), frames, 2);
  renderer.render(33, analyzer.snapshot());
  assert(renderer.frozen());
  assert(checksum(pixels.data() + 8, pixelCount) != 0);
  renderer.tap();
  assert(!renderer.frozen());
  renderer.render(66, analyzer.snapshot());
  assert(checksum(pixels.data() + 8, pixelCount) != heldFrame ||
         heldFrame != liveFrame);

  const auto originalView = renderer.view();
  renderer.swipeUp();
  assert(renderer.view() != originalView);
  const uint8_t originalRange = renderer.range();
  const uint8_t originalPersistence = renderer.persistence();
  renderer.swipeDown();
  assert(renderer.range() != originalRange);
  assert(renderer.persistence() != originalPersistence);
  renderer.reset();
  assert(!renderer.frozen());
  assert(renderer.view() == copilot::AmbientScopeView::Balanced);

  copilot::AmbientScopeSnapshot error;
  error.state = copilot::AmbientScopeState::MicError;
  renderer.render(99, error);
  assert(checksum(pixels.data() + 8, pixelCount) != 0);

  copilot::AmbientScopeRenderer invalid(nullptr, nullptr, size, size);
  assert(!invalid.begin());
  assert(invalid.error() != nullptr);

  std::cout << "PASS: Ambient Scope analysis, bounds, and controls\n";
}
