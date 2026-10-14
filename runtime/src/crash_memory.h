#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace crash_memory {
using Output = void (*)(int,const char*,size_t);
struct Section { uint32_t address, flags; std::vector<uint8_t> bytes; uint64_t extent=0; };
// Normal startup only. Captures executable/non-writable input-image bytes, never live guest data.
bool enabled();
void install(std::vector<Section> sections);
// Normal thread only; all OS queries/formatting precede atomic publication.
void refresh(uint32_t guest_threads, uint32_t runtime_top);
// Bounded, allocation/lock-free; no guest or OS-memory dereferences.
void note(int fd, Output out, const uint32_t* registers);
}
