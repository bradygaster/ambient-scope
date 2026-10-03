#pragma once

#include <Arduino.h>
#include <Wire.h>

namespace copilot {

class Es7210Capture {
 public:
  bool begin(TwoWire& wire, unsigned sampleRate);
  const char* error() const { return error_; }

 private:
  bool write(uint8_t reg, uint8_t value);
  bool read(uint8_t reg, uint8_t& value);
  bool update(uint8_t reg, uint8_t mask, uint8_t value);

  TwoWire* wire_ = nullptr;
  const char* error_ = nullptr;
};

}  // namespace copilot
