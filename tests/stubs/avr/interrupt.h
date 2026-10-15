#pragma once
#include "io.h"
namespace sim { extern bool interruptsEnabled; void serviceInterrupts(); }
#define ISR(vector) void vector()
inline void cli() { sim::interruptsEnabled = false; }
inline void sei() { sim::interruptsEnabled = true; sim::serviceInterrupts(); }
