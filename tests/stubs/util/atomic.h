#pragma once
#include <avr/interrupt.h>
namespace sim {
struct AtomicScope {
  bool saved;
  bool once = true;
  AtomicScope(): saved(interruptsEnabled) { interruptsEnabled = false; }
  ~AtomicScope() { interruptsEnabled = saved; serviceInterrupts(); }
};
}
#define ATOMIC_RESTORESTATE 0
#define ATOMIC_FORCEON 1
#define ATOMIC_BLOCK(mode) for (sim::AtomicScope atomicScope; atomicScope.once; atomicScope.once = false)
