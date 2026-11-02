/* Scoped HD material uniforms. No texture/model replacement, persistent shared
 * material edits, guessed eye positions, screen-space red dots, or new shaders. */
#pragma once

#define DL_MAX_MATERIALS 96
#define DL_DRAW_DEPTH 16
typedef struct { f32* value; f32 rgb[3]; u32* flags; u32 dirty; } DLColorSave;
static DLColorSave dl_colors[DL_MAX_MATERIALS * 2];
static u32 dl_color_count, dl_draw_depth;
static u32 dl_draw_marks[DL_DRAW_DEPTH];

static void dl_restore_colors(u32 mark) {
    while (dl_color_count > mark) {
        DLColorSave* saved = &dl_colors[--dl_color_count];
        for (u32 j = 0; j < 3; ++j) saved->value[j] = saved->rgb[j];
        /* Restore CPU values AND mark them for the next native uniform upload. */
        *saved->flags |= saved->dirty;
    }
}
static void dl_color(u8* material, u32 index, int eye) {
    u32* flags = (u32*)(material + 0xa0);
    f32* color = dl_material_parameter(flags, index);
    if (!color || dl_color_count >= DL_MAX_MATERIALS * 2) return;
    /* An outer model submission may recursively enter the same model/joints. */
    for (u32 i = 0; i < dl_color_count; ++i)
        if (dl_colors[i].value == color) return;
    DLColorSave* saved = &dl_colors[dl_color_count++];
    saved->value = color; saved->flags = flags; saved->dirty = 1u << index;
    for (u32 j = 0; j < 3; ++j) saved->rgb[j] = color[j];
    /* Linear RGB: charcoal body, bright red native eye surfaces. Preserve alpha,
     * texture animation, eyelids, skinning, depth testing and material visibility. */
    color[0] = eye ? 1.0f : 0.008f;
    color[1] = eye ? 0.002f : 0.009f;
    color[2] = eye ? 0.002f : 0.012f;
    *flags |= saved->dirty;
}
static void dl_visual_begin(void* model) {
    u32 depth = dl_draw_depth++;
    if (depth >= DL_DRAW_DEPTH) return;
    dl_draw_marks[depth] = dl_color_count;
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
    for (u32 i = 0; i < count; ++i) {
        u8* material = materials + i * 0x39c;
        u32 animation = dl_word(material, 0x24);
        /* Native createHeap attaches these two controllers only to the eyes. */
        int eye = address == link->mpCLModel && animation &&
            (animation == link->m_tex_eye_scroll[0] || animation == link->m_tex_eye_scroll[1]);
        dl_color(material, 4, eye); /* native TEV color 0 uniform */
        dl_color(material, 7, eye); /* native TEV constant color 0 uniform */
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
