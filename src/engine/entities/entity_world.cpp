// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/entity_world.h"

#include "engine/chara/behavior_state.h"
#include "engine/entities/console_character.h"
#include "engine/game_state/session.h"

#include <span>

namespace sh::entities {

CharacterSlots& EntityWorld::storage() {
    return storage_ != nullptr ? *storage_ : sh::game::session().system.characters;
}

bool EntityWorld::live(int slot) {
    const CharacterRecord* r = slot >= 0 && slot < kSlotCount ? storage().record(slot) : nullptr;
    return r != nullptr && r->model.chara_id != 0;
}

Character* EntityWorld::character(int slot) {
    if (!live(slot)) {
        return nullptr;
    }
    CharacterRecord* r = storage().record(slot);
    const int id = r->model.chara_id;
    Character& c = slots_[static_cast<std::size_t>(slot)];
    c.bind(slot, r, host_ != nullptr ? host_->def(id) : nullptr);
    const sh::chara::BehaviorLayout* layout = host_ != nullptr ? host_->var_layout(id) : nullptr;
    if (layout != nullptr) {
        c.vars().bind(layout, std::span<std::uint8_t>(r->vars.bytes, sizeof(r->vars.bytes)));
    } else {
        c.vars().clear();
    }
    if (host_ != nullptr) {
        BoneBinding b = host_->bones(slot);
        if (b.bones != nullptr && b.animation != nullptr) {
            const int count = static_cast<int>(b.animation->skeleton().bone_count());
            c.bind_bones(b.bones, count, std::move(b.animation));
            c.update_world();
        }
    }
    return &c;
}

int EntityWorld::create(int chara_id, const Vec3& position, const Angles& rotation, int spawn_state) {
    const int slot = host_ == nullptr ? -1 : host_->create(chara_id, position, rotation, spawn_state);
    if (slot >= 0 && slot < kSlotCount) {
        ++generations_[static_cast<std::size_t>(slot)];
    }
    return slot;
}

bool EntityWorld::remove(int slot) {
    if (slot <= 0 || slot >= kSlotCount || host_ == nullptr || !host_->remove(slot)) {
        return false;
    }
    ++generations_[static_cast<std::size_t>(slot)];
    return true;
}

CharacterRecord* EntityWorld::var_record(int slot) {
    if (slot == -1) {
        // The original indexes the NPC table with the unset index -1 (console_character.h).
        return &console::unset_slot(storage());
    }
    return slot >= 0 && slot < kSlotCount ? storage().record(slot) : nullptr;
}

namespace {

const sh::chara::BehaviorField* field_of(EntityWorld::Host* host, const CharacterRecord& r, std::string_view name) {
    if (host == nullptr) {
        return nullptr;
    }
    const sh::chara::BehaviorLayout* own = host->var_layout(r.model.chara_id);
    const sh::chara::BehaviorField* f = own != nullptr ? own->find(name) : nullptr;
    if (f == nullptr) {
        const sh::chara::BehaviorLayout* other = host->var_layout_naming(name);
        f = other != nullptr ? other->find(name) : nullptr;
    }
    if (f == nullptr || f->offset + sh::chara::field_kind_size(f->kind) > kCharacterVarBytes) {
        return nullptr;
    }
    return f;
}

}  // namespace

std::optional<long long> EntityWorld::var(int slot, std::string_view name) {
    CharacterRecord* r = var_record(slot);
    const sh::chara::BehaviorField* f = r != nullptr ? field_of(host_.get(), *r, name) : nullptr;
    if (f == nullptr) {
        return std::nullopt;
    }
    return sh::chara::field_read(f->kind, r->vars.bytes + f->offset);
}

bool EntityWorld::set_var(int slot, std::string_view name, long long value) {
    CharacterRecord* r = var_record(slot);
    const sh::chara::BehaviorField* f = r != nullptr ? field_of(host_.get(), *r, name) : nullptr;
    if (f == nullptr) {
        return false;
    }
    sh::chara::field_write(f->kind, r->vars.bytes + f->offset, value);
    if (slot == -1) {
        console::unset_slot_commit(storage());
    }
    return true;
}

CharacterRef EntityWorld::ref(int slot) const {
    if (slot < 0 || slot >= kSlotCount) {
        return {};
    }
    return CharacterRef{slot, generations_[static_cast<std::size_t>(slot)]};
}

Character* EntityWorld::resolve(const CharacterRef& ref) {
    if (!ref.valid() || ref.slot >= kSlotCount || generations_[static_cast<std::size_t>(ref.slot)] != ref.generation) {
        return nullptr;
    }
    return character(ref.slot);
}

EntityWorld& entity_world() {
    static EntityWorld world;
    return world;
}

}  // namespace sh::entities
