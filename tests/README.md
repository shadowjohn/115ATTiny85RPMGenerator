# Host regression tests

Run from this project on John's Windows machine:

```powershell
& .\tests\run.ps1
```

Individual cases can be selected in PowerShell:

```powershell
& .\tests\run.ps1 -Cases @('display_500', 'display_14000', 'boundary_high')
```

The runner initializes Visual Studio 18 Community's x64 MSVC 14.51.36231 environment, compiles `regression.cpp` with C++17, then runs each case in a fresh process so sketch function-static state cannot leak between cases. The compiler and `vcvars64.bat` paths are explicit in `run.ps1`; update those paths to use another MSVC installation. Build products stay under ignored `tests/build/`.

## What is exercised

`regression.cpp` includes the **actual adjacent `.ino`**. The harness supplies Arduino, AVR register, atomic-block and TM1637 interfaces; it does not reimplement the sketch's RPM, filter, step selector or pulse scheduler.

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

## Limits

This is a deterministic scheduler regression model, not an electrical or instruction-level AVR emulator. It assigns zero execution time to sketch arithmetic, ISR instructions and atomic sections; it does not inject torn multibyte writes, AVR instruction interleavings, Timer0 ISR latency, oscillator error or analog noise other than explicitly selected input values. It therefore cannot prove interrupt latency bounds, physical jitter, AVR atomicity, calibrated RPM accuracy or receiver acceptance.

The model implements Timer1 compare A, 8-bit wrap, basic interrupt masking/pending delivery and the TIFR W1C behavior needed by these tests. It does not fully emulate other Timer0/Timer1 flags, PWM/CTC modes, PLL clocks, prescaler reset phase or every AVR register side effect. Configuration assertions reject the unsupported Timer1 mode/clock instead of simulating them. A restart may start up to one modeled tick off the wall-clock phase.

Verify real PB4 timing with a scope/logic analyzer while continuously changing VR and refreshing the display. No programming, fuse changes or physical receiver acceptance is performed by this harness.
