// SPDX-License-Identifier: MPL-2.0
// Mod stages in the running game (docs/mod-stages.md; the manifest, names and recipe are mod_stages_manifest.h).
//
// At start-up a package's stages are built (tools/story_mods/stage_builder.py) from the player's own game
// files into the player's stage cache (<manager>/Data/.mod-stages/<name>), or reused from it when nothing
// changed, and the game's file reads of Stage/<name>_Stage.szs and Stage/<name>_Room<n>.szs are served from
// there (hle/fs.cpp). A stage the player's files can't make is left out with a line in the log; the rest of
// the package works.
//
// While the game runs (hooks_mod_stages.txt):
//   - an entry's box in a room of the game takes Link into the stage (the game's own stage change);
//   - the stage's music and scene sound banks are those of its music_like stage (spotNameToId);
//   - saving in the stage stores the stage's entrance, a place of the game, in the save file
//     (setGameStartStage), so the file loads with or without the package; the mod save area (story_store)
//     keeps where in the stage the save was made, and loading that Quest Log while the package is
//     installed continues there (gameStart);
//   - a game assertion is logged with the stage.
// Save states made in a mod stage are refused when the package that makes it is not installed.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mod_json.h"
#include "mod_stages_files.h"
#include "mod_stages_manifest.h"

struct Cpu;

namespace mod_stages {

// A package's stages: checked, named, built (or taken from the cache) and served. Throws with a player-facing
// reason when the manifest's "stages" section is invalid; a stage the player's files can't make is logged and
// left out. game_dir: the player's game folder; python and tool: the builder.
void declare(const std::string& mod, const Value& manifest, const std::string& package_dir, const std::string& data_dir,
             const std::vector<std::string>& python, const std::string& tool, const std::string& game_dir);
bool declared();  // any stage was built this session


struct Info {
    std::string mod, id, name;  // the package, its stage id, the game name
    std::vector<unsigned> rooms;
    Place entrance;
    struct SpawnPoint {
        unsigned room = 0, point = 0;
        Point position;
        double angle = 0;
    };
    std::vector<SpawnPoint> spawns;
};
std::vector<Info> stages();
// the stage named `name` (a game name), or false
bool find(const std::string& name, Info& out);
// "stage:<mod>/<id>" or a game name -> the game name ("" when unknown)
std::string resolve(const std::string& reference);
// A stage name a save state names that can't be loaded now: a mod stage's name (by its form) that no
// installed package makes and the game files don't have.
bool missing(const std::string& stage);

// once per game logic pass (guest_mods logic_service): the entries' boxes
void logic(Cpu* c);

}  // namespace mod_stages
