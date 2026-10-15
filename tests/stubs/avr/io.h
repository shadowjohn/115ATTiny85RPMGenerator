#pragma once
#include <stdint.h>

namespace sim {
struct PortRegister {
  uint8_t value = 0;
  operator uint8_t() const { return value; }
  PortRegister& operator=(uint8_t next);
  PortRegister& operator|=(unsigned int mask) { return *this = uint8_t(value | mask); }
  PortRegister& operator&=(unsigned int mask) { return *this = uint8_t(value & mask); }
  PortRegister& operator^=(unsigned int mask) { return *this = uint8_t(value ^ mask); }
};
struct FlagRegister {
  uint8_t value = 0;
  operator uint8_t() const { return value; }
  FlagRegister& operator=(uint8_t written) { value &= uint8_t(~written); return *this; }
  void raise(uint8_t flags) { value |= flags; }
  void clear(uint8_t flags) { value &= uint8_t(~flags); }
};
extern PortRegister portb;
extern FlagRegister tifr;
extern volatile uint8_t ddrb, tccr1, tcnt1, ocr1a, ocr1b, ocr1c, timsk, gtccr, pllcsr;
}
#define PORTB sim::portb
#define DDRB sim::ddrb
#define TCCR1 sim::tccr1
#define TCNT1 sim::tcnt1
#define OCR1A sim::ocr1a
#define OCR1B sim::ocr1b
#define OCR1C sim::ocr1c
#define TIMSK sim::timsk
#define TIFR sim::tifr
#define GTCCR sim::gtccr
#define PLLCSR sim::pllcsr
#define CS10 0
#define CS11 1
#define CS12 2
#define CS13 3
#define CTC1 7
#define PWM1A 6
#define PWM1B 6
#define OCIE1A 6
#define OCF1A 6
#define OCIE1B 5
#define OCF1B 5
#define COM1B1 5
#define COM1B0 4
#define TOIE1 2
#define TOV1 2
#define PCKE 2
#define PLLE 1
#define PSR1 1

#ifndef _BV
#define _BV(bit) (1U << (bit))
#endif
