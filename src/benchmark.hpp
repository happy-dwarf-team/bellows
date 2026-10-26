#pragma once

// Bellows as a participant of Anvil's start-up benchmark (anvil.benchmark v1,
// Anvil 1.4 and later; Anvil's docs/spec/benchmark-v1.md §5 and §11).
//
// load() declares every field once, whatever the modules' modes: per module
// `<module>.mode`, and the numbers of the module's anvil.log lines (counters,
// finish times, the profile it used). The modules set them next to those log
// lines on the ready-report thread, open_warmup reports its open cost, a module
// that built the first profile of a mod set marks the start as learning, and
// the ready report ends with Done(). The log lines stay as they are.
//
// On an Anvil without the service (older than 1.4) every call does nothing and
// Bellows runs as before. Outside a benchmark start Anvil checks the calls and
// records nothing.

#include "common.hpp"

#include <anvil/anvil_benchmark.hpp>

#include <span>

namespace sb::bench {

// Inside load(): looks up anvil.benchmark and declares the fields; `modules`
// are the names of plugin.cpp's module list, each with a `<name>.mode`.
void Join(anvil::Host& host, std::span<const char* const> modules);

// The service (does nothing before Join or without the service).
const anvil::Benchmark& Get();

// `<module>.mode`: "off", "on", "verify" or "failed".
void SetMode(const char* module, const char* mode);

// The counters LogStats prints for `module`, under the same condition (the
// module was installed).
void SetCounters(const char* module, const Stats& stats);

// The start is not representative: a module ran without a learned profile for
// this mod set and is building it.
void NewModSet();

// The participant's after-ready work is finished.
void Done();

}  // namespace sb::bench
