// SPDX-License-Identifier: GPL-3.0-or-later
// The resident character models -- see chara_models.h.

#include "engine/chara/runtime/chara_models.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include "engine/app/loader/game_files.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/game/chara_file_table.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/debug/dbg_trace.h"
#include "engine/effects/effect_world.h"
#include "engine/memory/work_buffers.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_runtime.h"
#include "formats/game/model_records.h"
#include "render/geometry/oversized_models.h"

namespace sw = sh::world;

extern "C" {
void Chara_FsImageCalc(sh::loader::ImagePlace* image, std::int32_t chara, std::int32_t slot);
void Lm_MaterialFileIdxApply(sw::LmHeader* lm, std::int32_t file, sw::TextureImage* image, std::int32_t blend);
void Lm_MaterialFlagsApply(sw::LmHeader* lm);
}

namespace sh::chara {

namespace {

static_assert(sizeof(sw::TextureImage) == sizeof(sh::loader::ImagePlace));

// Textures are staged in the file queue's staging buffer.
void* texture_staging() { return sh::memory::work_buffers().data(sh::memory::WorkBuffer::FileStaging); }

const sh::chara::game::CharaFileRow& files(int chara) { return sh::chara::game::chara_file_row(chara); }

std::int32_t file_bytes(int file) {
    return static_cast<std::int32_t>(sh::loader::disc_files().bytes(file));
}

// The map's group models are packed one behind another from the start of their buffer.
std::uint8_t* group_models_start() {
    return sh::memory::work_buffers().as<std::uint8_t>(sh::memory::WorkBuffer::GroupModels);
}

sh::loader::ImagePlace* place_of(sw::TextureImage& image) {
    return reinterpret_cast<sh::loader::ImagePlace*>(&image);
}

sw::TextureImage slot_image(int chara, int slot) {
    sw::TextureImage image{};
    Chara_FsImageCalc(place_of(image), chara, slot);
    return image;
}

void reset(CharaModelRecord& model) {
    model.chara = kNoChara;
    model.loaded = 0;
    model.ticket = 0;
    model.lm = reinterpret_cast<sw::LmHeader*>(group_models_start());
    model.texture = {};
}

}  // namespace

CharaModelRecord* CharaModels::registered(int chara) const {
    if (chara >= kCharaModelTypes && chara < sh::game_state::chara::kMaxTypes) {
        return mod_registered_[chara - kCharaModelTypes];
    }
    return chara >= 0 && chara < kCharaModelTypes ? registered_[chara] : nullptr;
}

void CharaModels::set_registered(int chara, CharaModelRecord* model) {
    if (chara >= kCharaModelTypes && chara < sh::game_state::chara::kMaxTypes) {
        mod_registered_[chara - kCharaModelTypes] = model;
    } else if (chara >= 0 && chara < kCharaModelTypes) {
        registered_[chara] = model;
    }
}

void CharaModels::advance(std::uint8_t*& cursor, int chara) const {
    cursor += (file_bytes(files(chara).model) + 3) & ~3;
}

void CharaModels::free_all() {
    for (int i = 0; i < kCharaModelTypes; i++) {
        if (i != kPlayerChara) {
            registered_[i] = nullptr;
        }
    }
    cursor_ = group_models_start();
    for (CharaModelRecord& model : groups_) {
        reset(model);
    }
}

void CharaModels::load_player() {
    sw::TextureImage image = slot_image(kPlayerChara, 0);
    registered_[kPlayerChara] = &player_;

    const sh::chara::game::CharaFileRow& row = files(kPlayerChara);
    void* lm = sh::models::oversized_models().redirect(row.model, sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlayerModel));
    sh::loader::queue_read(row.model, lm);
    const std::int32_t ticket = sh::loader::queue_read_image(row.texture, texture_staging(), place_of(image));

    player_.chara = kPlayerChara;
    player_.loaded = 0;
    player_.ticket = ticket;
    player_.lm = static_cast<sw::LmHeader*>(lm);
    player_.texture = image;
}

std::int32_t CharaModels::load_map_groups() {
    std::int32_t ticket = 0;
    bool reloading = false;
    cursor_ = group_models_start();
    for (int i = 0; i < kGroupModels; i++) {
        const int chara = sh::maps::map_runtime().group_chara(i);
        if (chara == kNoChara) {
            continue;
        }
        // From the first group whose type differs on, every model is read again (the buffers are packed in order).
        if (!reloading && chara != groups_[i].chara) {
            reloading = true;
            for (int j = i; j < kGroupModels; j++) {
                groups_[j].chara = kNoChara;
            }
        }
        if (reloading) {
            ticket = load_into(chara, i, reinterpret_cast<sw::LmHeader*>(cursor_), slot_image(chara, i));
        }
        advance(cursor_, chara);
    }
    return ticket;
}

void CharaModels::release(CharaModelRecord& model) {
    if (model.chara != kNoChara) {
        registered_[model.chara] = nullptr;
        reset(model);
    }
}

void CharaModels::assign_buffers(std::uint8_t force_free) {
    for (int i = 0; i < kGroupModels; i++) {
        if ((force_free >> i) & 1) {
            release(groups_[i]);
        }
    }
    cursor_ = group_models_start();
    for (CharaModelRecord& model : groups_) {
        if (model.chara == kNoChara) {
            continue;
        }
        // A big model lives on the heap: the cursor follows the group buffer it stands in for.
        const void* base = model.lm;
        if (sh::models::oversized_models().owns(base)) {
            base = sh::models::oversized_models().game_buffer_of(base);
        }
        if (base == nullptr) {
            continue;
        }
        std::uint8_t* end = static_cast<std::uint8_t*>(const_cast<void*>(base)) + file_bytes(files(model.chara).model);
        if (cursor_ < end) {
            cursor_ = end;
        }
    }
}

void CharaModels::load(int chara, int slot, sw::LmHeader* lm, const sw::TextureImage* place) {
    sw::LmHeader* target = lm;
    if (target == nullptr) {
        if (groups_[slot].chara != kNoChara) {
            target = groups_[slot].lm;
        } else {
            target = reinterpret_cast<sw::LmHeader*>(cursor_);
            advance(cursor_, chara);
        }
    }
    load_into(chara, slot, target, place != nullptr ? *place : slot_image(chara, slot));
}

std::int32_t CharaModels::load_into(int chara, int slot, sw::LmHeader* lm, const sw::TextureImage& place) {
    CharaModelRecord& model = groups_[slot];
    const int previous = model.chara;

    if (chara == kNoChara) {
        registered_[previous] = nullptr;
        return 0;
    }
    if (previous != kNoChara) {
        if (chara == previous && lm == model.lm && std::memcmp(&place, &model.texture, sizeof place) == 0) {
            return 0;
        }
        registered_[previous] = nullptr;
    }
    registered_[chara] = &model;

    // Looked up by the row's current model file (rows are retargeted at run time), whatever buffer came in: a
    // reused slot can hand over the previous character's redirected buffer.
    const sh::chara::game::CharaFileRow& row = files(chara);
    lm = static_cast<sw::LmHeader*>(sh::models::oversized_models().redirect(row.model, lm));
    if (SH_LOG_ON(MODEL, SH_LOGLV_DEBUG)) {
        char model_name[48];
        char texture_name[48] = "none";
        DbgTrace_FileName(row.model, model_name, sizeof model_name);
        if (row.texture != -1) {
            DbgTrace_FileName(row.texture, texture_name, sizeof texture_name);
        }
        SH_LOG_DEBUG(MODEL, "chara %s (id %d) model slot %d: %s, texture %s", DbgTrace_CharaName(chara), chara, slot,
                     model_name, texture_name);
    }
    sw::TextureImage image = place;
    std::int32_t ticket = sh::loader::queue_read(row.model, lm);
    if (row.texture != -1) {
        ticket = sh::loader::queue_read_image(row.texture, texture_staging(), place_of(image));
    }

    model.chara = chara;
    model.loaded = 0;
    model.ticket = ticket;
    model.lm = lm;
    model.texture = image;
    return ticket;
}

void CharaModels::process_player_load() {
    process_load(player_);
}

void CharaModels::process_loads() {
    for (int i = 0; i < kCharaModelTypes; i++) {
        if (i != kPlayerChara && registered_[i] != nullptr) {
            process_load(*registered_[i]);
        }
    }
}

void CharaModels::process_load(CharaModelRecord& model) {
    if (model.loaded || model.chara == kNoChara || !sh::loader::queue_loaded(model.ticket)) {
        return;
    }
    model.loaded = 1;

    const sh::chara::game::CharaFileRow& row = files(model.chara);
    sh::formats::records::lm_convert(model.lm);
    Lm_MaterialFileIdxApply(model.lm, row.texture, &model.texture, row.blend % 4);
    Lm_MaterialFlagsApply(model.lm);
    SH_DBG("[CHARA-TEX] chara=%d materials=%d models=%d page=[%d,%d] u=%d v=%d palette=(%d,%d)", model.chara,
           model.lm->material_count, model.lm->model_count, model.texture.page[0], model.texture.page[1],
           model.texture.u, model.texture.v, model.texture.palette_x, model.texture.palette_y);

    sw::SkeletonRecord& skel = model.skeleton;
    sw::skeleton_init(skel, skel.storage, sw::kSkeletonMaxParts);
    sw::skeleton_from_model(skel, model.lm);
    sw::skeleton_bone_index_from_names(skel);
    sw::skeleton_set_visible(skel, true);
}

void CharaModels::set_translucent(int chara, bool translucent) {
    CharaModelRecord* model = registered(chara);
    if (model == nullptr) {
        return;
    }
    sw::lm_set_translucent(*model->lm, translucent);

    // Bit 15 of a palette entry marks it semi-transparent.
    namespace ew = sh::effects::effect_world;
    std::uint16_t rows[16][16];
    for (int y = 0; y < 16; y++) {
        ew::read_palette_row(model->texture.palette_x, model->texture.palette_y + y, rows[y]);
    }
    for (auto& row : rows) {
        for (std::uint16_t& entry : row) {
            entry = translucent ? static_cast<std::uint16_t>(entry | 0x8000u) : static_cast<std::uint16_t>(entry & 0x7FFFu);
        }
    }
    ew::write_palette_rows(model->texture.palette_x, model->texture.palette_y, 16, &rows[0][0]);
}

void CharaModels::apply_material(int chara, int blend) {
    CharaModelRecord* model = registered(chara);
    if (model == nullptr) {
        return;
    }
    Lm_MaterialFileIdxApply(model->lm, files(chara).texture, &model->texture, blend);
    Lm_MaterialFlagsApply(model->lm);
}

bool CharaModels::group_has(int chara) const {
    for (const CharaModelRecord& model : groups_) {
        if (model.chara == chara) {
            return true;
        }
    }
    return false;
}

sw::SkeletonRecord* CharaModels::skeleton(int chara) const {
    CharaModelRecord* model = registered(chara);
    return model != nullptr ? &model->skeleton : nullptr;
}

const CharaModelRecord* CharaModels::owner_of_part(const sw::LmModel* part) const {
    auto holds = [part](const CharaModelRecord& m) {
        return m.lm != nullptr && part >= m.lm->models && part < m.lm->models + m.lm->model_count;
    };
    if (holds(player_)) {
        return &player_;
    }
    for (const CharaModelRecord& model : groups_) {
        if (holds(model)) {
            return &model;
        }
    }
    // Models outside the groups: the pool's (a type spawned off its map) and a mod's new types.
    for (const CharaModelRecord* model : registered_) {
        if (model != nullptr && holds(*model)) {
            return model;
        }
    }
    for (const CharaModelRecord* model : mod_registered_) {
        if (model != nullptr && holds(*model)) {
            return model;
        }
    }
    return nullptr;
}

const CharaModelRecord* CharaModels::model_of(const sw::LmHeader* lm) const {
    if (player_.lm == lm) {
        return &player_;
    }
    for (const CharaModelRecord& model : groups_) {
        if (model.lm == lm) {
            return &model;
        }
    }
    for (const CharaModelRecord* model : registered_) {
        if (model != nullptr && model->lm == lm) {
            return model;
        }
    }
    for (const CharaModelRecord* model : mod_registered_) {
        if (model != nullptr && model->lm == lm) {
            return model;
        }
    }
    return nullptr;
}

std::int32_t CharaModels::model_file(int chara) {
    return chara >= 0 && chara < kCharaModelTypes ? files(chara).model : -1;
}

namespace {
std::array<std::string, sh::game_state::chara::kMaxTypes>& model_overrides() {
    static std::array<std::string, sh::game_state::chara::kMaxTypes> overrides;
    return overrides;
}
}  // namespace

void CharaModels::set_model_override(int chara, std::string glb) {
    if (chara < 0 || chara >= sh::game_state::chara::kMaxTypes) {
        return;
    }
    constexpr std::string_view kModels = "models/";
    if (std::string_view(glb).starts_with(kModels)) {
        glb.erase(0, kModels.size());
    }
    model_overrides()[chara] = std::move(glb);
}

const std::string& CharaModels::model_override(int chara) {
    static const std::string kNone;
    return chara >= 0 && chara < sh::game_state::chara::kMaxTypes ? model_overrides()[chara] : kNone;
}

std::string CharaModels::model_glb(int chara) {
    if (chara < 0 || chara >= sh::game_state::chara::kMaxTypes) {
        return {};
    }
    if (const std::string& chosen = model_override(chara); !chosen.empty()) {
        return chosen;
    }
    const CharacterDef* def = type_def(chara);
    const std::string_view data_model = def != nullptr ? std::string_view(def->model) : std::string_view();
    const std::int32_t data_file = sh::chara::game::chara_data_model_file(chara);
    if (chara >= kCharaModelTypes) {
        // A mod's new type has no glb of the import to fall back to: its disc model only carries the parts.
        return drawn_model_glb(data_model, data_file, data_file, {});
    }
    const std::int32_t current = files(chara).model;
    const sh::loader::DiscFiles& disc = sh::loader::disc_files();
    const std::string path = current > 0 && current < disc.count() ? std::string(disc.path(current)) : std::string();
    return drawn_model_glb(data_model, data_file, current, path);
}

CharaModels& chara_models() {
    static CharaModels models;
    return models;
}

}  // namespace sh::chara
