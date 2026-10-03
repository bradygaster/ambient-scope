#include "AudioAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace copilot {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float clamp01(float value) {
  return std::max(0.0f, std::min(1.0f, value));
}

}

void AudioAnalyzer::reset() {
  if (!prepared_) {
    for (size_t index = 0; index < kTransformSize; ++index) {
      window_[index] =
          0.5f - 0.5f * std::cos(2.0f * kPi * index /
                                 static_cast<float>(kTransformSize - 1));
    }
    for (size_t bin = 1; bin < kTransformSize / 2; ++bin) {
      const float step = 2.0f * kPi * bin / kTransformSize;
      cosineStep_[bin] = std::cos(step);
      sineStep_[bin] = std::sin(step);
    }
    prepared_ = true;
  }
  dc_[0] = dc_[1] = 0;
  noiseFloor_ = 0.001f;
  gain_ = 1.0f;
  previousRms_ = 0;
  quietBlocks_ = 0;
  std::memset(smoothedBands_, 0, sizeof(smoothedBands_));
  snapshot_ = {};
}

void AudioAnalyzer::processInterleaved(const int16_t* samples, size_t frames,
                                       size_t channels) {
  if (!samples || frames < kTransformSize || channels == 0 || channels > 2) {
    snapshot_.available = false;
    snapshot_.state = AmbientScopeState::MicError;
    return;
  }

  float energy[2] = {};
  float peak[2] = {};
  int16_t rawMinimum[2] = {32767, 32767};
  int16_t rawMaximum[2] = {-32768, -32768};
  uint16_t nonzero[2] = {};
  bool clipped = false;
  float centered[kTransformSize] = {};
  const size_t usedChannels = std::min<size_t>(channels, 2);
  for (size_t channel = 0; channel < usedChannels; ++channel) {
    float localDc = dc_[channel];
    for (size_t frame = 0; frame < frames; ++frame) {
      const float raw = samples[frame * channels + channel] / 32768.0f;
      rawMinimum[channel] =
          std::min(rawMinimum[channel], samples[frame * channels + channel]);
      rawMaximum[channel] =
          std::max(rawMaximum[channel], samples[frame * channels + channel]);
      if (samples[frame * channels + channel] != 0) ++nonzero[channel];
      localDc += (raw - localDc) * 0.003f;
      const float value = raw - localDc;
      energy[channel] += value * value;
      peak[channel] = std::max(peak[channel], std::abs(value));
      clipped = clipped || std::abs(samples[frame * channels + channel]) >= 32700;
    }
    dc_[channel] = localDc;
  }
  const size_t selected =
      usedChannels == 2 && energy[1] > energy[0] ? 1u : 0u;
  const float rms = std::sqrt(energy[selected] / frames);
  const float rawPeak = peak[selected];

  if (rms < std::max(0.025f, noiseFloor_ * 2.2f)) {
    const float rate = rms < noiseFloor_ ? 0.025f : 0.0025f;
    noiseFloor_ += (std::max(0.00005f, rms) - noiseFloor_) * rate;
  }
  noiseFloor_ = std::max(0.00005f, std::min(0.08f, noiseFloor_));

  const float desiredGain =
      std::max(0.6f, std::min(28.0f, 0.42f / std::max(0.006f, rawPeak)));
  const float gainRate = desiredGain < gain_ ? 0.34f : 0.025f;
  gain_ += (desiredGain - gain_) * gainRate;
  gain_ = std::max(0.6f, std::min(28.0f, gain_));

  for (size_t index = 0; index < kTransformSize; ++index) {
    const size_t frame = index;
    const float raw = samples[frame * channels + selected] / 32768.0f;
    centered[index] = (raw - dc_[selected]) * gain_;
  }
  for (size_t index = 0; index < AmbientScopeSnapshot::kWaveformSamples;
       ++index) {
    const size_t frame = index * frames /
                         AmbientScopeSnapshot::kWaveformSamples;
    const float raw = samples[frame * channels + selected] / 32768.0f;
    snapshot_.waveform[index] =
        std::max(-1.0f, std::min(1.0f, (raw - dc_[selected]) * gain_));
  }

  float binEnergy[kTransformSize / 2] = {};
  float strongest = 0;
  size_t strongestBin = 0;
  for (size_t bin = 1; bin < kTransformSize / 2; ++bin) {
    float real = 0;
    float imaginary = 0;
    float cosine = 1.0f;
    float sine = 0.0f;
    for (size_t index = 0; index < kTransformSize; ++index) {
      real += centered[index] * window_[index] * cosine;
      imaginary -= centered[index] * window_[index] * sine;
      const float nextCosine =
          cosine * cosineStep_[bin] - sine * sineStep_[bin];
      sine = sine * cosineStep_[bin] + cosine * sineStep_[bin];
      cosine = nextCosine;
    }
    binEnergy[bin] =
        std::sqrt(real * real + imaginary * imaginary) /
        (kTransformSize * 0.25f);
    if (binEnergy[bin] > strongest) {
      strongest = binEnergy[bin];
      strongestBin = bin;
    }
  }

  float low = 0;
  float mid = 0;
  float high = 0;
  for (size_t bin = 1; bin < kTransformSize / 2; ++bin) {
    const float frequency =
        bin * static_cast<float>(kSampleRate) / kTransformSize;
    const float value = clamp01(binEnergy[bin] * 2.8f);
    if (frequency < 400.0f) low = std::max(low, value);
    else if (frequency < 2000.0f) mid = std::max(mid, value);
    else high = std::max(high, value);
  }
  for (size_t band = 0; band < AmbientScopeSnapshot::kSpectrumBands; ++band) {
    const float startRatio =
        band / static_cast<float>(AmbientScopeSnapshot::kSpectrumBands);
    const float endRatio =
        (band + 1) / static_cast<float>(AmbientScopeSnapshot::kSpectrumBands);
    const size_t first = 1 + static_cast<size_t>(
        std::pow(startRatio, 1.7f) * (kTransformSize / 2 - 2));
    const size_t last = std::max(first + 1, 1 + static_cast<size_t>(
        std::pow(endRatio, 1.7f) * (kTransformSize / 2 - 2)));
    float value = 0;
    for (size_t bin = first; bin <= std::min(last, kTransformSize / 2 - 1);
         ++bin) {
      value = std::max(value, binEnergy[bin]);
    }
    const float normalized = clamp01(value * 2.8f);
    smoothedBands_[band] +=
        (normalized - smoothedBands_[band]) *
        (normalized > smoothedBands_[band] ? 0.55f : 0.15f);
    snapshot_.spectrum[band] = smoothedBands_[band];
  }

  snapshot_.rms = rms;
  snapshot_.peak = rawPeak;
  snapshot_.noiseFloor = noiseFloor_;
  snapshot_.automaticGain = gain_;
  snapshot_.relativeDb =
      std::max(-60.0f, std::min(12.0f, 20.0f * std::log10(
          std::max(0.001f, rms / std::max(noiseFloor_, 0.00005f)))));
  snapshot_.low = low;
  snapshot_.mid = mid;
  snapshot_.high = high;
  snapshot_.dominantHz =
      strongest > std::max(0.015f, noiseFloor_ * gain_ * 2.0f)
          ? strongestBin * static_cast<float>(kSampleRate) / kTransformSize
          : 0.0f;
  snapshot_.rawMinimum = rawMinimum[selected];
  snapshot_.rawMaximum = rawMaximum[selected];
  snapshot_.nonzeroCount = nonzero[selected];
  snapshot_.available = true;
  snapshot_.sequence++;
  if (clipped) {
    snapshot_.clipCount++;
    snapshot_.state = AmbientScopeState::Clip;
    quietBlocks_ = 0;
  } else if (rms < noiseFloor_ * 1.8f) {
    quietBlocks_++;
    snapshot_.state =
        quietBlocks_ > 8 ? AmbientScopeState::Quiet
                         : AmbientScopeState::Live;
  } else {
    quietBlocks_ = 0;
    snapshot_.state = AmbientScopeState::Live;
  }
  previousRms_ = rms;
}

}  // namespace copilot
