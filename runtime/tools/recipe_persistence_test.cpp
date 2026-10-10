#include "gfx/vulkan/recipe_persistence.h"
#include "platform/host.h"
#include <cassert>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
static std::vector<uint8_t> read(const std::string& p) {
  std::ifstream f(std::filesystem::u8path(p),std::ios::binary); return {std::istreambuf_iterator<char>(f), {}};
}
int main() {
  auto dir=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::create_directories(dir);auto native=(dir/std::filesystem::path(std::u8string(u8"réçipes-水")));
  auto encoded=native.u8string();std::string path(reinterpret_cast<const char*>(encoded.data()),encoded.size());
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
  assert(rejected && read(path)==old && !std::filesystem::exists(std::filesystem::u8path(path+".tmp")));
  // A crash before replacement leaves the prior complete generation readable;
  // the next save leaves unrelated stale temporaries untouched.
  { std::ofstream f(std::filesystem::u8path(path+".tmp")); f<<"partial"; }
  assert(read(path)==old);
  gfxvk::atomic_recipe_write(path,next,replace);
  assert(read(path)==next && std::filesystem::exists(std::filesystem::u8path(path+".tmp")));
  std::string first, second;
  FILE* a = gfxvk::open_atomic_temporary(path, first);
  FILE* b = gfxvk::open_atomic_temporary(path, second);
  assert(first != second);
  assert(std::fwrite(old.data(),1,old.size(),a)==old.size());
  assert(std::fwrite(next.data(),1,next.size(),b)==next.size());
  std::fclose(a); std::fclose(b);
  assert(read(first)==old && read(second)==next);
  // Interleaved publication produces one complete generation, never mixed bytes.
  assert(replace(first,path)); assert(replace(second,path)); assert(read(path)==next);
  std::filesystem::remove_all(dir);
}
