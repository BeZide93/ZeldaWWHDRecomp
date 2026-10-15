// Player-local generated models, resolved only at their exact native archive path.
#pragma once
#include "story_world_manifest.h"
#include <string>
#include <vector>
namespace actor_model_cache {
std::string prepare(const std::string& mod,const story_notebook::ManifestModel& model,
                    const std::string& package_dir,const std::string& data_dir,
                    const std::vector<std::string>& python,const std::string& tool,const std::string& game_dir);
std::string archive_for(const std::string& guest_path);
bool bounds_for(const std::string& archive,story_notebook::WorldPoint& minimum,story_notebook::WorldPoint& maximum);
}
