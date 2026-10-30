// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <vector>
namespace enhanced::affinity {
// Indices are CPU ids; zero denotes an unavailable/unknown core. A two-cluster device
// uses its fast cluster for Performance as well. Unknown topology leaves affinity alone.
inline std::vector<int> select(const std::vector<long>& frequencies, bool prime) {
    long low = 0, high = 0;
    for (auto f : frequencies) if (f > 0) { low = low ? std::min(low, f) : f; high = std::max(high, f); }
    bool middle = false;
    for (auto f : frequencies) middle |= f > low && f < high;
    std::vector<int> cores;
    for (int i=0;i<int(frequencies.size());i++)
        if (frequencies[i] > 0 && (low == high || (prime || !middle ? frequencies[i] == high : frequencies[i] > low && frequencies[i] < high))) cores.push_back(i);
    return cores;
}
}
