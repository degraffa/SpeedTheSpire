#pragma once

// Umbrella header for the A6.1 diff harness -- includes the whole public API in
// one place (trace format, field-by-field differ, reproducer emitter, oracle
// adapter interface, and the fight-replay driver). Consumers (differ_test now,
// a future CLI or A6.2's fixture generator) can include just this.

#include "sts/diff/differ.hpp"
#include "sts/diff/oracle.hpp"
#include "sts/diff/replay.hpp"
#include "sts/diff/reproducer.hpp"
#include "sts/diff/trace.hpp"
