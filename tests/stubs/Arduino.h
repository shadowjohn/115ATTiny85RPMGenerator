#pragma once

#include <stdint.h>
#include <stdlib.h>

#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define PB0 0
#define PB1 1
#define PB2 2
#define PB3 3
#define PB4 4
#define PB5 5
#define _BV(bit) (1U << (bit))
#define F(value) value

typedef uint8_t byte;
typedef bool boolean;

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int analogRead(uint8_t pin);
unsigned long micros();
unsigned long millis();
void delay(unsigned long milliseconds);
void delayMicroseconds(unsigned int microseconds);

inline long map(long value, long fromLow, long fromHigh, long toLow, long toHigh) {
  return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}
template<class T> inline T constrain(T value, T low, T high) {
  return value < low ? low : (value > high ? high : value);
}
