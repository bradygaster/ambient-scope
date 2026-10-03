#pragma once

#include <cstdint>

namespace copilot {

constexpr int kDisplaySize = 466;
constexpr int kTargetFps = 30;
constexpr int kBrightness = 155;
constexpr int kSpiFrequency = 80000000;

constexpr int kAudioMclkPin = 42;
constexpr int kAudioBclkPin = 9;
constexpr int kAudioWordSelectPin = 45;
constexpr int kAudioDataInPin = 10;
constexpr int kAudioAmplifierPin = 46;

constexpr int kTouchSda = 15;
constexpr int kTouchScl = 14;
constexpr int kTouchInterrupt = 11;
constexpr int kTouchReset = 40;
constexpr unsigned kTouchDebounceMs = 180;
constexpr int kTouchTapTravelPixels = 24;
constexpr unsigned kTouchTapMaxMs = 650;
constexpr int kTouchSwipePixels = 72;
constexpr unsigned kTouchSwipeMaxMs = 1200;

}  // namespace copilot
