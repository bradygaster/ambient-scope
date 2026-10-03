#include "Es7210Capture.h"

namespace copilot {
namespace {

constexpr uint8_t kAddress = 0x40;

}

bool Es7210Capture::write(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(kAddress);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Es7210Capture::read(uint8_t reg, uint8_t& value) {
  wire_->beginTransmission(kAddress);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0 ||
      wire_->requestFrom(kAddress, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = wire_->read();
  return true;
}

bool Es7210Capture::update(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t current = 0;
  return read(reg, current) && write(reg, (current & ~mask) | (value & mask));
}

bool Es7210Capture::begin(TwoWire& wire, unsigned sampleRate) {
  wire_ = &wire;
  error_ = nullptr;
  if (sampleRate != 16000) {
    error_ = "ES7210 capture supports 16 kHz.";
    return false;
  }

  // MIC1/MIC2 are the two onboard microphones. ES7210 is an I2S slave;
  // GPIO42 supplies 4.096 MHz MCLK and SDOUT1 carries ADC1/ADC2 on GPIO10.
  if (!write(0x00, 0xff) || !write(0x00, 0x41) ||
      !write(0x01, 0x1f) || !write(0x09, 0x30) ||
      !write(0x0a, 0x30) || !write(0x40, 0xc3) ||
      !write(0x41, 0x70) || !write(0x42, 0x70) ||
      !write(0x02, 0xc1) || !write(0x07, 0x20) ||
      !write(0x04, 0x01) || !write(0x05, 0x00) ||
      !write(0x11, 0x60) || !write(0x12, 0x00) ||
      !write(0x4b, 0x00) || !write(0x4c, 0xff) ||
      !write(0x47, 0x00) || !write(0x48, 0x00) ||
      !write(0x49, 0xff) || !write(0x4a, 0xff) ||
      !update(0x43, 0x1f, 0x18) || !update(0x44, 0x1f, 0x18) ||
      !update(0x45, 0x1f, 0x00) || !update(0x46, 0x1f, 0x00) ||
      !write(0x01, 0x14) || !write(0x06, 0x00)) {
    error_ = "ES7210 register programming failed.";
    return false;
  }
  return true;
}

}  // namespace copilot
