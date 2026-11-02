/* Restore the original C0/K0 tint, with scoped RGB and override-mask ownership.
 * This is a partial appearance fallback, not a complete Dark Link skin. */
#pragma once
#define DL_MAX_MATERIALS 96
#define DL_DRAW_DEPTH 16
#define DL_TINT_MASK 0x90u /* C0 (4) and K0 (7), d_kankyo_6.cpp */
#define DL_SAVED_MATERIALS (DL_MAX_MATERIALS * 3)
typedef struct {
    u32* flags;
    u32 mask;
    u32* color[2];
    u32 rgb[2][3];
} DLMaterialSave;
static DLMaterialSave dl_materials[DL_SAVED_MATERIALS];
static u32 dl_material_count, dl_draw_depth;
static u32 dl_draw_marks[DL_DRAW_DEPTH];

static void dl_restore_colors(u32 mark) {
    while (dl_material_count > mark) {
        DLMaterialSave* saved = &dl_materials[--dl_material_count];
        for (u32 i = 0; i < 2; ++i)
            if (saved->color[i])
                for (u32 j = 0; j < 3; ++j) saved->color[i][j] = saved->rgb[i][j];
        /* These bits select overrides; do NOT leave newly enabled bits set.
         * Keep changes made by native code to every bit we do not own. */
        *saved->flags = (*saved->flags & ~DL_TINT_MASK) | saved->mask;
    }
}
static void dl_palette(u8* material, int eye) {
    u32* flags = (u32*)(material + 0xa0);
    DLMaterialSave* saved = 0;
    for (u32 i = 0; i < dl_material_count; ++i)
        if (dl_materials[i].flags == flags) { saved = &dl_materials[i]; break; }
    if (!saved) {
        if (dl_material_count >= DL_SAVED_MATERIALS) return;
        saved = &dl_materials[dl_material_count++];
        saved->flags = flags; saved->mask = *flags & DL_TINT_MASK;
        for (u32 i = 0; i < 2; ++i) {
            saved->color[i] = (u32*)dl_material_parameter(flags, i ? 7 : 4);
            if (saved->color[i])
                for (u32 j = 0; j < 3; ++j) saved->rgb[i][j] = saved->color[i][j];
        }
    }
    for (u32 i = 0; i < 2; ++i) {
        f32* color = (f32*)saved->color[i];
        if (!color) continue;
        color[0] = eye ? 1.0f : 0.008f;
        color[1] = eye ? 0.002f : 0.009f;
        color[2] = eye ? 0.002f : 0.012f;
        *flags |= 1u << (i ? 7 : 4);
    }
    /* No alpha writes, no material/ambient lighting overrides, no fog changes. */
}
static void dl_find_eyes(u8* data, u32 count, s32 eyes[2]) {
    eyes[0] = eyes[1] = -1;
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
        if (index >= 0 && (u32)index < count) eyes[i] = index;
    }
}
static void dl_visual_begin(void* model) {
    u32 depth = dl_draw_depth++;
    if (depth >= DL_DRAW_DEPTH) return;
    dl_draw_marks[depth] = dl_material_count;
    if (!dl_state.active || !dl_enabled || !model) return;
    daPy_lk_c* link = dl_player();
    if (!link || link != dl_owner || (dl_word(link, 0x3bc) & 0x800)) return;
    u32 address = (u32)model;
    if (address != link->mpCLModel && address != link->mpHandsModel && address != link->mpKatsuraModel) return;
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
/* Apply the original palette during native material preparation. The user's
 * 0.1.3 trace confirms the entry path and model identities. End the scope before
 * viewCalc queues the UBO job; no writes around that job or joint packet queues. */
WWHD_HOOK(DL_MODEL_ENTRY, dl_entry_before, (void* model)) { dl_visual_begin(model); }
WWHD_HOOK_RETURN(DL_MODEL_ENTRY, dl_entry_after, (void* model)) { (void)model; dl_visual_end(); }
WWHD_HOOK(DL_MODEL_UPDATE, dl_update_before, (void* model)) { dl_visual_begin(model); }
WWHD_HOOK_RETURN(DL_MODEL_UPDATE, dl_update_after, (void* model)) { (void)model; dl_visual_end(); }
