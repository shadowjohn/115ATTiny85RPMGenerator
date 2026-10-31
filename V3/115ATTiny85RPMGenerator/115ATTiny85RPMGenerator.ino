/*
 * ATtiny85 轉速訊號產生器 RPM Generator V3
 * Mode 1: original 500 RPM steps; 2: smooth VR; 3: automatic sweep.
 * Verified core: damellis attiny 1.0.2 + Arduino AVR 1.8.6
 * MCU: ATtiny85 (Internal 16 MHz); TM1637: avishorp 1.2.0
 */

#include <Arduino.h>
#include <TM1637Display.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#if !defined(__AVR_ATtiny85__) || F_CPU != 16000000UL
#error "Select ATtiny85 / Internal 16 MHz; this Timer1 configuration requires 16 MHz."
#endif

#define CLK PB2
#define DIO PB1
#define VR_PIN PB3
#define OUT_PIN PB4
#define BUTTON_PIN PB0

TM1637Display display(CLK, DIO);

const int rpm_steps[] = {
  0,0,500,1000,1500,2000,2500,3000,3500,4000,4500,5000,
  5500,6000,6500,7000,7500,8000,8500,9000,9500,10000,
  10500,11000,11500,12000,12500,13000,13500,14000,14000
};
const uint8_t rpm_steps_count = sizeof(rpm_steps) / sizeof(rpm_steps[0]);

const unsigned long VR_READ_INTERVAL_MS = 10;
const unsigned long DISPLAY_UPDATE_INTERVAL_MS = 50;
const int VR_HYSTERESIS = 4; // ADC counts, smaller than a ~34-count step.
const uint32_t BUTTON_DEBOUNCE_MS = 30;
const uint32_t AUTO_STEP_INTERVAL_MS = 1000;
const uint32_t AUTO_ENDPOINT_HOLD_MS = 2000;
const int AUTO_RPM_STEP = 1000;
const int AUTO_MAX_RPM = 14000;

// User-approved fixed x10 RPM: displayed value x 10 = requested RPM.
// Label the display/panel x10 RPM (1400 = 14000 RPM); output timing uses real RPM.
const int DISPLAY_RPM_DIVISOR = 10;

unsigned long lastVRReadTime = 0;
unsigned long lastDisplayUpdate = 0;
int rpm = 0; // Main-loop state only. The ISR reads the atomic timing pair below.
int vrRPM = 0;
enum GeneratorMode : uint8_t { MODE_STEPS = 1, MODE_SMOOTH = 2, MODE_AUTO = 3 };
GeneratorMode mode = MODE_STEPS;
int filteredVR = 0;
const uint16_t MODE_DISPLAY_MS = 800;
const int SMOOTH_ENDPOINT_ADC = 8; // Small dead zones at both ends keep zero/full-scale easy to select.
bool modeDisplayActive = true;
uint32_t modeDisplayStarted = 0;
bool autoInitialZero = true;
int8_t autoDirection = -1;
uint32_t lastAutoStepTime = 0;
uint8_t buttonLastReading = HIGH;
uint8_t buttonStableState = HIGH;
uint32_t buttonLastChangeTime = 0;

volatile uint32_t pendingHighTicks = 0;
volatile uint32_t pendingLowTicks = 0;
// ISR state, initialized only with interrupts disabled when starting/stopping.
volatile uint32_t activeHighTicks = 0;
volatile uint32_t activeLowTicks = 0;
volatile uint32_t remainingTicks = 0;
volatile bool isHigh = false;
bool pulseRunning = false;

// Timer1 CK/64 = 250 kHz = 4 us/tick. Long phases span several comparisons.
// Reserve >=64 ticks (256 us) for every ISR deadline, including the final chunk.
void scheduleNextChunk() {
  uint32_t chunk = remainingTicks;
  if (chunk > 250) {
    chunk = remainingTicks - 64;
    if (chunk > 250) chunk = 250;
  }
  remainingTicks -= chunk;
  OCR1A = uint8_t(OCR1A + uint8_t(chunk));
}

ISR(TIMER1_COMPA_vect) {
  if (remainingTicks != 0) {
    scheduleNextChunk();
    return;
  }

  if (isHigh) {
    PORTB &= ~_BV(OUT_PIN);
    isHigh = false;
    remainingTicks = activeLowTicks;
  } else {
    // The rising edge starts a complete cycle. Never mix old/new high/low times.
    activeHighTicks = pendingHighTicks;
    activeLowTicks = pendingLowTicks;
    PORTB |= _BV(OUT_PIN);
    isHigh = true;
    remainingTicks = activeHighTicks;
  }
  scheduleNextChunk();
}

void setup() {
  pinMode(OUT_PIN, OUTPUT);
  pinMode(VR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(OUT_PIN, LOW);
  // 開機已按住時先等待放開，避免把上電當成一次按下。
  buttonLastReading = uint8_t(digitalRead(BUTTON_PIN));
  buttonStableState = buttonLastReading;
  buttonLastChangeTime = millis();

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    TCCR1 = 0;
    // The damellis variant enables PWM1B during initVariant(). Disconnect both
    // Timer1 hardware outputs so PB1 (display) and PB4 stay ordinary GPIO.
    GTCCR &= ~(_BV(PWM1B) | _BV(COM1B1) | _BV(COM1B0));
    // Internal 16 MHz uses the PLL for the CPU. Keep PLLE; only select CK for T1.
    PLLCSR &= ~_BV(PCKE);
    TIMSK &= ~(_BV(OCIE1A) | _BV(OCIE1B) | _BV(TOIE1));
    OCR1C = 255;
    TCNT1 = 0;
    // Write-one-to-clear: never use |= on TIFR (Timer0 flags share this register).
    TIFR = _BV(OCF1A) | _BV(OCF1B) | _BV(TOV1);
  }

  display.setBrightness(0x0f);
  display.clear();
  modeDisplayStarted = millis();
}

int smoothVR(int current) {
  static int16_t filteredQ4 = 0;
  static bool initialized = false;
  const int16_t targetQ4 = current * 16;
  if (!initialized) {
    filteredQ4 = targetQ4;
    initialized = true;
  } else {
    filteredQ4 += (targetQ4 - filteredQ4) / 2;
  }
  return (filteredQ4 + 8) / 16;
}

uint8_t selectVRStep(int value) {
  static uint8_t index = 0;
  static bool initialized = false;
  if (!initialized) {
    index = (uint32_t(value) * (rpm_steps_count - 1)) / 1023;
    initialized = true;
  }

  // ceil(index * 1023 / 30) reproduces the original map() boundaries.
  // Walk multiple steps in one reading so a quick turn does not slew one by one.
  while (index < rpm_steps_count - 1) {
    const int boundary = (uint32_t(index + 1) * 1023 + rpm_steps_count - 2)
                         / (rpm_steps_count - 1);
    if (value < boundary + VR_HYSTERESIS) break;
    ++index;
  }
  while (index > 0) {
    const int boundary = (uint32_t(index) * 1023 + rpm_steps_count - 2)
                         / (rpm_steps_count - 1);
    if (value >= boundary - VR_HYSTERESIS) break;
    --index;
  }
  return index;
}

// ATtiny85 ADC is 0..1023 (1024 levels). Keep small endpoint plateaus;
// the interior is rounded to individual RPM, without 50/500-RPM quantization.
int smoothRPMForADC(int adc) {
  adc = constrain(adc, 0, 1023);
  if (adc <= SMOOTH_ENDPOINT_ADC) return 0;
  if (adc >= 1023 - SMOOTH_ENDPOINT_ADC) return 14000;
  const uint16_t span = 1023 - 2 * SMOOTH_ENDPOINT_ADC;
  return (uint32_t(adc - SMOOTH_ENDPOINT_ADC) * 14000UL + span / 2) / span;
}

void updateRPM(int newRPM) {
  if (newRPM <= 0) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      TIMSK &= ~_BV(OCIE1A);
      TCCR1 = 0;
      PORTB &= ~_BV(OUT_PIN);
      pendingHighTicks = 0;
      pendingLowTicks = 0;
      remainingTicks = 0;
      isHigh = false;
      pulseRunning = false;
      TIFR = _BV(OCF1A);
    }
    rpm = 0;
    return;
  }

  newRPM = constrain(newRPM, 1, 14000);
  // A very slow cycle can last seconds. On a rapid rise back into riding RPM,
  // restart once rather than holding the old low-RPM HIGH for several seconds.
  // Only this transition may truncate an old cycle; steady cycles stay paired.
  if (pulseRunning && rpm < 500 && newRPM >= 500) updateRPM(0);
  if (newRPM == rpm) return; // Repeated ADC readings must not restart the phase.

  // Round once, then split that same period: 330/360 HIGH and 30/360 LOW.
  const uint32_t periodTicks = (15000000UL + newRPM / 2) / newRPM;
  const uint32_t highTicks = (uint32_t(periodTicks) * 11 + 6) / 12;
  const uint32_t lowTicks = periodTicks - highTicks;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    pendingHighTicks = highTicks;
    pendingLowTicks = lowTicks;
    if (!pulseRunning) {
      TCCR1 = 0;
      TIMSK &= ~_BV(OCIE1A);
      TCNT1 = 0;
      GTCCR |= _BV(PSR1); // Reset only Timer1 prescaler; leave Timer0 running.
      OCR1A = 0;
      activeHighTicks = highTicks;
      activeLowTicks = lowTicks;
      isHigh = false;
      PORTB &= ~_BV(OUT_PIN);
      remainingTicks = lowTicks;
      scheduleNextChunk();
      TIFR = _BV(OCF1A) | _BV(OCF1B) | _BV(TOV1);
      pulseRunning = true;
      TIMSK |= _BV(OCIE1A);
      TCCR1 = _BV(CS12) | _BV(CS11) | _BV(CS10); // CK/64, normal mode.
    }
  }
  rpm = newRPM;
}

void updateDisplay() {
  const uint32_t now = millis();
  static int lastDisplayedNumber = -1;
  if (modeDisplayActive && uint32_t(now - modeDisplayStarted) >= MODE_DISPLAY_MS)
    modeDisplayActive = false;
  if (uint32_t(now - lastDisplayUpdate) >= DISPLAY_UPDATE_INTERVAL_MS) {
    const int number = modeDisplayActive ? int(mode) : rpm / DISPLAY_RPM_DIVISOR;
    if (number != lastDisplayedNumber) {
      display.showNumberDec(number);
      lastDisplayedNumber = number;
    }
    lastDisplayUpdate = now;
  }
}

bool buttonPressed(uint32_t now) {
  const uint8_t reading = uint8_t(digitalRead(BUTTON_PIN));
  if (reading != buttonLastReading) {
    buttonLastReading = reading;
    buttonLastChangeTime = now;
  }
  if (reading != buttonStableState &&
      uint32_t(now - buttonLastChangeTime) >= BUTTON_DEBOUNCE_MS) {
    buttonStableState = reading;
    return reading == LOW;
  }
  return false;
}

void updateAutoRPM(uint32_t now) {
  const uint32_t interval = (rpm == 0 || rpm == AUTO_MAX_RPM)
                             ? AUTO_ENDPOINT_HOLD_MS : AUTO_STEP_INTERVAL_MS;
  if (uint32_t(now - lastAutoStepTime) < interval) return;

  // 每格從實際套用時刻計時，主迴圈延後時也不會略過中間階梯。
  lastAutoStepTime = now;
  if (autoInitialZero) {
    autoInitialZero = false;
    updateRPM(AUTO_MAX_RPM);
  } else {
    const int nextRPM = rpm + autoDirection * AUTO_RPM_STEP;
    updateRPM(nextRPM);
    if (nextRPM == 0) autoDirection = 1;
    else if (nextRPM == AUTO_MAX_RPM) autoDirection = -1;
  }
}

void loop() {
  const uint32_t now = millis();
  if (uint32_t(now - lastVRReadTime) >= VR_READ_INTERVAL_MS) {
    filteredVR = smoothVR(analogRead(VR_PIN));
    vrRPM = rpm_steps[selectVRStep(filteredVR)];
    if (mode == MODE_STEPS) updateRPM(vrRPM);
    else if (mode == MODE_SMOOTH) updateRPM(smoothRPMForADC(filteredVR));
    lastVRReadTime = now;
  }
  if (buttonPressed(now)) {
    mode = mode == MODE_AUTO ? MODE_STEPS : GeneratorMode(uint8_t(mode) + 1);
    modeDisplayActive = true;
    modeDisplayStarted = now;
    lastDisplayUpdate = uint32_t(now - DISPLAY_UPDATE_INTERVAL_MS);
    if (mode == MODE_AUTO) {
      autoInitialZero = true;
      autoDirection = -1;
      lastAutoStepTime = now;
      updateRPM(0);
    } else {
      updateRPM(mode == MODE_STEPS ? vrRPM : smoothRPMForADC(filteredVR));
    }
  }
  if (mode == MODE_AUTO) updateAutoRPM(now);
  updateDisplay();
}
