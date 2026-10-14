// Required packages (required.h).
#include "required.h"
#include "packages.h"
#include "mods.h"
#include "runtime.h"

#include <mutex>

namespace mods::required {
namespace {
std::mutex g_mu;
Status g_status;
void set(const Status& s) {
    std::lock_guard lock(g_mu);
    g_status = s;
}
const packages::View* find(const std::vector<packages::View>& list, const std::string& id) {
    for (const auto& v : list)
        if (v.id == id) return &v;
    return nullptr;
}
// installed: confirm it as the port's own and keep it enabled
bool keep_enabled(std::string& why) {
    auto list = packages::list();
    const auto* v = find(list, kStoryNotebook);
    if (!v) return why = "not installed", false;
    std::string error;
    if (!v->native_confirmed && !packages::confirm_native(kStoryNotebook, error)) return why = error, false;
    if (!v->enabled) {
        if (!packages::unacknowledged_experimental(kStoryNotebook).empty() && !packages::acknowledge_experimental(kStoryNotebook, error))
            return why = error, false;
        if (!packages::enable(kStoryNotebook, true, error)) return why = error, false;
        packages::refresh_guest_startup();  // it loads in this start already
        LOG("[mods] %s enabled: the story system's required package", kStoryNotebook.c_str());
    }
    return true;
}
}  // namespace

std::string catalogue_url(bool development) {
    if (const char* forced = getenv("WWHD_STORY_CATALOGUE"); forced && *forced) return forced;
    return std::string("https://raw.githubusercontent.com/ZeldaWWHDRecomp/ZeldaWWHDMods/") + (development ? "devel" : "main") + "/index.json";
}

void ensure(const Context& c) {
    Status s;
    s.needed = mods::story_system();
    if (!s.needed) return set(s);
    std::string why;
    if (keep_enabled(why)) {
        s.installed = true;
        return set(s);
    }
    if (why == "not installed") {
        try {
            catalogue::Loaded loaded;
            try {
                loaded = catalogue::refresh_cached(c.catalogue, c.fetch, c.work);
            } catch (const std::exception& e) {
                LOG("[mods] the mod catalogue isn't reachable (%s); trying its cached copy", e.what());
                loaded = catalogue::cached(c.catalogue, c.work);
            }
            const catalogue::Entry* entry = nullptr;
            for (const auto& e : loaded.index.entries)
                if (e.id == kStoryNotebook) entry = &e;
            catalogue::require(entry != nullptr, "the catalogue has no " + kStoryNotebook);
            catalogue::StagedPackage package(*entry, c.port, c.build, packages::platform_key(), c.work, loaded.fixture_root, c.fetch);
            std::string error, id;
            catalogue::require(packages::install(package.path().string(), error, &id), error);
            LOG("[mods] %s %s installed from the catalogue: the story system's required package", kStoryNotebook.c_str(), entry->version.c_str());
            if (keep_enabled(why)) {
                s.installed = true;
                return set(s);
            }
        } catch (const std::exception& e) {
            why = e.what();
        }
    }
    s.text = "Story notebook: waiting to download (" + why + ")";
    LOG("[mods] %s", s.text.c_str());
    set(s);
}

Status status() {
    std::lock_guard lock(g_mu);
    return g_status;
}
}  // namespace mods::required
