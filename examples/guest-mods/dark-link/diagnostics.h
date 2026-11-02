/* Read-only visual-path probe. No writes to game material/render state.
 * 0.1.2's local PE fog route did not produce a visible change. Do not infer
 * renderer behavior from the mocked guest-memory tests. */
#pragma once
#define DL_PROBE_STEPS 60
#define DL_PROBE_SESSIONS 4
#define DL_MAX_MATERIALS 96
static u32 dl_probe_seen[3], dl_probe_calls[4], dl_probe_matches[4];
static u32 dl_probe_remaining, dl_probe_sessions;
static unsigned long long dl_probe_step;
static int dl_probe_on, dl_probe_state = -1;

/* Fixed-size, bounded scalar log rows, no printf import or game asset dumps. */
static void dl_report(const char* label, const u32* values, u32 count) {
    char line[256]; u32 n = 0;
    while (*label && n < 64) line[n++] = *label++;
    for (u32 i = 0; i < count && i < 20; ++i) {
        line[n++] = ' ';
        for (u32 j = 0; j < 8; ++j)
            line[n++] = "0123456789abcdef"[(values[i] >> (28 - j * 4)) & 15];
    }
    line[n] = 0;
    wwhd_log(line);
}
static void dl_probe_summary(void) {
    dl_report("DL probe calls [wrapper,entry,entry-return,viewCalc]", dl_probe_calls, 4);
    dl_report("DL probe player matches [same order]", dl_probe_matches, 4);
}
static void dl_probe_tick(void) {
    int on = wwhd_config_bool("diagnostics", 1);
    if (!on) {
        dl_probe_on = 0; dl_probe_remaining = 0; dl_probe_sessions = 0;
        dl_probe_state = -1;
        return;
    }
    if (!dl_probe_on) {
        dl_probe_on = 1;
        wwhd_log("Dark Link 0.1.3 DIAGNOSTIC: appearance unavailable; read-only visual probe enabled");
    }
    unsigned long long step = wwhd_logic_step();
    if (step == dl_probe_step) return;
    dl_probe_step = step;
    int state = dl_state.active | (dl_enabled << 1);
    if (state != dl_probe_state) {
        if (dl_probe_remaining) dl_probe_summary();
        dl_probe_state = state;
        dl_probe_remaining = 0;
        if (dl_probe_sessions < DL_PROBE_SESSIONS) {
            ++dl_probe_sessions;
            dl_probe_remaining = DL_PROBE_STEPS;
            for (u32 i = 0; i < 3; ++i) dl_probe_seen[i] = 0;
            for (u32 i = 0; i < 4; ++i) dl_probe_calls[i] = dl_probe_matches[i] = 0;
            u32 row[] = {dl_probe_sessions, (u32)dl_state.active, (u32)dl_enabled,
                (u32)dl_gameplay, (u32)dl_owner, dl_owner_id};
            dl_report("DL phase [session,active,enabled,gameplay,owner,id]", row, 6);
        }
    } else if (dl_probe_remaining && !--dl_probe_remaining) dl_probe_summary();
}
static void dl_probe_materials(u8* model, u8* data, u32 slot) {
    u32 count = dl_word(data, 0x0c), runtime_count = dl_word(model, 0x138);
    u8* materials = (u8*)dl_word(data, 0x10);
    u8* runtime = (u8*)dl_word(model, 0x13c);
    if (!materials || count > DL_MAX_MATERIALS) return;
    s32 eyes[2] = {-1, -1};
    u8* resource = (u8*)dl_word(data, 0);
    if (slot == 0 && resource) {
        u32 relative = dl_word(resource, 0x18);
        if (relative) {
            void* table = resource + 0x18 + (s32)relative;
            const u8* names = (const u8*)&WWHD_GAME_DATA(DL_EYE_MATERIAL_NAMES, u8);
            for (u32 i = 0; i < 2; ++i) {
                const char* name = (const char*)dl_word(names, i * 8);
                if (name) eyes[i] = dl_material_name_index(table, name);
            }
        }
    }
    u32 eye_row[] = {slot, (u32)eyes[0], (u32)eyes[1]};
    dl_report("DL eyes [model-slot,left,right]", eye_row, 3);
    for (u32 i = 0; i < count; ++i) {
        u8* mat = materials + i * 0x39c;
        u8* res = (u8*)dl_word(mat, 0);
        u32 index = res ? *(u16*)(res + 0x0c) : 0xffffffffu;
        u32 row[] = {slot, i, index, dl_word(mat, 0xa0), dl_word(mat, 8),
            dl_word(mat, 0x10), dl_word(mat, 0x18), dl_word(mat, 0x20), dl_word(mat, 0x24)};
        dl_report("DL material [slot,i,index,mask,shape,color,tev,pe,anm]", row, 9);
        /* The per-instance material record is 0x38 bytes (PlayerRuntimeMaterial).
         * Read its scalar words only; never follow unknown pointers. */
        if (runtime && runtime_count <= DL_MAX_MATERIALS && index < runtime_count) {
            u32 words[16]; words[0] = slot; words[1] = i;
            for (u32 j = 0; j < 14; ++j) words[j + 2] = dl_word(runtime + index * 0x38, j * 4);
            dl_report("DL instance [slot,i,14 words]", words, 16);
        }
    }
}
static void dl_probe_observe(u32 stage, void* model) {
    if (!dl_probe_on || !dl_probe_remaining) return;
    ++dl_probe_calls[stage];
    daPy_lk_c* link = dl_player();
    if (!model || !link || link != dl_owner) return;
    u32 address = (u32)model, slot;
    if (address == link->mpCLModel) slot = 0;
    else if (address == link->mpHandsModel) slot = 1;
    else if (address == link->mpKatsuraModel) slot = 2;
    else return;
    ++dl_probe_matches[stage];
    if (dl_probe_seen[slot] & (1u << stage)) return;
    dl_probe_seen[slot] |= 1u << stage;
    u8* data = (u8*)dl_word(model, 0xac);
    u32 row[] = {stage, slot, address, (u32)data, dl_word(model, 0x74),
        dl_word(model, 0x138), dl_word(model, 0x13c),
        data ? dl_word(data, 0x0c) : 0, data ? dl_word(data, 0x10) : 0};
    dl_report("DL model [stage,slot,ptr,data,flags,ninst,inst,nmat,mat]", row, 9);
    /* viewCalc is only an observation point here, not a claimed immediate draw.
     * Snapshot once per model/phase; never hook the asynchronous UBO worker. */
    if (stage == 3 && data) dl_probe_materials(model, data, slot);
}
WWHD_HOOK_RETURN(WWHD_ADDR_daPy_Execute, dl_probe_execute, (daPy_lk_c* link)) {
    if (link == dl_player()) dl_probe_tick();
}
WWHD_HOOK(0x025E2E5C, dl_probe_wrapper, (void* model)) { dl_probe_observe(0, model); }
WWHD_HOOK(DL_MODEL_ENTRY, dl_probe_entry, (void* model)) { dl_probe_observe(1, model); }
WWHD_HOOK_RETURN(DL_MODEL_ENTRY, dl_probe_return, (void* model)) { dl_probe_observe(2, model); }
WWHD_HOOK(0x027F55FC, dl_probe_view, (void* model)) { dl_probe_observe(3, model); }
