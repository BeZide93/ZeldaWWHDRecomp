// Story quests declared in a mod's manifest.json ("quests_format": 1, "quests": [...]), read when the mod
// is enabled, without running mod code (docs/mod-sdk-v2.md, "Quests in the manifest"). The port then
// sends the quest's letter, starts the quest when the letter is read, hands Link the enclosed letter,
// shows each step's texts and place, and runs the declared resident talks; the mod's code only reports
// progress the port can't see. Pure code, unit-tested in runtime/tools/story_notebook_test.cpp.
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "mod_json.h"
#include "story_mail_book.h"
#include "story_notebook_list.h"
#include "story_takeover.h"

namespace story_notebook {

struct ManifestStep {
    unsigned n = 0;
    std::string text, where;
    bool has_place = false;
    float x = 0, z = 0;
    bool has_talk = false;
    Resident resident = kResidentNone;
    bool needs_letter = false;
    std::string lines;   // joined with '\n'
    uint8_t reward = 0;  // a rupee item
};
struct ManifestQuest {
    std::string id, title, reward_text, start_text, journal = "on_enable";
    uint8_t reward_item = 0;
    bool has_letter = false, letter_encloses_letter = false;
    std::string letter_id, letter_sender, letter_text;
    std::vector<ManifestStep> steps;
};

// "rupee_green" .. "rupee_orange"; 0 if not one
inline uint8_t rupee_item(const std::string& name) {
    static const char* const kNames[] = {"rupee_green", "rupee_blue", "rupee_yellow", "rupee_red", "rupee_purple", "rupee_orange"};
    for (uint8_t i = 0; i < 6; i++)
        if (name == kNames[i]) return uint8_t(i + 1);
    return 0;
}
// a resident by its game identity: actor class and resident code
inline Resident resident_of(const std::string& actor_class, const std::string& code) {
    if (actor_class == "people" && code == "UB3") return kResidentWindfallHarbour;
    return kResidentNone;
}

// Parses the manifest's quests. Returns false with a reason for anything it doesn't accept (the mod's
// quests are then not in the notebook; the mod itself still loads).
inline bool parse_quests(const mods::json::Value& manifest, std::vector<ManifestQuest>& out, std::string& why) {
    using mods::json::Value;
    out.clear();
    const Value& quests = manifest.get("quests");
    if (quests.type == Value::Null) return true;
    const Value& format = manifest.get("quests_format");
    if (format.type != Value::Number || format.number != 1) return why = "unsupported quests_format", false;
    if (quests.type != Value::Array || quests.array.size() > kMaxQuestsPerMod) return why = "quests must be a list of up to 8", false;
    auto text = [&](const Value& v, size_t max, bool lines, std::string& to, const char* what) {
        if (v.type == Value::Null) return true;
        std::u16string ignored;
        if (v.type != Value::String || v.text.size() > max || !story_mail::append_utf16(v.text, lines, ignored))
            return why = std::string("invalid ") + what, false;
        to = v.text;
        return true;
    };
    for (const Value& q : quests.array) {
        ManifestQuest m;
        if (q.type != Value::Object) return why = "a quest must be an object", false;
        if (q.get("id").type != Value::String || !story_mail::valid_id(q.get("id").text)) return why = "invalid quest id", false;
        m.id = q.get("id").text;
        if (!text(q.get("title"), kMaxTitleBytes, false, m.title, "title") || m.title.empty()) return why = "quest " + m.id + ": invalid title", false;
        if (q.get("journal").type == Value::String) {
            m.journal = q.get("journal").text;
            if (m.journal != "on_enable" && m.journal != "on_trigger" && m.journal != "hidden") return why = "quest " + m.id + ": invalid journal", false;
        }
        if (!text(q.get("start").get("text"), kMaxNextBytes, false, m.start_text, "start text")) return false;
        if (!text(q.get("reward").get("text"), kMaxRewardBytes, false, m.reward_text, "reward text")) return false;
        if (q.get("reward").get("item").type == Value::String && !(m.reward_item = rupee_item(q.get("reward").get("item").text)))
            return why = "quest " + m.id + ": unsupported reward item", false;
        const Value& letter = q.get("letter");
        if (letter.type == Value::Object) {
            m.has_letter = true;
            if (letter.get("id").type != Value::String || !story_mail::valid_id(letter.get("id").text)) return why = "invalid letter id", false;
            m.letter_id = letter.get("id").text;
            if (!text(letter.get("sender"), story_mail::kMaxSenderBytes, false, m.letter_sender, "letter sender") ||
                !text(letter.get("text"), story_mail::kMaxTextBytes, true, m.letter_text, "letter text") || m.letter_sender.empty() ||
                m.letter_text.empty())
                return why = "quest " + m.id + ": invalid letter", false;
            for (const Value& e : letter.get("encloses").array)
                if (e.type == Value::String && e.text.rfind("letter:", 0) == 0) m.letter_encloses_letter = true;
        }
        for (const Value& st : q.get("steps").array) {
            ManifestStep s;
            if (st.get("n").type != Value::Number || st.get("n").number < 1 || st.get("n").number > kMaxStep || std::floor(st.get("n").number) != st.get("n").number)
                return why = "quest " + m.id + ": invalid step number", false;
            s.n = unsigned(st.get("n").number);
            if (!text(st.get("text"), kMaxNextBytes, false, s.text, "step text") || !text(st.get("where"), kMaxWhereBytes, false, s.where, "where"))
                return false;
            const Value& place = st.get("place");
            if (place.type == Value::Object && place.get("x").type == Value::Number && place.get("z").type == Value::Number) {
                s.x = float(place.get("x").number), s.z = float(place.get("z").number);
                if (!(std::fabs(s.x) <= 350000 && std::fabs(s.z) <= 350000)) return why = "quest " + m.id + ": place off the sea", false;
                s.has_place = true;
            }
            const Value& npc = st.get("npc");
            if (npc.type == Value::Object) {
                s.has_talk = true;
                s.resident = resident_of(npc.get("who").get("class").text, npc.get("who").get("code").text);
                if (s.resident == kResidentNone) return why = "quest " + m.id + ": unsupported resident", false;
                s.needs_letter = npc.get("when").get("carries").type == Value::String;
                for (const Value& l : npc.get("lines").array) {
                    if (l.type != Value::String) return why = "invalid line", false;
                    if (!s.lines.empty()) s.lines += '\n';
                    s.lines += l.text;
                }
                std::u16string ignored;
                if (s.lines.empty() || s.lines.size() > kMaxLinesBytes || npc.get("lines").array.size() > 3 ||
                    !story_mail::append_utf16(s.lines, true, ignored))
                    return why = "quest " + m.id + ": the lines must be up to three, 240 bytes", false;
                const Value& give = npc.get("then").get("give");
                if (give.type == Value::String && !(s.reward = rupee_item(give.text))) return why = "quest " + m.id + ": unsupported item to give", false;
            }
            m.steps.push_back(s);
        }
        out.push_back(std::move(m));
    }
    return true;
}

}  // namespace story_notebook
