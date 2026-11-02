/* Scoped HD material uniforms. No texture/model replacement, persistent shared
 * material edits, guessed eye positions, screen-space red dots, or new shaders. */
#pragma once

#define DL_MAX_MATERIALS 96
#define DL_DRAW_DEPTH 16
#define DL_COLOR_REGISTERS 11
#define DL_SAVED_MATERIALS (DL_MAX_MATERIALS * 3)
/* HD uses eleven RGBA registers: material/ambient colors, TEV C0..C2 and
 * K0..K3. See the public d_cloth_packet/d_magma lighting layouts, and the
 * native uniform writes in d_kankyo_6, d_a_wind_tag and d_a_bgn_draw.
 * Patching C0/K0 alone recolors only one side of Link's toon-lighting blend. */
typedef struct {
    u8* material;
    f32* value[DL_COLOR_REGISTERS];
    f32 rgb[DL_COLOR_REGISTERS][3];
    u32 dirty;
} DLMaterialSave;
static DLMaterialSave dl_materials[DL_SAVED_MATERIALS];
static u32 dl_material_count, dl_draw_depth;
static u32 dl_draw_marks[DL_DRAW_DEPTH];

static void dl_restore_colors(u32 mark) {
    while (dl_material_count > mark) {
        DLMaterialSave* saved = &dl_materials[--dl_material_count];
        for (u32 i = 0; i < DL_COLOR_REGISTERS; ++i) {
            if (!saved->value[i]) continue;
            for (u32 j = 0; j < 3; ++j) saved->value[i][j] = saved->rgb[i][j];
        }
        /* Restore CPU values AND mark them for the next native uniform upload. */
        *(u32*)(saved->material + 0xa0) |= saved->dirty;
    }
}
static void dl_palette(u8* material, int eye) {
    u32* flags = (u32*)(material + 0xa0);
    DLMaterialSave* saved = 0;
    for (u32 i = 0; i < dl_material_count; ++i)
        if (dl_materials[i].material == material) { saved = &dl_materials[i]; break; }
    if (!saved) {
        if (dl_material_count >= DL_SAVED_MATERIALS) return;
        saved = &dl_materials[dl_material_count++];
        saved->material = material; saved->dirty = 0;
        for (u32 i = 0; i < DL_COLOR_REGISTERS; ++i) {
            f32* color = dl_material_parameter(flags, i);
            saved->value[i] = color;
            if (!color) continue;
            for (u32 j = 0; j < 3; ++j) saved->rgb[i][j] = color[j];
            saved->dirty |= 1u << i;
        }
    }
    /* Save once, but REAPPLY at each nested draw. Model preparation/animation
     * may regenerate uniforms after the outer entry hook. The old duplicate
     * early-return let those native colors through to the actual draw. */
    for (u32 i = 0; i < DL_COLOR_REGISTERS; ++i) {
        f32* color = saved->value[i];
        if (!color) continue;
        color[0] = eye ? 1.0f : 0.008f;
        color[1] = eye ? 0.002f : 0.009f;
        color[2] = eye ? 0.002f : 0.012f;
        /* Alpha, texture animation, eyelids, skinning and depth remain native. */
    }
    *flags |= saved->dirty;
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
    /* Instance identity, not a model name shared with other actors. */
    if (address != link->mpCLModel && address != link->mpHandsModel &&
        address != link->mpKatsuraModel) return;
    /* Leave the native frozen rendering alone. */
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
        /* Resolve the native eye names even if animation/controller attachment
         * changes during a draw; keep controller identity as a fallback. */
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

/* Scope both preparation/submission and the actual HD draw, plus Link's
 * separate eye/eyebrow joint passes. Restore after each native call completes. */
WWHD_HOOK(DL_MODEL_ENTRY, dl_entry_before, (void* model)) { dl_visual_begin(model); }
WWHD_HOOK_RETURN(DL_MODEL_ENTRY, dl_entry_after, (void* model)) { (void)model; dl_visual_end(); }
WWHD_HOOK(DL_MODEL_UPDATE, dl_update_before, (void* model, void* data)) { (void)data; dl_visual_begin(model); }
WWHD_HOOK_RETURN(DL_MODEL_UPDATE, dl_update_after, (void* model, void* data)) { (void)model; (void)data; dl_visual_end(); }
WWHD_HOOK(DL_MODEL_DRAW, dl_draw_before, (void* model)) { dl_visual_begin(model); }
WWHD_HOOK_RETURN(DL_MODEL_DRAW, dl_draw_after, (void* model)) { (void)model; dl_visual_end(); }
WWHD_HOOK(DL_JOINT_ENTRY, dl_joint_before, (void* model, void* joint)) { (void)joint; dl_visual_begin(model); }
WWHD_HOOK_RETURN(DL_JOINT_ENTRY, dl_joint_after, (void* model, void* joint)) { (void)model; (void)joint; dl_visual_end(); }
