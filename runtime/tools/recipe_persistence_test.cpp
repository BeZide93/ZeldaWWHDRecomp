#include "gfx/vulkan/recipe_persistence.h"
#include "platform/host.h"
#include <cassert>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
static std::vector<uint8_t> read(const std::filesystem::path& p) {
  std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f), {}};
}
int main() {
  auto dir=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::create_directories(dir);auto path=(dir/"recipes").string();
  std::vector<uint8_t> old{1,2,3}, next{4,5,6,7};
  auto replace=host::replace_file;
  gfxvk::RecipeSaveRevision revisions;
  revisions.changed(); const auto snapshot=revisions.current;
  gfxvk::atomic_recipe_write(path,old,replace); assert(read(path)==old);
  // New recipe arrives during the completed file's rename callback. The old
  // snapshot is valid on disk, but another periodic save remains required.
  gfxvk::atomic_recipe_write(path,old,[&](auto& a,auto& b) {
    revisions.changed(); return replace(a,b);
  });
  revisions.complete(snapshot); assert(revisions.dirty());
  revisions.complete(revisions.current); assert(!revisions.dirty());
  bool rejected=false;
  try { gfxvk::atomic_recipe_write(path,next,[](auto&,auto&) { return false; }); }
  catch(const std::runtime_error&) { rejected=true; }
  assert(rejected && read(path)==old && !std::filesystem::exists(path+".tmp"));
  // A crash before replacement leaves the prior complete generation readable;
  // the next save truncates/removes that stale temporary rather than publishing it.
  { std::ofstream f(path+".tmp"); f<<"partial"; }
  assert(read(path)==old);
  gfxvk::atomic_recipe_write(path,next,replace);
  assert(read(path)==next && !std::filesystem::exists(path+".tmp"));
  std::filesystem::remove_all(dir);
}
