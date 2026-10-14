// SPDX-License-Identifier: MPL-2.0
#include "gfx/vulkan/shader_source.h"
#include <cassert>
#include <unordered_map>
using namespace gfxvk::vk;
int main() {
    const std::string text(4096, 'a'), collision(4096, 'b');
    auto disk = std::make_shared<const std::string>(text);
    auto other = std::make_shared<const std::string>(collision);
    std::string scratch = text;
    auto shader = canonical_shader_source(scratch, disk, other);
    assert(shader == disk && scratch.empty());
    scratch = text;
    auto pendingShared = canonical_shader_source(scratch, other, shader);
    assert(pendingShared == shader);
    scratch = text;
    auto distinct = canonical_shader_source(scratch, other, other);
    assert(distinct != other && *distinct == text && scratch.empty());
    // A disk snapshot and a Shader remain valid through cache replacement.
    auto snapshot = disk;
    disk = other;
    assert(*shader == text && *snapshot == text && *disk == collision);
    using Entry = std::pair<ShaderSource, std::weak_ptr<int>>;
    std::unordered_map<unsigned, Entry> pending;
    auto oldBinary = std::make_shared<int>(1), newBinary = std::make_shared<int>(2);
    pending.emplace(7, Entry{shader, oldBinary});
    pending[7] = Entry{other, newBinary};
    release_pending_shader_source(pending, 7u, oldBinary);
    assert(pending.size() == 1 && pending.at(7).first == other);
    release_pending_shader_source(pending, 7u, newBinary);
    assert(pending.empty() && *shader == text && *snapshot == text);
    // Immutable snapshots/export retain byte-for-byte text, including NULs.
    scratch = std::string("abc\0xyz", 7);
    auto binaryText = canonical_shader_source(scratch, {}, {});
    std::string exported(binaryText->data(), binaryText->size());
    assert(exported == std::string("abc\0xyz", 7));
}
