#pragma once

#include <cstddef>
#include <cstdint>

namespace copilot {

enum class AmbientScopeState : uint8_t {
  MicError,
  Quiet,
  Live,
  Clip,
};

struct AmbientScopeSnapshot {
  static constexpr size_t kWaveformSamples = 160;
  static constexpr size_t kSpectrumBands = 24;

  float waveform[kWaveformSamples] = {};
  float spectrum[kSpectrumBands] = {};
  float rms = 0;
  float peak = 0;
  float relativeDb = -60;
  float noiseFloor = 0.001f;
  float automaticGain = 1;
  float low = 0;
  float mid = 0;
  float high = 0;
  float dominantHz = 0;
  uint32_t sequence = 0;
  uint32_t clipCount = 0;
  int16_t rawMinimum = 0;
  int16_t rawMaximum = 0;
  uint16_t nonzeroCount = 0;
  AmbientScopeState state = AmbientScopeState::MicError;
  bool available = false;
};

class AudioAnalyzer {
 public:
  static constexpr unsigned kSampleRate = 16000;
  static constexpr size_t kTransformSize = 128;

  void reset();
  void processInterleaved(const int16_t* samples, size_t frames,
                          size_t channels);
  const AmbientScopeSnapshot& snapshot() const { return snapshot_; }

 private:
  float dc_[2] = {};
  float noiseFloor_ = 0.001f;
  float gain_ = 1.0f;
  float window_[kTransformSize] = {};
  float cosineStep_[kTransformSize / 2] = {};
  float sineStep_[kTransformSize / 2] = {};
  float smoothedBands_[AmbientScopeSnapshot::kSpectrumBands] = {};
  float previousRms_ = 0;
  uint32_t quietBlocks_ = 0;
  bool prepared_ = false;
  AmbientScopeSnapshot snapshot_;
};

}  // namespace copilot
