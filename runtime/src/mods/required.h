// Required packages: the story system's Story Notebook (the mod repository's "story-notebook": the
// letter that brings the story notebook, the notebook's look, the first small quest). While the
// story system is on (the "story-system" built-in, on by default), the mod manager installs it from
// the mod catalogue by itself, confirms it like the port's own code and keeps it enabled; it can't be
// switched off or removed on its own (packages.cpp). Every quest mod depends on it.
//
// Offline, or when the download fails: the story system stays inactive (no letters, takeovers,
// chart marks or notebook), quest mods wait with "Needs the Story Notebook quest", the plain game is
// untouched and no save is changed; the next start (or Retry) tries again.
#pragma once
#include "catalogue_client.h"
#include <string>

namespace mods::required {
inline const std::string kStoryNotebook = "story-notebook";
// The catalogue the package comes from: WWHD_STORY_CATALOGUE, else the mod repository's main channel
// for releases and its devel channel for development builds.
std::string catalogue_url(bool development);
struct Context {
    std::string catalogue;
    catalogue::Fetch fetch;
    catalogue::Version port;
    std::string build = "USA";
    std::filesystem::path work;  // the catalogue cache folder
};
// At startup, after packages::initialize() and before the guest mods load: install, confirm and enable
// the required package if the story system is on. Never throws; the outcome is status().
void ensure(const Context& context);
struct Status {
    bool needed = false;     // the story system is on
    bool installed = false;  // and the package is installed and enabled
    std::string text;        // for the Mods tab: "", "Story notebook: waiting to download (...)"
};
Status status();
}  // namespace mods::required
