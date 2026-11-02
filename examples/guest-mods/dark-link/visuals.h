/* Native, material-local fog blend: recolor after toon lighting without changing
 * the lighting/TEV override mask. No textures, shaders or resource edits. */
#pragma once

#define DL_MAX_MATERIALS 96
#define DL_DRAW_DEPTH 16
#define DL_SAVED_MATERIALS (DL_MAX_MATERIALS * 3)
/* d_kankyo_6.h: J3DMaterial +0x20 -> PE block, whose first word is
 * its vtable; +0x14 is getFogInfo. This is NOT the TEV block's +4 vtable.
 * FogInfo is 0x18 bytes: type/padding, start/end, near/far, RGBA. */
typedef struct {
    u8* fog;
    u32 original[6];
} DLMaterialSave;
static DLMaterialSave dl_materials[DL_SAVED_MATERIALS];
static u32 dl_material_count, dl_draw_depth;
static u32 dl_draw_marks[DL_DRAW_DEPTH];

static void dl_restore_colors(u32 mark) {
    while (dl_material_count > mark) {
        DLMaterialSave* saved = &dl_materials[--dl_material_count];
        for (u32 i = 0; i < 6; ++i) ((u32*)saved->fog)[i] = saved->original[i];
    }
}
static void dl_palette(u8* material, int eye) {
    void* block = (void*)dl_word(material, 0x20);
    if (!block) return;
    void* vtable = (void*)dl_word(block, 0);
    if (!vtable) return;
    u32 getter = dl_word(vtable, 0x14);
    if (!getter) return;
    u8* fog = ((u8* (*)(void*))getter)(block);
    /* Respect unsupported/special fog modes; never fabricate a PE block. */
    if (!fog || (fog[0] != 0 && fog[0] != 2)) return;
    DLMaterialSave* saved = 0;
    for (u32 i = 0; i < dl_material_count; ++i)
        if (dl_materials[i].fog == fog) { saved = &dl_materials[i]; break; }
    if (!saved) {
        if (dl_material_count >= DL_SAVED_MATERIALS) return;
        saved = &dl_materials[dl_material_count++];
        saved->fog = fog;
        for (u32 i = 0; i < 6; ++i) saved->original[i] = ((u32*)fog)[i];
    }
    /* Linear fog blends the final lit RGB, so both toon-lighting branches get
     * the same silhouette color. Put the entire finite ramp behind the camera:
     * every visible fragment is beyond the end; avoid start == end division.
     * Keep native near/far, padding, material alpha and all texture masks. */
    fog[0] = 2;
    *(f32*)(fog + 4) = -10000.0f;
    *(f32*)(fog + 8) = -9999.0f;
    fog[0x14] = eye ? 255 : 2;
    fog[0x15] = eye ? 0 : 2;
    fog[0x16] = eye ? 0 : 3;
    fog[0x17] = 255;
}
static void dl_find_eyes(u8* data, u32 count, s32 indices[2]) {
    indices[0] = indices[1] = -1;
    u8* resource = (u8*)dl_word(data, 0);
    if (!resource) return;
    u32 relative = dl_word(resource, 0x18);
    if (!relative) return;
    void* table = resource + 0x18 + (s32)relative;
    const u8* names = (const u8*)&WWHD_GAME_DATA(DL_EYE_MATERIAL_NAMES, u8);
    for (u32 i = 0; i < 2; ++i) {
        const char* name = (const char*)dl_word(names, i * 8);
        if (!name) continue;
        s32 index = dl_material_name_index(table, name);
        if (index >= 0 && (u32)index < count) indices[i] = index;
    }
}
static void dl_visual_begin(void* model) {
    u32 depth = dl_draw_depth++;
    if (depth >= DL_DRAW_DEPTH) return;
    dl_draw_marks[depth] = dl_material_count;
    if (!dl_state.active || !dl_enabled || !model) return;
    daPy_lk_c* link = dl_player();
    if (!link || link != dl_owner) return;
    u32 address = (u32)model;
    if (address != link->mpCLModel && address != link->mpHandsModel &&
        address != link->mpKatsuraModel) return;
    if (dl_word(link, 0x3bc) & 0x800) return;
    u8* data = (u8*)dl_word(model, 0xac);
    if (!data) return;
    u32 count = dl_word(data, 0x0c);
    u8* materials = (u8*)dl_word(data, 0x10);
    if (!materials || !count || count > DL_MAX_MATERIALS) return;
    s32 eyes[2] = {-1, -1};
    if (address == link->mpCLModel) dl_find_eyes(data, count, eyes);
    for (u32 i = 0; i < count; ++i) {
        u8* material = materials + i * 0x39c;
        u32 animation = dl_word(material, 0x24);
        int eye = address == link->mpCLModel && ((s32)i == eyes[0] || (s32)i == eyes[1] ||
            (animation && (animation == link->m_tex_eye_scroll[0] || animation == link->m_tex_eye_scroll[1])));
        dl_palette(material, eye);
    }
}
static void dl_visual_end(void) {
    if (!dl_draw_depth) return;
    u32 depth = --dl_draw_depth;
    if (depth < DL_DRAW_DEPTH) dl_restore_colors(dl_draw_marks[depth]);
}

/* Scope only the native material preparation. 027F55FC is viewCalc: it queues
 * an asynchronous matrix/UBO job, NOT a draw. Joint entry queues packets too.
 * Neither is a safe boundary for temporary shared material edits. The model
 * entry/update methods below precede viewCalc in mDoExt_modelEntryDL/UpdateDL. */
WWHD_HOOK(DL_MODEL_ENTRY, dl_entry_before, (void* model)) { dl_visual_begin(model); }
WWHD_HOOK_RETURN(DL_MODEL_ENTRY, dl_entry_after, (void* model)) { (void)model; dl_visual_end(); }
WWHD_HOOK(DL_MODEL_UPDATE, dl_update_before, (void* model)) { dl_visual_begin(model); }
WWHD_HOOK_RETURN(DL_MODEL_UPDATE, dl_update_after, (void* model)) { (void)model; dl_visual_end(); }
