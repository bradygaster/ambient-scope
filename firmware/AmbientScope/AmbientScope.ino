#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdarg>
#include <cstring>

#include "src/AmbientScopeAudio.h"
#include "src/AmbientScopeRenderer.h"
#include "src/Config.h"
#include "src/TouchInput.h"

using namespace copilot;

namespace {

Arduino_ESP32QSPI displayBus(12, 38, 4, 5, 6, 7);
Arduino_CO5300 display(&displayBus, 39, 0, 466, 466, 6, 0, 0, 0);
constexpr size_t kTransferBytes = 16384;
constexpr int kViewport = 392;
constexpr int kViewportX = (kDisplaySize - kViewport) / 2;
constexpr int kViewportY = (kDisplaySize - kViewport) / 2;
uint8_t* transferBuffer = nullptr;
uint16_t* pixels = nullptr;
uint16_t* backdrop = nullptr;
AmbientScopeRenderer* scope = nullptr;

void logBounded(const char* format, ...) {
  char message[192];
  va_list arguments;
  va_start(arguments, format);
  const int length = vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(message) ||
      !Serial || Serial.availableForWrite() < length) {
    return;
  }
  Serial.write(reinterpret_cast<const uint8_t*>(message), length);
}

[[noreturn]] void fatal(const char* message) {
  logBounded("FATAL %s\n", message);
  display.fillScreen(0);
  display.setTextColor(0xf81f);
  display.setTextSize(2);
  display.setCursor(130, 220);
  display.print("AMBIENT SCOPE ERROR");
  for (;;) delay(2000);
}

void present() {
  display.startWrite();
  display.writeAddrWindow(kViewportX, kViewportY, kViewport, kViewport);
  const auto* bytes = reinterpret_cast<const uint8_t*>(pixels);
  constexpr size_t frameBytes = kViewport * kViewport * sizeof(uint16_t);
  for (size_t offset = 0; offset < frameBytes; offset += kTransferBytes) {
    const size_t count = std::min(kTransferBytes, frameBytes - offset);
    std::memcpy(transferBuffer, bytes + offset, count);
    displayBus.writeBytes(transferBuffer, count);
  }
  display.endWrite();
}

}  // namespace

void setup() {
  Serial.setTxBufferSize(2048);
  Serial.setTxTimeoutMs(0);
  Serial.begin(115200);
  if (!psramFound()) fatal("8 MB OPI PSRAM not detected.");
  if (!display.begin(kSpiFrequency)) fatal("CO5300 initialization failed.");
  display.setBrightness(0);
  display.fillScreen(0);
  if (!initializeTouchInput()) fatal(touchInputError());

  constexpr size_t frameBytes = kViewport * kViewport * sizeof(uint16_t);
  pixels = static_cast<uint16_t*>(
      heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  backdrop = static_cast<uint16_t*>(
      heap_caps_malloc(frameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  transferBuffer = static_cast<uint8_t*>(heap_caps_aligned_alloc(
      16, kTransferBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (!pixels || !backdrop || !transferBuffer) {
    fatal("Frame buffer allocation failed.");
  }

  static AmbientScopeRenderer renderer(pixels, backdrop, kViewport, kViewport);
  scope = &renderer;
  if (!scope->begin()) fatal(scope->error());

  AmbientScopeSnapshot starting;
  starting.state = AmbientScopeState::MicError;
  scope->render(millis(), starting);
  present();
  if (!beginAmbientScopeAudio()) fatal("Audio task creation failed.");
  for (int brightness = 0; brightness <= kBrightness; brightness += 5) {
    display.setBrightness(brightness);
    delay(8);
  }
  display.setBrightness(kBrightness);
  logBounded("READY Ambient Scope mic=ES7210 privacy=aggregate-only\n");
}

void loop() {
  static uint32_t nextFrame = 0;
  static uint32_t reportAt = 0;
  static uint32_t frameCount = 0;
  static uint32_t previousFrameAt = 0;
  static uint32_t worstGap = 0;
  static uint32_t lastSequence = 0;
  static uint32_t staleSince = 0;
  const uint32_t now = millis();
  constexpr uint32_t kFrameInterval = 1000 / kTargetFps;
  if (!nextFrame) nextFrame = now;
  if (static_cast<int32_t>(now - nextFrame) < 0) {
    delay(1);
    return;
  }
  nextFrame += kFrameInterval;
  if (static_cast<int32_t>(now - nextFrame) >
      static_cast<int32_t>(kFrameInterval)) {
    nextFrame = now + kFrameInterval;
  }
  if (previousFrameAt) worstGap = std::max(worstGap, now - previousFrameAt);
  previousFrameAt = now;

  TouchGesture gesture;
  TouchState touch;
  if (pollTouchInput(gesture, touch)) {
    if (gesture.kind == TouchGestureKind::Tap) {
      scope->tap();
    } else if (gesture.kind == TouchGestureKind::SwipeUp) {
      scope->swipeUp();
    } else if (gesture.kind == TouchGestureKind::SwipeDown) {
      scope->swipeDown();
    }
  }
  if (touchInputError()) fatal(touchInputError());

  AmbientScopeSnapshot snapshot = ambientScopeSnapshot();
  if (snapshot.sequence != lastSequence) {
    lastSequence = snapshot.sequence;
    staleSince = now;
  } else if (!staleSince) {
    staleSince = now;
  } else if (now - staleSince > 1000) {
    snapshot.available = false;
    snapshot.state = AmbientScopeState::MicError;
  }

  scope->render(now, snapshot);
  present();
  ++frameCount;
  if (now - reportAt >= 5000) {
    logBounded("SCOPE fps=%.1f psram=%u heap=%u gap_ms=%u audio=%u state=%u "
               "seq=%u hold=%u view=%u range=%u\n",
               frameCount * 1000.0f /
                   std::max<uint32_t>(1, now - reportAt),
               ESP.getFreePsram(), ESP.getFreeHeap(), worstGap,
               static_cast<unsigned>(ambientScopeAudioReady()),
               static_cast<unsigned>(snapshot.state), snapshot.sequence,
               static_cast<unsigned>(scope->frozen()),
               static_cast<unsigned>(scope->view()),
               static_cast<unsigned>(scope->range()));
    logBounded("MIC rms=%.6f peak=%.5f db=%.1f noise=%.6f gain=%.2f hz=%.0f "
               "bands=%.2f/%.2f/%.2f raw=%d..%d nz=%u clips=%u\n",
               snapshot.rms, snapshot.peak, snapshot.relativeDb,
               snapshot.noiseFloor, snapshot.automaticGain,
               snapshot.dominantHz, snapshot.low, snapshot.mid, snapshot.high,
               snapshot.rawMinimum, snapshot.rawMaximum, snapshot.nonzeroCount,
               snapshot.clipCount);
    reportAt = now;
    frameCount = 0;
    worstGap = 0;
  }
}
