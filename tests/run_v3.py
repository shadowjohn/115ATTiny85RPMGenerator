#!/usr/bin/env python3
"""Build the real V3 sketch against host peripherals and run isolated cases."""
from pathlib import Path
import subprocess
import tempfile


CASES = (
    "modes mapping low_14 low_50 accelerate auto wrap boot_held "
    "timer_config all_steps same_rpm boundary_high boundary_low "
    "boundary_down stop_restart hysteresis"
).split()


def main():
    root = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="attiny85-v3-") as build:
        executable = str(Path(build) / "regression")
        subprocess.run([
            "g++", "-std=c++17", "-O2", "-DF_CPU=16000000UL",
            "-D__AVR_ATtiny85__=1", "-DHOST_HAS_TIMER1_ISR=1",
            "-DHOST_HAS_SELECT_VR_STEP=1", "-I" + str(root / "stubs"),
            str(root / "v3_regression.cpp"), "-o", executable,
        ], check=True)
        failed = sum(subprocess.run([executable, case]).returncode != 0 for case in CASES)
    print(f"{len(CASES) - failed}/{len(CASES)} cases passed")
    return int(failed != 0)


if __name__ == "__main__":
    raise SystemExit(main())
