// SPDX-License-Identifier: MPL-2.0
#include "cpu_affinity.h"
#include "cpu_affinity_policy.h"
#include "../mods/enhanced_touch.h"
#include <mutex>
#include <string>
#ifdef __ANDROID__
#include <sched.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <vector>
#endif
namespace enhanced::affinity {
namespace {
Option choice{"enhancedCpuMode", 0, 3};
std::mutex mutex;
std::string result = "Automatic (upstream policy)";
#ifdef __ANDROID__
struct Thread { int tid; Role role; cpu_set_t upstream; };
std::vector<Thread> threads;
void apply(const Thread& thread, int mode) {
    cpu_set_t set = thread.upstream;
    if (mode) {
        std::vector<long> frequencies(CPU_SETSIZE);
        // Use the main/UI thread's allowed set: a worker may inherit its parent's previous pinning.
        cpu_set_t allowed;
        if (sched_getaffinity(getpid(), sizeof allowed, &allowed) != 0) { result = "CPU affinity unavailable"; return; }
        for (int cpu=0;cpu<CPU_SETSIZE;cpu++) if (CPU_ISSET(cpu, &allowed)) {
            char path[128]; snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
            if (FILE* file = fopen(path, "r")) { if (fscanf(file, "%ld", &frequencies[cpu]) != 1) frequencies[cpu]=0; fclose(file); }
        }
        const bool prime = (mode == 2 && thread.role == Role::Render) || (mode == 3 && thread.role == Role::Game);
        const auto cores = select(frequencies, prime);
        if (cores.empty()) { result = "CPU topology unavailable; retaining upstream policy"; return; }
        CPU_ZERO(&set); for (int cpu : cores) CPU_SET(cpu, &set);
    }
    if (sched_setaffinity(thread.tid, sizeof set, &set) != 0)
        result = std::string("CPU affinity refused: ") + strerror(errno);
}
#endif
}
int mode() { return choice.get(); }
const char* status() { thread_local std::string copy; std::lock_guard<std::mutex> lock(mutex); copy=result; return copy.c_str(); }
void set_mode(int value) {
    choice.set(value);
    std::lock_guard<std::mutex> lock(mutex);
    result = value ? "Manual policy applied to registered frame threads" : "Automatic (upstream policy)";
#ifdef __ANDROID__
    for (const auto& thread : threads) apply(thread, choice.get());
#else
    result = "Manual CPU affinity is available on Android";
#endif
}
void add(Role role) {
#ifdef __ANDROID__
    std::lock_guard<std::mutex> lock(mutex);
    Thread thread{int(gettid()), role, {}};
    if (sched_getaffinity(0, sizeof thread.upstream, &thread.upstream) != 0) return;
    threads.erase(std::remove_if(threads.begin(), threads.end(), [&](auto& t) {return t.tid == thread.tid;}), threads.end());
    threads.push_back(thread);
    if (choice.get()) { result = "Manual policy applied to registered frame threads"; apply(thread, choice.get()); }
#else
    (void)role;
#endif
}
void remove() {
#ifdef __ANDROID__
    std::lock_guard<std::mutex> lock(mutex);
    const int tid = gettid();
    threads.erase(std::remove_if(threads.begin(), threads.end(), [&](auto& t) {return t.tid == tid;}), threads.end());
#endif
}
}
