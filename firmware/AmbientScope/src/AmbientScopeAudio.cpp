#include "AmbientScopeAudio.h"

#include <Arduino.h>
#include <ESP_I2S.h>

#include <atomic>

#include "Config.h"
#include "audio/Es7210Capture.h"

namespace copilot {
namespace {

I2SClass i2s;
AudioAnalyzer analyzer;
Es7210Capture codec;
AmbientScopeSnapshot snapshots[2];
std::atomic<uint8_t> snapshotIndex{0};
std::atomic<bool> ready{false};

void captureBlock() {
  constexpr size_t kFrames = 256;
  static int16_t stereo[kFrames * 2];
  const size_t bytes =
      i2s.readBytes(reinterpret_cast<char*>(stereo), sizeof(stereo));
  if (bytes != sizeof(stereo)) {
    ready.store(false);
    vTaskDelay(pdMS_TO_TICKS(5));
    return;
  }

  analyzer.processInterleaved(stereo, kFrames, 2);
  AmbientScopeSnapshot snapshot = analyzer.snapshot();
  const bool active =
      snapshot.nonzeroCount > kFrames / 8 &&
      snapshot.rawMaximum - snapshot.rawMinimum > 2;
  static unsigned deadBlocks = 0;
  deadBlocks = active ? 0 : deadBlocks + 1;
  if (deadBlocks > 125) {
    snapshot.available = false;
    snapshot.state = AmbientScopeState::MicError;
  }

  const uint8_t next = snapshotIndex.load(std::memory_order_relaxed) ^ 1u;
  snapshots[next] = snapshot;
  snapshotIndex.store(next, std::memory_order_release);
  vTaskDelay(1);
}

void audioTask(void*) {
  pinMode(kAudioAmplifierPin, OUTPUT);
  digitalWrite(kAudioAmplifierPin, LOW);
  i2s.setPins(kAudioBclkPin, kAudioWordSelectPin, -1, kAudioDataInPin,
              kAudioMclkPin);
  if (!i2s.begin(I2S_MODE_STD, AudioAnalyzer::kSampleRate,
                 I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
                 I2S_STD_SLOT_BOTH)) {
    Serial.println("AUDIO error=i2s-init");
    vTaskDelete(nullptr);
    return;
  }

  analyzer.reset();
  if (!codec.begin(Wire, AudioAnalyzer::kSampleRate)) {
    Serial.printf("AUDIO error=es7210-init detail=%s\n", codec.error());
    i2s.end();
    vTaskDelete(nullptr);
    return;
  }

  ready.store(true);
  Serial.printf("AUDIO ready sample_rate=%u codec=ES7210\n",
                AudioAnalyzer::kSampleRate);
  while (true) captureBlock();
}

}  // namespace

bool beginAmbientScopeAudio() {
  return xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 1, nullptr,
                                 0) == pdPASS;
}

bool ambientScopeAudioReady() {
  return ready.load();
}

AmbientScopeSnapshot ambientScopeSnapshot() {
  return snapshots[snapshotIndex.load(std::memory_order_acquire)];
}

}  // namespace copilot
