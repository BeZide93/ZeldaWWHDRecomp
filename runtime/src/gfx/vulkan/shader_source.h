// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <memory>
#include <string>
namespace gfxvk::vk {
using ShaderSource = std::shared_ptr<const std::string>;
// Hash buckets only nominate candidates; exact bytes decide sharing.
inline ShaderSource canonical_shader_source(std::string& scratch,
        const ShaderSource& disk, const ShaderSource& pending) {
    ShaderSource result;
    if (disk && *disk == scratch) result = disk;
    else if (pending && *pending == scratch) result = pending;
    else result = std::make_shared<const std::string>(std::move(scratch));
    std::string{}.swap(scratch);
    return result;
}
// A colliding/newer submission must survive an older binary's publication.
template<class Map, class Key, class Binary>
void release_pending_shader_source(Map& pending, const Key& key,
        const std::shared_ptr<Binary>& binary) {
    if (auto it = pending.find(key);
        it != pending.end() && it->second.second.lock() == binary)
        pending.erase(it);
}
}
