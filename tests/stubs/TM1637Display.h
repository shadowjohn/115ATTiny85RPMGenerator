#pragma once

#include <stdint.h>

#define SEG_A 0x01
#define SEG_B 0x02
#define SEG_C 0x04
#define SEG_D 0x08
#define SEG_E 0x10
#define SEG_F 0x20
#define SEG_G 0x40

namespace sim {
void displayTransfer();
void rememberDisplay(int number, uint8_t dots);
void rememberSegments(const uint8_t segments[], uint8_t length, uint8_t position);
}

class TM1637Display {
 public:
  TM1637Display(uint8_t, uint8_t, unsigned int = 100) {}
  void setBrightness(uint8_t, bool = true) {}
  void clear() { sim::displayTransfer(); }
  void showNumberDec(int number, bool = false, uint8_t = 4, uint8_t = 0) {
    sim::rememberDisplay(number, 0);
    sim::displayTransfer();
  }
  void showNumberDecEx(int number, uint8_t dots, bool = false, uint8_t = 4, uint8_t = 0) {
    sim::rememberDisplay(number, dots);
    sim::displayTransfer();
  }
  void setSegments(const uint8_t segments[], uint8_t length = 4, uint8_t position = 0) {
    sim::rememberSegments(segments, length, position);
    sim::displayTransfer();
  }
};
