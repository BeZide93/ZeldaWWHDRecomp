/* Per-instance albedo views, applied AFTER native texture animation/refresh.
 * Evidence: story_prop_textures.cpp / story_props.cpp (NNS material bindings,
 * native 027F50F8), d_a_obj_ftree.cpp (J3D -> NNS material index), export_glb.py
 * (_a0 albedo sampler), and Cemu GX2_Texture / LatteReg (RGB component selectors).
 * No image data is copied or changed. Published descriptors are immutable. */
#pragma once
#define DL_TEXTURE_REFRESH 0x027F50F8
#define DL_TEXTURE_BYTES 0xa0u
#define DL_TEXTURE_CACHE 512u
#define DL_TEXTURE_BINDINGS (DL_MAX_MATERIALS * 3)
typedef struct {
    u32 original[DL_TEXTURE_BYTES / 4];
    u32 view[DL_TEXTURE_BYTES / 4];
    u32 eye;
} DLTextureView;
typedef struct {
    u32 model, materials, index, resource, refs, slot, original, view;
} DLTextureBinding;
static DLTextureView dl_texture_cache[DL_TEXTURE_CACHE];
static u32 dl_texture_count, dl_binding_count;
static DLTextureBinding dl_bindings[DL_TEXTURE_BINDINGS];

static int dl_game_span(u32 p, u32 size) {
    return !(p & 3u) && p >= 0x10000000u && p < 0x50000000u && size <= 0x50000000u - p;
}
static u32 dl_relative(u32 p) {
    if (!dl_game_span(p, 4)) return 0;
    u32 delta = *(u32*)p;
    if (!delta) return 0;
    long long address = (long long)p + (s32)delta;
    return address >= 0x10000000ll && address < 0x50000000ll ? (u32)address : 0;
}
static int dl_texture_model(u32 model) {
    daPy_lk_c* link = dl_player();
    return link && link == dl_owner && dl_actor_id(link) == dl_owner_id &&
        (model == link->mpCLModel || model == link->mpHandsModel || model == link->mpKatsuraModel);
}
static int dl_binding_live(const DLTextureBinding* b) {
    if (!dl_texture_model(b->model) || !dl_game_span(b->model, 0xb0) ||
        dl_word((void*)b->model, 0x34) != b->materials) return 0;
    u32 count = *(u16*)(b->model + 0x2a);
    if (b->index >= count || count > DL_MAX_MATERIALS ||
        !dl_game_span(b->materials, count * 0x3c)) return 0;
    u8* material = (u8*)(b->materials + b->index * 0x3c);
    return dl_word(material, 0) == b->resource && dl_word(material, 0x2c) == b->refs &&
        dl_game_span(b->slot, 4);
}
static void dl_texture_restore_model(u32 model) {
    for (u32 i = 0; i < dl_binding_count;) {
        DLTextureBinding* b = &dl_bindings[i];
        if (model && b->model != model) { ++i; continue; }
        /* Native animation or another mod may already have changed this slot.
         * Restore only a still-live binding that contains OUR exact view. */
        if (dl_binding_live(b) && *(u32*)b->slot == b->view) *(u32*)b->slot = b->original;
        dl_bindings[i] = dl_bindings[--dl_binding_count];
    }
}
static void dl_texture_restore_all(void) { dl_texture_restore_model(0); }

static u32 dl_texture_view(u32 texture, int eye) {
    /* Never guess descriptor layouts or dereference an unrecognized mod view. */
    if (!dl_game_span(texture, DL_TEXTURE_BYTES) || *(u32*)texture != 0x46544558u) return 0;
    const u32* source = (const u32*)texture;
    if (!source[2] || !source[3] || !source[0x28 / 4] || (source[0x88 / 4] & 0xffu) > 5) return 0;
    for (u32 i = 0; i < dl_texture_count; ++i) {
        DLTextureView* cached = &dl_texture_cache[i];
        if (cached->eye != (u32)eye) continue;
        u32 j = 0;
        while (j < DL_TEXTURE_BYTES / 4 && cached->original[j] == source[j]) ++j;
        if (j == DL_TEXTURE_BYTES / 4) return (u32)cached->view;
    }
    if (dl_texture_count == DL_TEXTURE_CACHE) return 0;
    DLTextureView* cached = &dl_texture_cache[dl_texture_count++];
    cached->eye = (u32)eye;
    for (u32 j = 0; j < DL_TEXTURE_BYTES / 4; ++j) cached->view[j] = cached->original[j] = source[j];
    /* GX2Texture starts at FTEX+4. Selectors 4/5 mean constant zero/one.
     * Change both compSel (+0x84) and its precomputed SQ_TEX_RESOURCE_WORD4
     * (+0x90). Keep the alpha selector and ALL other register bits intact. */
    cached->view[0x88 / 4] = (source[0x88 / 4] & 0xffu) | (eye ? 0x05040400u : 0x04040400u);
    cached->view[0x94 / 4] = (source[0x94 / 4] & ~(0x1ffu << 16)) |
        ((eye ? 5u : 4u) << 16) | (4u << 19) | (4u << 22);
    return (u32)cached->view;
}
static s32 dl_albedo_slot(u32 resource) {
    if (!dl_game_span(resource, 0x34) || *(u32*)resource != 0x464d4154u) return -1;
    const u8* r = (const u8*)resource;
    u32 count = r[0x10];
    if (!count || count > 32 || count != r[0x11]) return -1;
    u32 samplers = dl_relative(resource + 0x2c);
    if (!dl_game_span(samplers, count * 24)) return -1;
    for (u32 i = 0; i < count; ++i) {
        u32 name = dl_relative(samplers + i * 24 + 0xc);
        /* Names need not be word-aligned; bound the four-byte string. */
        if (name && name <= 0x50000000u - 4) {
            const char* s = (const char*)name;
            if (s[0] == '_' && s[1] == 'a' && s[2] == '0' && !s[3]) return (s32)i;
        }
    }
    /* Do not recolor toon ramps, normal maps or a guessed first sampler. */
    return -1;
}
static void dl_texture_apply(void* model) {
    if (!dl_enabled || !dl_state.active || !dl_texture_model((u32)model) ||
        (dl_word(dl_owner, 0x3bc) & 0x800)) return;
    u8* data = (u8*)dl_word(model, 0xac);
    if (!dl_game_span((u32)data, 0x14)) return;
    u32 count = dl_word(data, 0xc), materials = dl_word(data, 0x10);
    u32 native_count = *(u16*)((u8*)model + 0x2a), native_materials = dl_word(model, 0x34);
    if (!count || count > DL_MAX_MATERIALS || !native_count || native_count > DL_MAX_MATERIALS ||
        !dl_game_span(materials, count * 0x39c) || !dl_game_span(native_materials, native_count * 0x3c)) return;
    s32 eyes[DL_EYE_COUNT];
    for (u32 e = 0; e < DL_EYE_COUNT; ++e) eyes[e] = -1;
    int body = (u32)model == dl_owner->mpCLModel;
    if (body) dl_find_eyes(data, count, eyes);
    for (u32 i = 0; i < count && dl_binding_count < DL_TEXTURE_BINDINGS; ++i) {
        u8* material = (u8*)(materials + i * 0x39c);
        u32 resource = dl_word(material, 0);
        s32 sampler = dl_albedo_slot(resource);
        if (sampler < 0) continue;
        u32 index = *(u16*)(resource + 0xc);
        if (index >= native_count) continue;
        u8* native = (u8*)(native_materials + index * 0x3c);
        if (dl_word(native, 0) != resource) continue;
        u32 refs = dl_word(native, 0x2c), slots = ((u8*)resource)[0x11];
        if (!dl_game_span(refs, slots * 4)) continue;
        u32 slot = refs + (u32)sampler * 4;
        int duplicate = 0;
        for (u32 n = 0; n < dl_binding_count; ++n) if (dl_bindings[n].slot == slot) duplicate = 1;
        if (duplicate) continue;
        int eye = 0;
        if (body) {
            for (u32 e = 0; e < DL_EYE_COUNT; ++e) if ((s32)i == eyes[e]) eye = 1;
            u32 animation = dl_word(material, 0x24);
            if (animation && (animation == dl_owner->m_tex_eye_scroll[0] || animation == dl_owner->m_tex_eye_scroll[1])) eye = 1;
        }
        u32 original = *(u32*)slot, view = dl_texture_view(original, eye);
        if (!view) continue;
        dl_bindings[dl_binding_count++] = (DLTextureBinding){(u32)model, native_materials, index, resource, refs, slot, original, view};
        *(u32*)slot = view;
    }
}
/* The runtime's native prop texture path uses this exact synchronous refresh
 * boundary. Bindings must survive the outer wrapper return: queued packets read
 * per-instance material references later. Restore before the next refresh so
 * texture animation always starts from its own live texture, never our view. */
WWHD_HOOK(DL_TEXTURE_REFRESH, dl_texture_before, (void* model)) {
    if (model && dl_texture_model((u32)model)) dl_texture_restore_model((u32)model);
}
WWHD_HOOK_RETURN(DL_TEXTURE_REFRESH, dl_texture_after, (void* model)) {
    dl_texture_apply(model);
}
