/*
 * ATTiny85 轉速訊號產生器 RPM Generator
 * MCU: ATTiny85 (Internal 16 MHz)
 * VR控制RPM、PWM模擬激磁、TM1637顯示RPM
 * TM1637 要用 https://github.com/avishorp/TM1637
 */

#include <Arduino.h>
#include <TM1637Display.h>

#define CLK PB2
#define DIO PB1
#define VR_PIN PB3
#define OUT_PIN PB4

TM1637Display display(CLK, DIO);

const int rpm_steps[] = {
  0,0,500,1000,1500,2000,2500,3000,3500,4000,4500,5000,
  5500,6000,6500,7000,7500,8000,8500,9000,9500,10000,
  10500,11000,11500,12000,12500,13000,13500,14000,14000
};
const int rpm_steps_count = sizeof(rpm_steps) / sizeof(rpm_steps[0]);

const unsigned long VR_READ_INTERVAL_MS = 100;  // 原本是 300，現在改快
const unsigned long DISPLAY_UPDATE_INTERVAL_MS = 50;
const int RPM_DISPLAY_THRESHOLD = 50;

unsigned long lastVRReadTime = 0;
unsigned long lastDisplayUpdate = 0;

volatile int rpm = 0;

volatile unsigned long periodUs = 0;
volatile unsigned long highTimeUs = 0;
volatile unsigned long lowTimeUs = 0;

volatile bool isHigh = false;
volatile unsigned long lastToggleTime = 0;

float dwellRatio = 330.0f / 360.0f;  // 模擬凸台角度（30度），你可以改成 12/360、30/360

void setup() {
  pinMode(OUT_PIN, OUTPUT);
  pinMode(VR_PIN, INPUT);
  digitalWrite(OUT_PIN, LOW);

  display.setBrightness(0x0f);
  display.clear();

  isHigh = false;
  lastToggleTime = micros();
}
int smoothVR(int current) {
  static int vrFiltered = 0;
  vrFiltered = (vrFiltered * 3 + current) / 4; // 簡單加權平均
  return vrFiltered;
}
int readAverageVR(int samples = 8) {
  long sum = 0;
  analogRead(VR_PIN); // 丟棄第一次，讓 ADC 穩定
  for (int i = 0; i < samples; i++) {
    sum += analogRead(VR_PIN);
    delayMicroseconds(200);  // 比 delay(1) 好
  }
  return sum / samples;
}

void updateRPM(int newRPM) {
  if (newRPM <= 0) {
    rpm = 0;
    periodUs = 0;
    highTimeUs = 0;
    lowTimeUs = 0;
    digitalWrite(OUT_PIN, LOW);
    isHigh = false;
    return;       
  }

  rpm = newRPM;
  float freq = rpm / 60.0f;  // 轉成 Hz
  periodUs = (unsigned long)(1e6 / freq);
  highTimeUs = (unsigned long)(periodUs * dwellRatio);
  lowTimeUs = periodUs - highTimeUs;
}

void handleTriggerOutput() {
  if (rpm <= 0) {
    digitalWrite(OUT_PIN, LOW);
    isHigh = false;
    return;
  }

  unsigned long now = micros();
  unsigned long elapsed = now - lastToggleTime;

  if (isHigh) {
    if (elapsed >= highTimeUs) {
      digitalWrite(OUT_PIN, LOW);
      isHigh = false;
      lastToggleTime = now;
    }
  } else {
    if (elapsed >= lowTimeUs) {
      digitalWrite(OUT_PIN, HIGH);
      isHigh = true;
      lastToggleTime = now;
    }
  }
}

void updateDisplay() {
  unsigned long now = millis();
  static int lastDisplayedRPM = -1;

  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL_MS) {
    if (abs(rpm - lastDisplayedRPM) >= RPM_DISPLAY_THRESHOLD || lastDisplayedRPM == -1) {
      display.showNumberDec(rpm);
      lastDisplayedRPM = rpm;
    }
    lastDisplayUpdate = now;
  }
}

void loop() {
  unsigned long now = millis();

  // 讀 VR 並更新 RPM
  if (now - lastVRReadTime >= VR_READ_INTERVAL_MS) {
    int vrVal = smoothVR(analogRead(VR_PIN));
    int idx = map(vrVal, 0, 1023, 0, rpm_steps_count - 1);
    idx = constrain(idx, 0, rpm_steps_count - 1);
    updateRPM(rpm_steps[idx]);
    lastVRReadTime = now;
  }

  handleTriggerOutput();
  updateDisplay();
}
