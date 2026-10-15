// Host simulator for the real sketch. Each named case runs in a fresh process.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <Arduino.h>
#include <avr/interrupt.h>

#ifdef HOST_HAS_TIMER1_ISR
void TIMER1_COMPA_vect();
#endif

namespace sim {
struct Edge { uint64_t at; bool high; };
uint64_t nowUs = 0;
int adc = 0;
unsigned displayBlockUs = 20800;
unsigned displayTransfers = 0;
int displayedNumber = -1;
uint8_t displayedDots = 0;
uint8_t displayedSegments[4] = {};
bool usedSegments = false;
bool interruptsEnabled = true;
PortRegister portb;
FlagRegister tifr;
volatile uint8_t ddrb = 0, tccr1 = 0, tcnt1 = 0, ocr1a = 0, ocr1b = 0;
volatile uint8_t ocr1c = 255, timsk = 0, gtccr = 0, pllcsr = 0;
std::vector<Edge> edges;
struct Transfer { uint64_t start, end; };
std::vector<Transfer> transfers;
PortRegister& PortRegister::operator=(uint8_t next) {
  if ((value ^ next) & _BV(PB4)) edges.push_back({nowUs, bool(next & _BV(PB4))});
  value = next;
  return *this;
}
void serviceInterrupts() {
  if (interruptsEnabled && (timsk & _BV(OCIE1A)) && (uint8_t(tifr) & _BV(OCF1A))) {
    tifr.clear(_BV(OCF1A));
#ifdef HOST_HAS_TIMER1_ISR
    interruptsEnabled = false; // AVR masks nested interrupts on ISR entry.
    TIMER1_COMPA_vect();
    interruptsEnabled = true;
#endif
  }
}
void advance(uint64_t microseconds) {
  const uint64_t end = nowUs + microseconds;
  while (nowUs < end) {
    ++nowUs;
    serviceInterrupts();
    if ((tccr1 & 15) == 7 && (nowUs % 4) == 0) {
      tcnt1 = uint8_t(tcnt1 + 1);
      if (tcnt1 == ocr1a) {
        tifr.raise(_BV(OCF1A));
        serviceInterrupts();
      }
    }
  }
}
void displayTransfer() {
  ++displayTransfers;
  const uint64_t start = nowUs;
  advance(displayBlockUs);
  transfers.push_back({start, nowUs});
}
void rememberDisplay(int number, uint8_t dots) {
  displayedNumber = number;
  displayedDots = dots;
  usedSegments = false;
}
void rememberSegments(const uint8_t segments[], uint8_t length, uint8_t position) {
  usedSegments = true;
  for (uint8_t i = 0; i < length && position + i < 4; ++i) displayedSegments[position + i] = segments[i];
}
}

void pinMode(uint8_t pin, uint8_t mode) {
  if (mode == OUTPUT) sim::ddrb |= uint8_t(_BV(pin));
  else sim::ddrb &= uint8_t(~_BV(pin));
}
void digitalWrite(uint8_t pin, uint8_t value) {
  if (value) PORTB |= uint8_t(_BV(pin)); else PORTB &= uint8_t(~_BV(pin));
}
int analogRead(uint8_t) { sim::advance(104); return sim::adc; }
unsigned long micros() { return static_cast<uint32_t>(sim::nowUs); }
unsigned long millis() { return static_cast<uint32_t>(sim::nowUs / 1000); }
void delay(unsigned long milliseconds) { sim::advance(uint64_t(milliseconds) * 1000); }
void delayMicroseconds(unsigned int microseconds) { sim::advance(microseconds); }

#include "../115ATTiny85RPMGenerator.ino"

static void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
static void closeTo(double actual, double expected, const char* message) {
  if (std::abs(actual - expected) > 4.01) {
    std::printf("  actual %.3f us; expected %.3f us (tolerance 4 us)\n", actual, expected);
    throw std::runtime_error(message);
  }
}
static std::vector<uint64_t> risingEdges() {
  std::vector<uint64_t> values;
  for (const auto& edge: sim::edges) if (edge.high) values.push_back(edge.at);
  return values;
}
static void foregroundFor(uint64_t duration, bool holdRPM = false, bool suppressDisplay = false) {
  const uint64_t end = sim::nowUs + duration;
  while (sim::nowUs < end) {
    if (holdRPM) lastVRReadTime = millis(); // Isolate display/output from VR mapping.
    if (suppressDisplay) lastDisplayUpdate = millis();
    loop();
    sim::advance(4);
  }
}
static void untilEdges(size_t count) {
  const uint64_t deadline = sim::nowUs + 300000;
  while (sim::edges.size() < count && sim::nowUs < deadline) foregroundFor(4, true, true);
  require(sim::edges.size() >= count, "output edge deadline exceeded");
}

static void vrResponse() {
  setup();
  sim::adc = 0;
  foregroundFor(30000);
  const uint64_t start = sim::nowUs;
  sim::adc = 1023;
  while (sim::nowUs - start < 150000 && rpm < 12600) foregroundFor(4);
  const uint64_t elapsed = sim::nowUs - start;
  std::printf("VR 0 -> full-scale: observed %d RPM after %llu us (90%% target <=150000 us)\n",
              int(rpm), static_cast<unsigned long long>(elapsed));
  require(rpm >= 12600 && elapsed <= 150000, "VR response is slower than the 150 ms target");
}

static void timerConfiguration() {
  PLLCSR = _BV(PCKE) | _BV(PLLE);
  GTCCR = _BV(PWM1B) | _BV(COM1B1) | _BV(COM1B0);
  TIMSK = _BV(OCIE1A) | _BV(OCIE1B) | _BV(TOIE1) | _BV(1);
  sim::tifr.raise(_BV(OCF1A) | _BV(OCF1B) | _BV(TOV1) | _BV(1));
  setup();
  require(DDRB & _BV(PB4), "PB4 must be configured as an output");
  require(!(PLLCSR & _BV(PCKE)), "Timer1 must use CK, not PCK");
  require(PLLCSR & _BV(PLLE), "CPU PLL enable must be preserved");
  require(!(GTCCR & (_BV(PWM1B) | _BV(COM1B1) | _BV(COM1B0))), "Timer1 hardware output B was not disconnected");
  require(OCR1C == 255, "Timer1 normal-mode TOP must be 255");
  require((TIMSK & (_BV(OCIE1A) | _BV(OCIE1B) | _BV(TOIE1))) == 0, "Timer1 interrupts must begin disabled");
  require(TIMSK & _BV(1), "setup changed unrelated Timer0 interrupt bit");
  require(uint8_t(TIFR) & _BV(1), "setup cleared unrelated Timer0 pending flag");
  require(!(uint8_t(TIFR) & (_BV(OCF1A) | _BV(OCF1B) | _BV(TOV1))), "setup left stale Timer1 flags");
  updateRPM(14000);
  require(TCCR1 == 7, "Timer1 must be normal mode at CK/64");
  require(TIMSK & _BV(OCIE1A), "Timer1 compare A interrupt not enabled");
  require(uint8_t(TIFR) & _BV(1), "start cleared unrelated Timer0 pending flag");
  updateRPM(0);
  require(uint8_t(TIFR) & _BV(1), "stop cleared unrelated Timer0 pending flag");
  std::printf("Timer1 GPIO, normal mode, CK/64, TOP and PLL/Timer0 preservation checks passed\n");
}

static void displayRange() {
  setup();
  // Independent fixtures catch a regression of the default unit or a switch at 10000.
  const struct { int requestedRPM; int displayed; } fixtures[] = {
    {0, 0}, {500, 50}, {9500, 950}, {10000, 1000}, {10500, 1050}, {14000, 1400}
  };
  for (const auto& fixture: fixtures) {
    updateRPM(fixture.requestedRPM);
    foregroundFor(60000, true);
    if (sim::usedSegments || sim::displayedNumber != fixture.displayed) {
      std::printf("  %d RPM: displayed %d (segment-only %d), expected %d x10 RPM\n",
                  fixture.requestedRPM, sim::displayedNumber, int(sim::usedSegments), fixture.displayed);
      throw std::runtime_error("display must use fixed x10 RPM across the full range");
    }
    require(rpm == fixture.requestedRPM, "display scaling changed the requested output RPM");
  }
  std::printf("Fixed x10 RPM display: 0->0, 500->50, 9500->950, 10000->1000, 10500->1050, 14000->1400; output RPM preserved\n");
}
static void displayBlocking(int target) {
  setup();
  updateRPM(target);
  if (target == 500) {
    // Force the first actual updateDisplay() transfer to straddle a falling edge.
    foregroundFor(110000, true, true);
    lastDisplayUpdate = millis() - DISPLAY_UPDATE_INTERVAL_MS;
  }
  foregroundFor(450000, true);
  auto rises = risingEdges();
  require(rises.size() >= 3, "too few rising edges");
  const double expected = 60000000.0 / target;
  double worstError = 0;
  for (size_t i = 1; i < rises.size(); ++i) {
    worstError = std::max(worstError, std::abs(double(rises[i] - rises[i - 1]) - expected));
  }
  std::printf("%d RPM with %u us TM1637 blocking: %zu rises; worst period error %.3f us\n",
              target, sim::displayBlockUs, rises.size(), worstError);
  require(sim::displayTransfers > 1, "actual updateDisplay() transfer was not exercised");
  require(worstError <= 4.01, "display transfer delayed or lost output edges");
  bool edgeDuringTransfer = false;
  for (const auto& edge: sim::edges) {
    for (const auto& transfer: sim::transfers) {
      if (edge.at > transfer.start && edge.at < transfer.end) edgeDuringTransfer = true;
    }
  }
  require(edgeDuringTransfer, "test did not observe output continuing inside a display transfer");
  for (size_t i = 1; i < sim::edges.size(); ++i) {
    const auto& previous = sim::edges[i - 1];
    const auto& edge = sim::edges[i];
    require(previous.high != edge.high, "output edges do not alternate");
    closeTo(double(edge.at - previous.at), expected * (previous.high ? 11.0 / 12.0 : 1.0 / 12.0),
            "wrong high/low duration during display transfer");
  }
}

static void allSteps() {
  setup();
  double worstPeriod = 0, worstHigh = 0, worstLow = 0;
  for (int target = 500; target <= 14000; target += 500) {
    updateRPM(target);
    foregroundFor(250000, true);
    sim::edges.clear();
    const double expected = 60000000.0 / target;
    foregroundFor(static_cast<uint64_t>(expected * 4 + 5000), true);
    const auto rises = risingEdges();
    require(rises.size() >= 3, "too few edges in full-step sweep");
    for (size_t i = 1; i < rises.size(); ++i) {
      const double error = std::abs(double(rises[i] - rises[i - 1]) - expected);
      worstPeriod = std::max(worstPeriod, error);
      if (error > 4.01) std::printf("  failed step %d RPM\n", target);
      require(error <= 4.01, "step sweep period outside one timer tick");
    }
    for (size_t i = 1; i < sim::edges.size(); ++i) {
      const auto& previous = sim::edges[i - 1];
      const auto& edge = sim::edges[i];
      require(previous.high != edge.high, "step sweep edges do not alternate");
      const double wanted = expected * (previous.high ? 11.0 / 12.0 : 1.0 / 12.0);
      const double error = std::abs(double(edge.at - previous.at) - wanted);
      if (previous.high) worstHigh = std::max(worstHigh, error);
      else worstLow = std::max(worstLow, error);
      closeTo(double(edge.at - previous.at), wanted, "step sweep high/low ratio outside one timer tick");
    }
  }
  std::printf("28 steps 500..14000 RPM: worst period/high/low errors %.3f / %.3f / %.3f us\n",
              worstPeriod, worstHigh, worstLow);
}

static void stopAndRestart() {
  setup();
  updateRPM(0);
  sim::edges.clear();
  foregroundFor(150000, true);
  require(sim::edges.empty() && !(uint8_t(PORTB) & _BV(PB4)), "0 RPM must stay LOW without edges");
  updateRPM(14000);
  untilEdges(1);
  require(sim::edges[0].high, "start must begin with a rising edge after LOW");
  updateRPM(0); // Stop while high: immediate LOW is the documented exception to full cycles.
  require(!(uint8_t(PORTB) & _BV(PB4)), "stop during HIGH did not immediately drive LOW");
  sim::edges.clear();
  foregroundFor(150000, true);
  require(sim::edges.empty(), "timer produced edges while stopped");
  const uint64_t restart = sim::nowUs;
  updateRPM(500);
  require(!(uint8_t(PORTB) & _BV(PB4)), "restart did not begin LOW");
  untilEdges(1);
  closeTo(double(sim::edges.front().at - restart), 10000.0, "restart LOW segment was incomplete");
  updateRPM(-1);
  require(rpm == 0 && !(uint8_t(PORTB) & _BV(PB4)), "negative RPM must stop LOW");
  std::printf("0 RPM and negative RPM stay LOW; restart begins with a complete 10000 us LOW\n");
}

static void repeatedRPM() {
  setup();
  updateRPM(14000);
  const uint64_t deadline = sim::nowUs + 150000;
  while (sim::nowUs < deadline) {
    updateRPM(14000);
    foregroundFor(4, true, true);
  }
  const auto rises = risingEdges();
  require(rises.size() > 20, "repeated same-RPM requests restarted or stalled output");
  for (size_t i = 1; i < rises.size(); ++i)
    closeTo(double(rises[i] - rises[i - 1]), 60000000.0 / 14000, "same RPM request changed phase");
  std::printf("Repeated same-RPM calls: %zu continuous rising edges\n", rises.size());
}

static void boundaryUpdate(bool duringLow, bool decreasing) {
  setup();
  const int oldRPM = decreasing ? 14000 : 500;
  const int newRPM = decreasing ? 500 : 14000;
  const double oldPeriod = 60000000.0 / oldRPM;
  const double newPeriod = 60000000.0 / newRPM;
  updateRPM(oldRPM);
  untilEdges(duringLow ? 2 : 1);
  const uint64_t firstRise = sim::edges[0].at;
  foregroundFor(duringLow ? 80 : 100, true, true);
  updateRPM(newRPM);
  untilEdges(5);
  closeTo(double(sim::edges[1].at - firstRise), oldPeriod * 11 / 12, "pending RPM truncated active HIGH");
  closeTo(double(sim::edges[2].at - sim::edges[1].at), oldPeriod / 12, "pending RPM replaced active LOW");
  closeTo(double(sim::edges[3].at - sim::edges[2].at), newPeriod * 11 / 12, "new HIGH not applied at rising boundary");
  closeTo(double(sim::edges[4].at - sim::edges[3].at), newPeriod / 12, "new HIGH/LOW were not a matching pair");
  std::printf("%d -> %d RPM update during %s preserves old cycle and applies a complete new pair\n",
              oldRPM, newRPM, duringLow ? "LOW" : "HIGH");
}

static void filterResponse() {
  require(smoothVR(0) == 0, "filter did not initialize from first sample");
  int filtered = 0;
  for (int i = 0; i < 4; ++i) filtered = smoothVR(1023);
  std::printf("Filter 0 -> 1023 after four samples: %d (required >= 921)\n", filtered);
  require(filtered >= 921, "filter takes more than four samples to reach 90 percent");
}

static void monotonicVR() {
  setup();
  int previous = 0;
  for (int value = 0; value <= 1023; value += 11) {
    sim::adc = value;
    const uint64_t deadline = sim::nowUs + 20000;
    while (sim::nowUs < deadline) {
      loop();
      require(rpm >= previous, "increasing VR caused RPM to move backwards");
      previous = rpm;
      sim::advance(4);
    }
  }
  sim::adc = 1023;
  foregroundFor(150000);
  require(rpm == 14000, "full-scale VR does not reach the last RPM step");
  const auto rises = risingEdges();
  double worstPeriodIncrease = 0;
  for (size_t i = 2; i < rises.size(); ++i) {
    const double previousPeriod = double(rises[i - 1] - rises[i - 2]);
    const double period = double(rises[i] - rises[i - 1]);
    worstPeriodIncrease = std::max(worstPeriodIncrease, period - previousPeriod);
  }
  require(worstPeriodIncrease <= 4.01, "increasing VR lengthened an output period (apparent RPM dropped)");
  sim::adc = 0;
  foregroundFor(150000);
  require(rpm == 0, "zero VR does not return to 0 RPM");
  std::printf("VR sweep stays monotonic in RPM and pulse periods (worst increase %.3f us); both endpoints reachable\n",
              worstPeriodIncrease);
}

static void hysteresis() {
#ifdef HOST_HAS_SELECT_VR_STEP
  for (int step = 1; step < rpm_steps_count - 1; ++step) {
    selectVRStep(0);
    const int threshold = (step * 1023 + (rpm_steps_count - 2)) / (rpm_steps_count - 1);
    const int below = selectVRStep(threshold - 8);
    require(below == step - 1, "unexpected lower step at threshold");
    for (int i = 0; i < 100; ++i)
      require(selectVRStep(threshold + ((i & 1) ? -3 : 3)) == below, "+/-3 ADC noise crossed the lower hysteresis band");
    const int above = selectVRStep(threshold + 8);
    require(above == step, "stable VR increase did not advance the step");
    for (int i = 0; i < 100; ++i)
      require(selectVRStep(threshold + ((i & 1) ? -3 : 3)) == above, "+/-3 ADC noise crossed the upper hysteresis band");
  }
  std::printf("29 reachable thresholds: +/-3 ADC noise holds either selected step\n");
#else
  throw std::runtime_error("sketch has no selectVRStep hysteresis selector");
#endif
}

static void millisWrap() {
  sim::nowUs = uint64_t(UINT32_MAX - 30) * 1000;
  setup();
  sim::adc = 0;
  foregroundFor(5000);
  sim::adc = 1023;
  foregroundFor(150000);
  require(millis() < 1000, "test did not cross the 32-bit millis wrap");
  require(rpm >= 12600, "VR sampling stalled at millis wrap");
  updateRPM(14000);
  foregroundFor(150000, true);
  sim::edges.clear();
  foregroundFor(50000, true);
  const auto rises = risingEdges();
  require(rises.size() >= 5, "pulse output stalled after millis wrap");
  for (size_t i = 1; i < rises.size(); ++i)
    closeTo(double(rises[i] - rises[i - 1]), 60000000.0 / 14000, "pulse period changed after millis wrap");
  std::printf("32-bit millis wrap: VR continues sampling and 14000 RPM timing remains valid\n");
}

int main(int argc, char** argv) {
  if (argc != 2) { std::fprintf(stderr, "Pass one test name.\n"); return 2; }
  const std::string test = argv[1];
  try {
    if (test == "vr_response") vrResponse();
    else if (test == "display_500") displayBlocking(500);
    else if (test == "display_14000") displayBlocking(14000);
    else if (test == "all_steps") allSteps();
    else if (test == "stop_restart") stopAndRestart();
    else if (test == "same_rpm") repeatedRPM();
    else if (test == "boundary_high") boundaryUpdate(false, false);
    else if (test == "boundary_low") boundaryUpdate(true, false);
    else if (test == "boundary_down") boundaryUpdate(false, true);
    else if (test == "filter_response") filterResponse();
    else if (test == "vr_monotonic") monotonicVR();
    else if (test == "hysteresis") hysteresis();
    else if (test == "millis_wrap") millisWrap();
    else if (test == "timer_config") timerConfiguration();
    else if (test == "display_range") displayRange();
    else throw std::runtime_error("unknown test");
    std::printf("PASS %s\n", test.c_str());
    return 0;
  } catch (const std::exception& error) {
    std::printf("FAIL %s: %s\n", test.c_str(), error.what());
    return 1;
  }
}
