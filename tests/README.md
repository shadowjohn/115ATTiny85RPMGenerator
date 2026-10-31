# Host regression tests

Run from this project on the configured Windows machine (V2 by default):

```powershell
& .\tests\run.ps1
```

The original V1 suite remains available without the button/automatic fixtures:

```powershell
& .\tests\run.ps1 -Version V1
```

Individual cases can be selected in PowerShell:

```powershell
& .\tests\run.ps1 -Cases @('display_500', 'display_14000', 'boundary_high')
```

The runner initializes Visual Studio 18 Community's x64 MSVC 14.51.36231 environment, compiles `regression.cpp` with C++17, then runs each case in a fresh process so sketch function-static state cannot leak between cases. The compiler and `vcvars64.bat` paths are explicit in `run.ps1`; update those paths to use another MSVC installation. Build products stay under ignored `tests/build/V1/` or `tests/build/V2/`.

## What is exercised

`regression.cpp` includes the **actual selected V1 or V2 `.ino`**. The runner and conditional include use the same version. The harness supplies Arduino, AVR register, atomic-block and TM1637 interfaces; it does not reimplement the sketch's RPM, filter, step selector or pulse scheduler.

- ADC reads return the selected 0..1023 value after 104 us of simulated acquisition time.
- TM1637 number/segment transfers block the foreground for 20,800 us while Timer1 continues. Tests require a transfer after setup, output edges inside a transfer, correct rising periods and complete 11/12 HIGH, 1/12 LOW phases. The 500 RPM case deliberately places the first transfer across a falling edge.
- Timer1 runs in normal 8-bit mode at 4 us per tick for `TCCR1=7`. A match of `TCNT1` and `OCR1A` raises the compare-A flag and dispatches the sketch's real ISR when enabled. Both `digitalWrite()` and direct `PORTB` changes are recorded as output edges.
- `TIFR` writes use write-one-to-clear semantics; compare flags can remain pending until interrupts are restored. The timer configuration case verifies that setup/start/stop preserve a seeded Timer0 interrupt flag and mask, preserve `PLLE`, select `PCKE=0`, disconnect hardware output B, set TOP=255 and configure PB4 as OUTPUT.
- Cases cover all 28 nonzero RPM steps; stop/negative RPM/restart; repeated same-RPM requests; increasing RPM changes during HIGH and LOW; decreasing RPM changes; warmed filter response; monotonic VR and pulse-period sweeps; all 29 reachable interior thresholds with +/-3 ADC noise; 32-bit `millis()` wrap; and the fixed x10 RPM display.
- Display fixtures use independent literals: 0->0, 500->50, 9500->950, 10000->1000, 10500->1050 and 14000->1400. Each requires numeric rendering and an unchanged requested output RPM. The fixtures do not derive their expectations from `DISPLAY_RPM_DIVISOR`, so restoring the old default or switching units at 10000 fails this test. The 28-step pulse sweep separately checks the real RPM / 60 output frequency.

## Evidence

`baseline-results.txt` records three failures observed before the Timer1/VR fix. The 500/14000 RPM tests reproduced display-induced period errors of 10,804 us / 19,990.286 us. The original cold-start VR case reached only 3000 RPM after 150 ms.

`green-results.txt` records the fixed sketch's 15/15 passing cases. The response case was strengthened after the baseline run to warm the filter at zero first and measure elapsed time to the first observed >=90% RPM value. It observed 13,500 RPM in 50,000 us after the full-scale input change. The full-step sweep's maximum period/HIGH/LOW errors were 1.714 / 2.609 / 2.000 us in this model.

After the user approved fixed x10 RPM as the default, the new display fixture first failed against divisor 1 at 500 RPM (displayed 500, expected 50). The recorded green run uses divisor 10 across the full range.

MSVC `/W4` reports narrowing warnings at explicit AVR-sized values because host `int` is 32 bits, whereas AVR `int` is 16 bits; early harness versions also reported warnings at register replacement interfaces. These are **host compiler warnings**, not results from an AVR build. Actual target compilation is a separate verification step.

## V2 button and automatic sweep (2026-10-26)

The default V2 run runs 21 cases: the original 15 plus six button/automatic cases. Fixtures drive the simulated PB0 input and call the real `setup()` / `loop()`; they do not call the new state-machine functions or derive expected values/times from firmware constants.

- `button_debounce`: PB0 input/pull-up, sub-30 ms press rejection, press bounce, stable press, held press, release bounce, second press restoring manual VR, and restarting automatic mode at zero.
- `button_boot_held`: a grounded PB0 at power-up stays in manual mode; release followed by a new press starts automatic mode.
- `auto_cycle`: independent 0 -> 14000 -> 13000 ... 0 -> 1000 ... 14000 -> 13000 fixtures, one-second interior steps and two-second endpoint holds. Every plateau checks the fixed x10 display and PB4 pulse period (zero must hold LOW), while VR changes cannot override the automatic request.
- `auto_return_vr`: VR changes from full-scale to zero during an automatic plateau; the next press applies that current filtered zero within 50 ms. Manual VR resumes, and another entry restarts the initial zero hold.
- `auto_millis_wrap`: a 30 ms press crosses the 32-bit `millis()` rollover, after which automatic holds/steps and return to manual still work. Windows/MSVC `unsigned long` is 32 bits, matching this part of AVR arithmetic.
- `auto_dwell_wrap`: the initial two-second zero endpoint hold begins before rollover and ends afterward; PB4 stays LOW across wrap and the hold cannot end early. The following full-scale hold, descending step and manual return are also checked.

The input stub enables PORTB's pull-up bit for `INPUT_PULLUP`; an externally grounded button still reads LOW. Button-only debounce boundaries suppress display transfers to isolate the 30 ms requirement. Sweep timing allows -3/+35 ms around each requested dwell because transitions are observed through foreground calls and TM1637 can block for 20.8 ms. The step timing itself remains a foreground scheduling test, not an ISR or physical button measurement.

`button-red-results.txt` records the first five feature cases failing against unmodified V2 before implementation. V1 was then compiled and passed its original 15 cases. The new tests are valid only for V2; `-Version V1` selects the original suite by default.

## Limits

This is a deterministic scheduler regression model, not an electrical or instruction-level AVR emulator. It assigns zero execution time to sketch arithmetic, ISR instructions and atomic sections; it does not inject torn multibyte writes, AVR instruction interleavings, Timer0 ISR latency, oscillator error or analog noise other than explicitly selected input values. It therefore cannot prove interrupt latency bounds, physical jitter, AVR atomicity, calibrated RPM accuracy or receiver acceptance.

The model implements Timer1 compare A, 8-bit wrap, basic interrupt masking/pending delivery and the TIFR W1C behavior needed by these tests. It does not fully emulate other Timer0/Timer1 flags, PWM/CTC modes, PLL clocks, prescaler reset phase or every AVR register side effect. Configuration assertions reject the unsupported Timer1 mode/clock instead of simulating them. A restart may start up to one modeled tick off the wall-clock phase.

Verify real PB4 timing with a scope/logic analyzer while continuously changing VR and refreshing the display. No programming, fuse changes or physical receiver acceptance is performed by this harness.

## V3 three-mode generator (2026-11-01)

Run `python3 tests/run_v3.py` with g++ on Linux, or `& .\tests\run.ps1 -Version V3` on the configured Windows machine. The default PowerShell version stays V2. The Linux runner builds in a temporary directory and removes it after running.

`v3_regression.cpp` selects the actual V3 sketch and reuses the existing register/peripheral simulator. Each case starts a fresh process. All **16/16** cases passed with g++: mode order and 800 ms display, debounce/held press, boot-held release, all 1024 smooth ADC inputs including endpoint plateaus, real loop response, 14 and 50 RPM long pulses, rapid acceleration out of a low-RPM cycle, complete automatic descent/ascent, display timeout across millis rollover, timer configuration, all original 500 RPM steps, repeated requests, high/low-boundary updates, stop/restart and hysteresis. The unchanged V1/V2 source files also passed their original **15/15** and **21/21** cases.

The simulator uses an ideal CPU clock and does not model instruction execution time inside the ISR. AVR target compilation passed separately (damellis attiny 1.0.2, Arduino AVR 1.8.6, TM1637 1.2.0; Internal 16 MHz). Flash: 3576/8192 bytes; static SRAM: 153/512 bytes. Hardware pulse timing and oscillator calibration remain a physical measurement, not a host-test result. Windows runner was updated but not executed on this Linux host.
