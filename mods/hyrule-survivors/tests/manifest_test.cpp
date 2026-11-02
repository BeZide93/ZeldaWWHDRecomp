// SPDX-License-Identifier: MPL-2.0
#include "mods/mod_stages_manifest.h"
#include "stage_name.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream f(argv[1]);
    assert(f);
    const std::string text((std::istreambuf_iterator<char>(f)), {});
    const auto m = mods::json::parse(text);
    std::string why;
    std::vector<mod_stages::Stage> stages;
    if (!mod_stages::parse(m, stages, why)) {
        std::cerr << why << '\n';
        return 1;
    }
    assert(stages.size() == 1 && stages[0].rooms.size() == 1);
    assert(stages[0].rooms[0].kit_ground);
    assert(mod_stages::allocate_name(m.get("id").text, stages[0].id, [](const std::string &) { return false; }) ==
           HS_STAGE_NAME);
    std::cout << "Stage manifest and runtime stage-name agreement: passed\n";
}
