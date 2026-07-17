#pragma once

// Umbrella header for the diff harness -- includes the whole public API in
// one place (trace format, field-by-field differ, reproducer emitter, oracle
// adapter interface, and the fight-replay driver). Consumers (differ_test now,
// a future CLI or the fixture generator) can include just this.

#include "sts/diff/differ.hpp"
#include "sts/diff/oracle.hpp"
#include "sts/diff/replay.hpp"
#include "sts/diff/reproducer.hpp"
#include "sts/diff/trace.hpp"
