#include "engine/gameflow/soft_reset.h"

#include <algorithm>
#include <string>
#include <utility>

#include "engine/game_state/game_state.h"
#include "engine/gameflow/state_names.h"
#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

namespace gs = sh::game_state;

using RulesResult = Result<SoftResetRules>;

// The pad's button bits, as the controller layouts name them.
constexpr struct {
    const char* name;
    std::uint32_t bit;
} kButtons[] = {{"Select", 1u << 0}, {"L3", 1u << 1},       {"R3", 1u << 2},      {"Start", 1u << 3},
                {"Up", 1u << 4},     {"Right", 1u << 5},    {"Down", 1u << 6},    {"Left", 1u << 7},
                {"L2", 1u << 8},     {"R2", 1u << 9},       {"L1", 1u << 10},     {"R1", 1u << 11},
                {"Triangle", 1u << 12}, {"Circle", 1u << 13}, {"Cross", 1u << 14}, {"Square", 1u << 15}};

std::uint32_t bit_of(std::string_view name) {
    for (const auto& b : kButtons) {
        if (name == b.name) return b.bit;
    }
    return 0;
}

Result<std::uint32_t> buttons(const json::Value* v, const std::string& key) {
    using R = Result<std::uint32_t>;
    if (v == nullptr || !v->is_array() || v->array().empty()) {
        return R::fail("soft_reset.json: \"" + key + "\" must be a non-empty array of button names");
    }
    std::uint32_t mask = 0;
    for (const json::Value& e : v->array()) {
        const std::uint32_t bit = e.is_string() ? bit_of(e.string()) : 0;
        if (bit == 0) return R::fail("soft_reset.json: unknown button in \"" + key + "\"");
        mask |= bit;
    }
    return R::ok(mask);
}

Result<std::int32_t> vblanks(const json::Value* v, const std::string& key) {
    using R = Result<std::int32_t>;
    if (v == nullptr || !v->is_integer() || v->integer() < 0 || v->integer() > 1000000) {
        return R::fail("soft_reset.json: \"" + key + "\" must be a vertical blank count from 0 to 1000000");
    }
    return R::ok(static_cast<std::int32_t>(v->integer()));
}

Result<std::string> label(const json::Value* v, const std::string& key) {
    if (v == nullptr || !v->is_string() || v->string().empty()) {
        return Result<std::string>::fail("soft_reset.json: \"" + key + "\" must be a non-empty name");
    }
    return Result<std::string>::ok(v->string());
}

}  // namespace

SoftResetRules SoftResetRules::builtin() {
    constexpr std::uint32_t select = 1u << 0, start = 1u << 3, l2 = 1u << 8, r2 = 1u << 9, l1 = 1u << 10,
                            r1 = 1u << 11, triangle = 1u << 12, square = 1u << 15;
    constexpr std::uint32_t all = select | start | l2 | r2 | l1 | r1;
    SoftResetRules r;
    r.spec.hold_buttons = select | start;
    r.spec.hold_vblanks = 120;
    r.spec.hold_name = "select+start-2s";
    r.spec.demo_vblanks = 1800;
    r.spec.combos = {{all, all, "combo"}, {start | triangle | square, start, "combo-alt"}};
    r.blocked_below_state = static_cast<int>(gs::GameState::MovieIntroAlternate);
    r.blocked_steps = {{static_cast<int>(gs::GameState::LoadSavegameScreen), {4}},
                       {static_cast<int>(gs::GameState::SaveScreen), {2, 3}}};
    return r;
}

RulesResult SoftResetRules::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) return RulesResult::fail("soft_reset.json: " + doc.error());
    const json::Value& root = doc.value();
    if (!root.is_object()) return RulesResult::fail("soft_reset.json: the root must be an object");
    SoftResetRules out = builtin();

    if (const json::Value* hold = root.find("hold")) {
        if (!hold->is_object()) return RulesResult::fail("soft_reset.json: \"hold\" must be an object");
        auto mask = buttons(hold->find("buttons"), "hold.buttons");
        if (!mask) return RulesResult::fail(mask.error());
        auto time = vblanks(hold->find("vblanks"), "hold.vblanks");
        if (!time) return RulesResult::fail(time.error());
        auto name = label(hold->find("name"), "hold.name");
        if (!name) return RulesResult::fail(name.error());
        out.spec.hold_buttons = mask.value();
        out.spec.hold_vblanks = time.value();
        out.spec.hold_name = name.value();
    }
    if (const json::Value* demo = root.find("demo_vblanks")) {
        auto time = vblanks(demo, "demo_vblanks");
        if (!time) return RulesResult::fail(time.error());
        out.spec.demo_vblanks = time.value();
    }
    if (const json::Value* combos = root.find("combos")) {
        if (!combos->is_array()) return RulesResult::fail("soft_reset.json: \"combos\" must be an array");
        out.spec.combos.clear();
        for (const json::Value& c : combos->array()) {
            if (!c.is_object()) return RulesResult::fail("soft_reset.json: a combo is {\"held\", \"pressed\", \"name\"}");
            auto held = buttons(c.find("held"), "combos.held");
            if (!held) return RulesResult::fail(held.error());
            auto pressed = buttons(c.find("pressed"), "combos.pressed");
            if (!pressed) return RulesResult::fail(pressed.error());
            auto name = label(c.find("name"), "combos.name");
            if (!name) return RulesResult::fail(name.error());
            out.spec.combos.push_back({held.value(), pressed.value(), name.value()});
        }
    }
    if (const json::Value* below = root.find("blocked_below_state")) {
        const std::int32_t state = below->is_string() ? game_state_by_name(below->string()) : -1;
        if (state < 0) return RulesResult::fail("soft_reset.json: \"blocked_below_state\" must be a game state name");
        out.blocked_below_state = state;
    }
    if (const json::Value* steps = root.find("blocked_steps")) {
        if (!steps->is_array()) return RulesResult::fail("soft_reset.json: \"blocked_steps\" must be an array");
        out.blocked_steps.clear();
        for (const json::Value& e : steps->array()) {
            const json::Value* state = e.is_object() ? e.find("state") : nullptr;
            const json::Value* list = e.is_object() ? e.find("steps") : nullptr;
            const std::int32_t id = state != nullptr && state->is_string() ? game_state_by_name(state->string()) : -1;
            if (id < 0 || list == nullptr || !list->is_array()) {
                return RulesResult::fail("soft_reset.json: \"blocked_steps\" entries are {\"state\", \"steps\"}");
            }
            ResetBlockedSteps b{id, {}};
            for (const json::Value& n : list->array()) {
                if (!n.is_integer() || n.integer() < 0 || n.integer() > 255) {
                    return RulesResult::fail("soft_reset.json: steps are integers from 0 to 255");
                }
                b.steps.push_back(static_cast<std::int32_t>(n.integer()));
            }
            out.blocked_steps.push_back(std::move(b));
        }
    }
    return RulesResult::ok(std::move(out));
}

bool SoftResetRules::blocked(std::int32_t game_state, std::int32_t step) const {
    if (game_state < blocked_below_state) return true;
    for (const ResetBlockedSteps& b : blocked_steps) {
        if (b.game_state == game_state && std::find(b.steps.begin(), b.steps.end(), step) != b.steps.end()) {
            return true;
        }
    }
    return false;
}

}  // namespace sh::gameflow
