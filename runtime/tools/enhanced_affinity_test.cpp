// SPDX-License-Identifier: MPL-2.0
#include "platform/cpu_affinity_policy.h"
#include <cassert>
int main() {
    using enhanced::affinity::select;
    using V=std::vector<int>;
    assert(select({1800,1800,2400,2400,3200},false)==V({2,3}));
    assert(select({1800,1800,2400,2400,3200},true)==V({4}));
    assert(select({1800,1800,2800,2800},false)==V({2,3}));
    assert(select({2000,2000},true)==V({0,1}));
    assert(select({0,0},true).empty());
    assert(select({0,1800,0,3200},false)==V({3}));
}
