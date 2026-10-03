#include "TouchInput.h"
#ifdef ARDUINO_ARCH_ESP32
#include <Arduino.h>
#include <Wire.h>
#include <TouchDrvCSTXXX.hpp>
#include <esp_timer.h>
#endif

namespace copilot {
namespace {
const char* error = nullptr;
#ifdef ARDUINO_ARCH_ESP32
TouchDrvCST92xx controller;
TouchGestureTracker tracker;
portMUX_TYPE touchLock = portMUX_INITIALIZER_UNLOCKED;
volatile bool pending = false;
bool initialized = false;
bool wasPressed = false;
int16_t lastX = 0;
int16_t lastY = 0;

void IRAM_ATTR interrupt() {
  portENTER_CRITICAL_ISR(&touchLock);
  pending = true;
  portEXIT_CRITICAL_ISR(&touchLock);
}
#endif
}

const char* touchInputError() { return error; }

bool initializeTouchInput() {
  error = nullptr;
#ifdef ARDUINO_ARCH_ESP32
  if (initialized) return true;
  controller.setPins(kTouchReset, kTouchInterrupt);
  if (!controller.begin(Wire, CST92XX_SLAVE_ADDRESS, kTouchSda, kTouchScl)) {
    error = "CST9217 touch controller initialization failed.";
    return false;
  }
  if (controller.getSupportTouchPoint() != 2) {
    error = "Unexpected touch controller point capacity.";
    return false;
  }
  controller.setMaxCoordinates(kDisplaySize - 1, kDisplaySize - 1);
  controller.setMirrorXY(true, true);
  Wire.setTimeOut(8);
  pinMode(kTouchInterrupt, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kTouchInterrupt), interrupt, FALLING);
  initialized = true;
  return true;
#else
  error = "Physical touch input requires the ESP32 device.";
  return false;
#endif
}

bool pollTouchInput(TouchGesture& gesture, TouchState& state) {
  gesture = {};
  state = {};
#ifdef ARDUINO_ARCH_ESP32
  if (!initialized) {
    error = "Touch input was not initialized.";
    return false;
  }
  portENTER_CRITICAL(&touchLock);
  const bool ready = pending;
  pending = false;
  portEXIT_CRITICAL(&touchLock);
  if (!ready && !tracker.active() && !wasPressed) return false;
  int16_t x[2] = {}, y[2] = {};
  const uint8_t count = controller.getPoint(x, y, 2);
  const bool pressed = count != 0;
  if (pressed) {
    lastX = x[0];
    lastY = y[0];
  }
  state = {
      pressed, pressed && !wasPressed, !pressed && wasPressed,
      pressed ? x[0] : lastX, pressed ? y[0] : lastY};
  wasPressed = pressed;
  tracker.sample(pressed, state.x, state.y, esp_timer_get_time() / 1000, gesture);
  return state.pressed || state.justReleased || gesture.kind != TouchGestureKind::None;
#else
  (void)state;
  (void)gesture;
  error = "Physical touch input requires the ESP32 device.";
  return false;
#endif
}

bool pollTouchGesture(TouchGesture& gesture) {
  TouchState state;
  return pollTouchInput(gesture, state) && gesture.kind != TouchGestureKind::None;
}
}
