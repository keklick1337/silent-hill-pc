// SPDX-License-Identifier: GPL-3.0-or-later
//
// The resident character models (docs/RENDER.md section 6): the player's model,
// the four models of the map's character groups, and per character type the model it draws with. A model is read
// from the disc file the character table names (its LM: the part list and part names the skeleton and the
// draw-order of the glb drawer come from) together with its texture; once both are in, its materials are bound and
// its skeleton is built, one part per LM model, each hung on the bone its name starts with.
//
// The four group models share one buffer after the player's: each model goes where the previous one ended (a
// cursor), so the order of the loads decides the layout, as in the game.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/game_state/game_ids.h"
#include "engine/world/lm_materials.h"
#include "engine/world/skeleton.h"

namespace sh::chara {

inline constexpr int kGroupModels = 4;
inline constexpr int kCharaModelTypes = 45;
inline constexpr int kNoChara = 0;
inline constexpr int kPlayerChara = 1;

// One character model. The layout is the one the character pool's own models use too (chara_link.h
// chara_model_bytes).
struct CharaModelRecord {
    std::int32_t chara;   // character type, kNoChara when unused
    std::uint8_t loaded;  // the files are in and the skeleton built
    std::int32_t ticket;  // file queue entry of the last read
    sh::world::LmHeader* lm;
    sh::world::TextureImage texture;
    sh::world::SkeletonRecord skeleton;
};

class CharaModels {
public:
    CharaModelRecord* registered(int chara) const;
    void set_registered(int chara, CharaModelRecord* model);
    CharaModelRecord& group(int slot) { return groups_[slot]; }
    CharaModelRecord& player() { return player_; }

    // Every group model unused, every type but the player's unregistered, the group buffer cursor at its start.
    void free_all();
    // Starts the reads of the player's model and texture.
    void load_player();
    // Starts the reads of the map's group models from the first group whose type changed; returns the last ticket.
    std::int32_t load_map_groups();
    // Frees the group models whose bit is set in `force_free`, then moves the cursor past the models still in.
    void assign_buffers(std::uint8_t force_free);
    // Loads `chara` into group slot `slot`: into `lm` when given, else into the slot's buffer when it holds a model,
    // else at the cursor; the texture where `place` says, else where the character's slot puts it.
    void load(int chara, int slot, sh::world::LmHeader* lm, const sh::world::TextureImage* place);
    // Finishes the player's load once its files are in.
    void process_player_load();
    // Finishes every registered model's load (the player's aside) once its files are in.
    void process_loads();
    // Finishes `model`'s load once its files are in: materials, then the skeleton.
    void process_load(CharaModelRecord& model);

    // Translucency of every primitive of `chara`'s model and its palette's semi-transparency bits.
    void set_translucent(int chara, bool translucent);
    // Binds `chara`'s texture to its materials again with blend mode `blend`.
    void apply_material(int chara, int blend);

    // Whether a group model holds `chara`.
    bool group_has(int chara) const;
    // The skeleton of the model registered for `chara`; null while none.
    sh::world::SkeletonRecord* skeleton(int chara) const;
    // The model (the player's first, then the groups) whose LM holds part `part`; null for none.
    const CharaModelRecord* owner_of_part(const sh::world::LmModel* part) const;
    // The first model (the player's first, then the groups) loaded from `lm`; null for none.
    const CharaModelRecord* model_of(const sh::world::LmHeader* lm) const;

    // The disc file of `chara`'s model as the character table names it now.
    static std::int32_t model_file(int chara);
    // The glb below convert/models/ `chara` is drawn from (chara_library.h drawn_model_glb): the one its
    // character.json "model" names while the type loads that model's skeleton file, else the glb of the file it
    // loads now ("CHARA/HERO.glb"). Empty: none (a mod's new type without a glb).
    static std::string model_glb(int chara);
    // A script's glb for `chara` (sh.chara.model_override), below convert/models/ ("<mod>/CYBIL.glb"): model_glb
    // gives it instead of the data's until it is set empty. It must be bound to the skeleton of the file the type
    // loads (its manifest names that file); a glb that cannot be read is drawn from the disc model as always. Not
    // saved: a script sets it again after a load.
    static void set_model_override(int chara, std::string glb);
    static const std::string& model_override(int chara);

private:
    std::int32_t load_into(int chara, int slot, sh::world::LmHeader* lm, const sh::world::TextureImage& place);
    void release(CharaModelRecord& model);
    void advance(std::uint8_t*& cursor, int chara) const;

    CharaModelRecord* registered_[kCharaModelTypes] = {};
    CharaModelRecord groups_[kGroupModels] = {};
    CharaModelRecord player_ = {};
    std::uint8_t* cursor_ = nullptr;
    // The mods' new types, which only the character pool loads (after the original's block, which storage() maps).
    CharaModelRecord* mod_registered_[sh::game_state::chara::kMaxTypes - kCharaModelTypes] = {};
};

CharaModels& chara_models();

}  // namespace sh::chara
