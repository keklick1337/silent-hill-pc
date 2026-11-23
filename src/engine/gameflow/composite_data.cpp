#include "engine/gameflow/composite_data.h"

#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

std::string at(const json::Value& v, std::string_view what) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column()) + ": " + std::string(what);
}

}  // namespace

Result<PickupTable> PickupTable::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<PickupTable>::fail("pickups.json: " + doc.error());
    }
    const json::Value* list = doc.value().find("pickups");
    if (list == nullptr || !list->is_array() || list->array().empty()) {
        return Result<PickupTable>::fail("pickups.json: \"pickups\" must be a non-empty array");
    }
    PickupTable out;
    for (const json::Value& item : list->array()) {
        const json::Value* name = item.is_object() ? item.find("item") : nullptr;
        const json::Value* message = item.is_object() ? item.find("message") : nullptr;
        const json::Value* ammo = item.is_object() ? item.find("ammo") : nullptr;
        if (name == nullptr || !name->is_string() || message == nullptr || !message->is_string() ||
            (ammo != nullptr && !ammo->is_bool())) {
            return Result<PickupTable>::fail("pickups.json: " +
                                             at(item, "a pickup needs \"item\" and \"message\" names"));
        }
        PickupEntry e;
        e.item = name->string();
        e.message = message->string();
        e.ammo = ammo != nullptr && ammo->boolean();
        out.entries_.push_back(std::move(e));
    }
    return Result<PickupTable>::ok(std::move(out));
}

Result<PaperMapReveals> PaperMapReveals::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<PaperMapReveals>::fail("paper_map_reveals.json: " + doc.error());
    }
    const json::Value* list = doc.value().find("reveals");
    if (list == nullptr || !list->is_array()) {
        return Result<PaperMapReveals>::fail("paper_map_reveals.json: \"reveals\" must be an array");
    }
    PaperMapReveals out;
    for (const json::Value& item : list->array()) {
        const json::Value* page = item.is_object() ? item.find("page") : nullptr;
        const json::Value* revealed = item.is_object() ? item.find("reveals") : nullptr;
        if (page == nullptr || !page->is_string() || revealed == nullptr || !revealed->is_array()) {
            return Result<PaperMapReveals>::fail(
                "paper_map_reveals.json: " + at(item, "an entry needs a \"page\" name and a \"reveals\" array"));
        }
        RevealEntry e;
        e.page = page->string();
        for (const json::Value& r : revealed->array()) {
            if (!r.is_string()) {
                return Result<PaperMapReveals>::fail("paper_map_reveals.json: " + at(r, "a page must be a name"));
            }
            e.revealed.push_back(r.string());
        }
        out.entries_.push_back(std::move(e));
    }
    return Result<PaperMapReveals>::ok(std::move(out));
}

}  // namespace sh::gameflow
