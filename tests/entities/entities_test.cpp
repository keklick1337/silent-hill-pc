#include "framework/sh_disc.h"
#include "framework/sh_test.h"
#include "engine/chara/chara_library.h"
#include "engine/entities/anim_player.h"
#include "engine/entities/animation_set.h"
#include "engine/entities/character.h"
#include "engine/entities/entity_world.h"
#include "engine/entities/fixed_transform.h"
#include "formats/convert/anim/anim_json.h"
#include "formats/convert/anim/anm_file.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The entity module (src/engine/entities): fixed-point bone products, the animation step, sampling and world
// transforms of every animation of the disc, Character and EntityWorld.

namespace {

namespace ent = sh::entities;
namespace an = sh::convert::anim;

ent::BoneTransform rot_z(std::int16_t c, std::int16_t s, std::int32_t tx, std::int32_t ty, std::int32_t tz) {
    ent::BoneTransform t;
    t.rotation = {c, static_cast<std::int16_t>(-s), 0, s, c, 0, 0, 0, 4096};
    t.translation = {tx, ty, tz};
    return t;
}

}  // namespace

SH_TEST(entities_compose_identity_and_known_product) {
    const ent::BoneTransform a = rot_z(2896, 2896, 100, -200, 300);  // 45 degrees
    SH_CHECK(ent::compose(ent::kIdentityTransform, a) == a);
    const ent::BoneTransform b = ent::compose(a, a);
    // 90 degrees: (r0 . c0) = c*c - s*s = 0, (r1 . c0) = 2*2896*2896 >> 12 = 4095, and the negative entry
    // rounds down: -16773632 >> 12 = -4096.
    SH_CHECK(b.rotation[0] == 0);
    SH_CHECK(b.rotation[3] == 4095);
    SH_CHECK(b.rotation[1] == -4096);
    // t = a.t + R(a) * a.t: x = 100 + (2896*100 - 2896*-200) >> 12 = 100 + 212 = 312 (868800 >> 12 = 212).
    SH_CHECK(b.translation[0] == 312);
    // y = -200 + (2896*100 + 2896*-200) >> 12 = -200 + (-289600 >> 12 = -71) = -271.
    SH_CHECK(b.translation[1] == -271);
    SH_CHECK(b.translation[2] == 600);
}

SH_TEST(entities_compose_saturates_rotation_and_takes_low_16_bits_of_the_local_translation) {
    ent::BoneTransform big;
    big.rotation = {32767, 0, 0, 0, 32767, 0, 0, 0, 32767};
    ent::BoneTransform r = ent::compose(big, big);  // 32767 * 32767 >> 12 = 262128 -> 32767
    SH_CHECK(r.rotation[0] == 32767);
    ent::BoneTransform neg = big;
    neg.rotation[0] = -32768;
    r = ent::compose(neg, big);
    SH_CHECK(r.rotation[0] == -32768);
    ent::BoneTransform local;
    local.translation = {0x10005, -0x10003, 70000};  // low 16 bits: 5, -3, 70000 - 65536 = 4464
    r = ent::compose(ent::kIdentityTransform, local);
    SH_CHECK(r.translation[0] == 5);
    SH_CHECK(r.translation[1] == -3);
    SH_CHECK(r.translation[2] == 4464);
    // The parent's translation is taken whole.
    ent::BoneTransform parent;
    parent.translation = {1000000, 0, 0};
    r = ent::compose(parent, ent::kIdentityTransform);
    SH_CHECK(r.translation[0] == 1000000);
}

SH_TEST(entities_step_once_ends_and_links) {
    ent::ClipEntry e{ent::ClipKind::Once, 7, 10, 12, 0};
    ent::AnimState s;
    s.status = 5;
    s.flags = ent::kAnimAdvances;
    s.time = 10 * 4096;
    auto r = ent::step_animation_fixed(s, e, 6000);
    SH_CHECK(r.sample && !r.linked);
    SH_CHECK(r.keyframe0 == 11 && r.keyframe1 == 12 && r.alpha == ((10 * 4096 + 6000) & 0xFFF));
    SH_CHECK(s.keyframe == 11 && s.status == 5 && s.alpha == 0);
    r = ent::step_animation_fixed(s, e, 6000);
    SH_CHECK(r.linked && s.time == 12 * 4096 && s.status == 7);
    // Backwards: stops at the start.
    ent::AnimState b;
    b.flags = ent::kAnimAdvances;
    b.time = 10 * 4096 + 100;
    r = ent::step_animation_fixed(b, e, -500);
    SH_CHECK(r.linked && b.time == 10 * 4096);
    // No advance: the pose holds, and only a posed or advancing animation samples.
    ent::AnimState h;
    h.time = 11 * 4096 + 7;
    r = ent::step_animation_fixed(h, e, 0);
    SH_CHECK(!r.sample && h.time == 11 * 4096 + 7 && !r.linked);
    // No next status: stays.
    ent::ClipEntry last{ent::ClipKind::Once, ent::kNoNextStatus, 0, 1, 0};
    ent::AnimState n;
    n.status = 3;
    n.flags = ent::kAnimAdvances;
    ent::step_animation_fixed(n, last, 9000);
    SH_CHECK(n.status == 3);
}

SH_TEST(entities_step_loop_wraps_and_pulls_outside_times_in) {
    ent::ClipEntry e{ent::ClipKind::Loop, ent::kNoNextStatus, 4, 7, 0};  // keyframes 4..7, 4 long
    ent::AnimState s;
    s.flags = ent::kAnimAdvances | ent::kAnimPosed;
    s.time = 7 * 4096 + 4000;
    auto r = ent::step_animation_fixed(s, e, 200);  // 8*4096 + 104 -> wraps to 4*4096 + 104
    SH_CHECK(s.time == 4 * 4096 + 104 && r.keyframe0 == 4 && r.keyframe1 == 5 && r.alpha == 104);
    s.time = 7 * 4096 + 10;
    r = ent::step_animation_fixed(s, e, 0);
    SH_CHECK(r.keyframe0 == 7 && r.keyframe1 == 4);  // the last keyframe blends into the first
    s.time = 40 * 4096;  // past the loop: pulled to its last keyframe, not wrapped
    ent::step_animation_fixed(s, e, 0);
    SH_CHECK(s.time == 7 * 4096);
    s.time = 0;  // before: pulled to the start
    ent::step_animation_fixed(s, e, 0);
    SH_CHECK(s.time == 4 * 4096);
    // Start "current keyframe".
    ent::ClipEntry cur{ent::ClipKind::Loop, ent::kNoNextStatus, ent::kCurrentKeyframe, 9, 0};
    ent::AnimState c;
    c.keyframe = 6;
    c.time = 6 * 4096;
    ent::step_animation_fixed(c, cur, 3 * 4096 + 5);  // 9*4096+5 -> next start 10: 9*4096 + 5 stays
    SH_CHECK(c.time == 9 * 4096 + 5);
    // The timestep is read after the pull into range.
    ent::AnimState t;
    t.time = 99 * 4096;
    std::int32_t seen = -1;
    ent::step_animation(t, e, [&] {
        seen = t.time;
        return 0;
    });
    SH_CHECK(seen == 7 * 4096);
}

SH_TEST(entities_step_blend_ends_on_the_end_keyframe) {
    ent::ClipEntry e{ent::ClipKind::Blend, 9, ent::kCurrentKeyframe, 20, 0};
    ent::AnimState s;
    s.flags = ent::kAnimAdvances;
    s.keyframe = 3;
    auto r = ent::step_animation_fixed(s, e, 1500);
    SH_CHECK(!r.linked && r.keyframe0 == 3 && r.keyframe1 == 20 && r.alpha == 1500 && s.time == 3 * 4096);
    r = ent::step_animation_fixed(s, e, 1500);  // 3000 >= half: time on the end keyframe
    SH_CHECK(s.time == 20 * 4096 && s.alpha == 3000 && s.keyframe == 3);
    r = ent::step_animation_fixed(s, e, 1500);  // 4500: done
    SH_CHECK(r.linked && s.status == 9 && s.keyframe == 20 && s.alpha == 0 && r.keyframe0 == 20 && r.alpha == 0);
    // Negative current keyframe: hold the end.
    ent::AnimState n;
    n.keyframe = -1;
    r = ent::step_animation_fixed(n, e, 0);
    SH_CHECK(r.keyframe0 == 20);
    SH_CHECK(ent::anim_timestep(4096 * 30, 136, ent::kAnimAdvances) == 30 * 136);
    SH_CHECK(ent::anim_timestep(4096 * 30, 136, 0) == 0);
}

SH_TEST(entities_keyframe_crossed) {
    SH_CHECK(ent::keyframe_crossed(4 * 4096 + 100, 5 * 4096 + 1, 5));
    SH_CHECK(!ent::keyframe_crossed(4 * 4096 + 100, 4 * 4096 + 900, 5));
    SH_CHECK(!ent::keyframe_crossed(4 * 4096, 6 * 4096, 4));
    SH_CHECK(ent::keyframe_crossed(6 * 4096, 3 * 4096, 4));  // backwards
}

// Every animation of the disc: sampling and world transforms of the engine against the game's arithmetic as the
// ANM reader implements it (AnmFile::local_poses / world_poses), on every keyframe pair at three alphas.
SH_TEST(entities_every_disc_animation_samples_like_the_game) {
    const auto disc = sh::test::open_game_disc();
    SH_CHECK(disc.has_value());
    if (!disc) {
        return;
    }
    std::optional<an::AnmFile> base;
    if (const auto raw = disc->read("ANIM/HB_BASE.ANM")) {
        auto f = an::AnmFile::parse(*raw);
        if (f) {
            base = f.value();
        }
    }
    SH_CHECK(base.has_value());
    std::size_t files = 0, poses = 0, bones = 0, local_diff = 0, world_diff = 0, max_bones = 0;
    for (const auto& e : disc->table) {
        if (e.name.size() < 4 || e.name.compare(e.name.size() - 4, 4, ".ANM") != 0) {
            continue;
        }
        const auto raw = disc->read(e.name);
        if (!raw || raw->empty()) {
            continue;
        }
        auto file = an::AnmFile::has_header(*raw) ? an::AnmFile::parse(*raw)
                    : base                        ? an::AnmFile::parse_extension(*raw, *base)
                                                  : sh::Result<an::AnmFile>::fail("no base");
        if (!file) {
            continue;
        }
        // Through the import's JSON, as the game loads it.
        const std::string json = an::anm_to_json(file.value(), e.name, *raw, file.value().extension() ? "ANIM/HB_BASE.ANM" : "");
        auto set = ent::AnimationSet::parse_json(json);
        SH_CHECK(set);
        if (!set) {
            std::printf("  %s: %s\n", e.name.c_str(), set.error().c_str());
            continue;
        }
        ++files;
        const ent::Skeleton& sk = set.value().skeleton();
        SH_CHECK(set.value().game_path() == e.name);
        SH_CHECK(sk.bone_count() == file.value().bones().size());
        max_bones = std::max(max_bones, sk.bone_count());
        const int count = set.value().keyframes().count;
        SH_CHECK(static_cast<std::size_t>(count) == file.value().keyframes().size());
        for (int k = 0; k + 1 < count; ++k) {
            for (const std::int32_t alpha : {0, 1365, 4095}) {
                std::vector<ent::BoneTransform> pose(sk.bone_count());
                ent::apply_rest_pose(sk, pose);
                ent::SampleRule all;
                all.bones = ~0u;
                SH_CHECK(ent::sample_pose(sk, set.value().keyframe(k), set.value().keyframe(k + 1), alpha, all, pose));
                const auto ref = file.value().local_poses(static_cast<std::size_t>(k), static_cast<std::size_t>(k + 1), alpha);
                ++poses;
                for (std::size_t b = 1; b < pose.size(); ++b) {
                    ++bones;
                    bool same = true;
                    for (std::size_t c = 0; c < 9; ++c) {
                        same = same && pose[b].rotation[c] == ref[b].m[c];
                    }
                    for (std::size_t c = 0; c < 3; ++c) {
                        same = same && pose[b].translation[c] == ref[b].t[c];
                    }
                    local_diff += same ? 0 : 1;
                }
                std::vector<ent::BoneTransform> world(pose.size());
                ent::compute_world(sk, pose, world);
                const auto ref_world = file.value().world_poses(ref);
                for (std::size_t b = 1; b < world.size(); ++b) {
                    bool same = true;
                    for (std::size_t c = 0; c < 9; ++c) {
                        same = same && world[b].rotation[c] == ref_world[b].m[c];
                    }
                    for (std::size_t c = 0; c < 3; ++c) {
                        same = same && world[b].translation[c] == ref_world[b].t[c];
                    }
                    world_diff += same ? 0 : 1;
                }
            }
        }
    }
    std::printf("  %zu files, %zu poses, %zu bones, local mismatches %zu, world mismatches %zu, max bones %zu\n", files,
                poses, bones, local_diff, world_diff, max_bones);
    SH_CHECK(files >= 100);
    SH_CHECK(local_diff == 0);
    SH_CHECK(world_diff == 0);
    SH_CHECK(max_bones > 32);  // bones past 31 share mask bits (bit i mod 32)
}

SH_TEST(entities_keyframe_layout_places_keyframe_files_behind_the_base) {
    auto make = [](int count, std::int8_t fill) {
        an::AnmHeader h;
        h.data_offset = 0x14 + 2 * 6;
        h.translation_slots = 1;
        h.rotation_slots = 0;
        h.keyframe_size = 3;
        h.bone_count = 2;
        h.active_bones = 3;
        h.keyframe_count = static_cast<std::uint16_t>(count);
        h.file_size = static_cast<std::uint32_t>(h.data_offset + count * 3);
        std::vector<an::AnmBone> bones(2);
        bones[1].parent = 0;
        bones[1].translation = 0;
        std::vector<an::AnmKeyframe> keys(static_cast<std::size_t>(count));
        for (int k = 0; k < count; ++k) {
            keys[static_cast<std::size_t>(k)].translations.push_back({fill, static_cast<std::int8_t>(k), 0});
        }
        auto f = an::AnmFile::from_parts(false, h, bones, {}, keys, {});
        return std::make_shared<const ent::AnimationSet>(ent::AnimationSet::from_anm(f.value(), "T").value());
    };
    ent::KeyframeLayout layout;
    layout.place(make(4, 1), 0);
    layout.place(make(3, 2), 10);
    SH_CHECK(layout.keyframe(2)[0] == 1 && layout.keyframe(2)[1] == 2);
    SH_CHECK(layout.keyframe(11)[0] == 2 && layout.keyframe(11)[1] == 1);
    SH_CHECK(layout.keyframe(5).empty());
    SH_CHECK(layout.keyframe(13).empty());
}

SH_TEST(entities_character_plays_named_clips_and_writes_its_record) {
    std::vector<std::string> warnings;
    auto lib = sh::chara::CharaLibrary::parse(
        R"({"characters": [{"id": 4, "enum": "Chara_Groaner", "sounds": {"death": 1414},
             "params": {"health_base": 210.0}, "anims": {"still": 0, "run_forward": 16}}]})",
        warnings);
    SH_CHECK(lib);
    ent::Character c(1, 4, lib.value().find(4));
    SH_CHECK(c.chara_id() == 4 && !c.bound());
    SH_CHECK(c.clip_index("run_forward") == 16);
    SH_CHECK(c.play("run_forward"));
    SH_CHECK(c.anim_state().status == 32 && c.playing_clip() == 16 && c.blending());
    SH_CHECK(!c.play("no_such_clip"));
    c.play(16, false);
    SH_CHECK(c.anim_state().status == 33 && !c.blending());
    c.set_position({4096, 0, -8192});
    c.set_health(c.param_q12("health_base", 0));
    SH_CHECK(c.health() == 210 * 4096);
    SH_CHECK(c.sound("death") == 1414 && c.sound("nope", 7) == 7);

    // Bound to a slot record, every accessor reads and writes the record itself.
    ent::CharacterRecord record{};
    record.model.chara_id = 4;
    record.flags = static_cast<std::int16_t>(ent::kFlagHit);
    record.model.anim.flags = 0x40;  // a bit AnimState does not carry
    c.bind(3, &record, lib.value().find(4));
    SH_CHECK(c.bound() && c.slot() == 3 && c.health() == 0);
    c.set_position({1, 2, 3});
    c.set_rotation({0, 1024, 0});
    c.set_health(5);
    c.set_move_speed(77);
    c.set_heading(-12);
    c.set_fall_speed(9);
    c.set_control(2, 5);
    c.set_flag(ent::kFlagDowned, true);
    c.set_flag(ent::kFlagHit, false);
    c.add_damage(100, {1, 0, -1});
    c.add_damage(20, {1, 0, -1});
    c.set_anim_state({7, ent::kAnimAdvances, 3 << 12, 3, 4095});
    SH_CHECK(record.position == (ent::Vec3{1, 2, 3}) && record.rotation.y == 1024 && record.health == 5);
    SH_CHECK(record.move_speed == 77 && record.heading == -12 && record.fall_speed == 9);
    SH_CHECK(record.model.control_state == 2 && record.model.control_step == 5);
    SH_CHECK(record.flags == ent::kFlagDowned && c.has_flag(ent::kFlagDowned) && !c.has_flag(ent::kFlagHit));
    SH_CHECK(record.damage.amount == 120 && record.damage.push == (ent::Vec3{2, 0, -2}));
    SH_CHECK(record.model.anim.status == 7 && record.model.anim.time == 3 << 12 && record.model.anim.alpha == 4095);
    SH_CHECK(record.model.anim.flags == (0x40 | ent::kAnimAdvances));
    record.partner = 4;
    SH_CHECK(c.attacker().slot == 5 && c.spawn_index() == 4);
    c.set_attacker({});
    SH_CHECK(record.partner == -1 && !c.attacker().valid());
    record.collision.state_and_count = 0x3F;  // state -1, three records
    SH_CHECK(c.collision().state() == -1);
    c.collision().set_state(2);
    SH_CHECK(record.collision.state_and_count == 0x32);
}

// The bones of a bound character are the game's transforms: reading, writing and sampling go to them.
SH_TEST(entities_character_poses_bound_bones) {
    ent::BoneCoord bones[2] = {};
    bones[1].local.t[0] = 11;
    bones[1].local.m[0][0] = 4096;
    bones[1].world_current = 1;
    for (int i = 0; i < 3; ++i) {
        bones[0].local.m[i][i] = 4096;  // bone 0 (the character) at the origin, unrotated
    }
    ent::CharacterRecord record{};
    record.model.chara_id = 1;
    ent::Character c;
    c.bind(0, &record, nullptr);
    c.bind_bones(bones, 2, nullptr);
    SH_CHECK(c.bone_count() == 2);
    SH_CHECK(c.bone_local(1).translation[0] == 11 && c.bone_local(1).rotation[0] == 4096);
    ent::BoneTransform t;
    t.translation = {5, 6, 7};
    c.set_bone_local(1, t);
    SH_CHECK(bones[1].local.t[2] == 7 && bones[1].world_current == 0);
    c.update_world();
    SH_CHECK(c.bone_world_position(1)[1] == 6);
}

// EntityWorld binds characters to the slot storage and keeps references by generation.
SH_TEST(entities_world_binds_slot_records) {
    struct Host final : ent::EntityWorld::Host {
        const sh::chara::CharacterDef* def(int) override { return nullptr; }
        const sh::chara::BehaviorLayout* var_layout(int) override { return nullptr; }
        const sh::chara::BehaviorLayout* var_layout_naming(std::string_view) override { return nullptr; }
        ent::EntityWorld::BoneBinding bones(int) override { return {}; }
        int create(int, const ent::Vec3&, const ent::Angles&, int) override { return 7; }
        bool remove(int slot) override { return slot == 7; }
    };
    auto slots = std::make_unique<ent::CharacterSlots>();
    ent::EntityWorld world;
    world.set_storage(slots.get());
    world.set_host(std::make_unique<Host>());
    SH_CHECK(world.character(7) == nullptr);
    slots->npcs[6].model.chara_id = 4;
    slots->npcs[6].health = 99;
    ent::Character* c = world.character(7);
    SH_CHECK(c != nullptr && c->health() == 99 && &c->record() == &slots->npcs[6]);
    c->set_health(1);
    SH_CHECK(slots->npcs[6].health == 1);
    SH_CHECK(slots->slot_of(&slots->npcs[6]) == 7 && slots->slot_of(&slots->player) == 0);
    const ent::CharacterRef ref = world.ref(7);
    SH_CHECK(world.resolve(ref) == c);
    SH_CHECK(world.remove(7));
    SH_CHECK(world.resolve(ref) == nullptr);
    SH_CHECK(!world.var(7, "anything").has_value());
}

SH_TEST(entities_character_ticks_its_clip_table) {
    an::AnmHeader h;
    h.data_offset = 0x14 + 2 * 6;
    h.translation_slots = 1;
    h.keyframe_size = 3;
    h.bone_count = 2;
    h.active_bones = 2;
    h.keyframe_count = 3;
    h.file_size = h.data_offset + 9;
    std::vector<an::AnmBone> bones(2);
    bones[1].parent = 0;
    bones[1].translation = 0;
    std::vector<an::AnmKeyframe> keys(3);
    for (int k = 0; k < 3; ++k) {
        keys[static_cast<std::size_t>(k)].translations.push_back({static_cast<std::int8_t>(k * 10), 0, 0});
    }
    auto f = an::AnmFile::from_parts(false, h, bones, {}, keys, {});
    SH_CHECK(f);
    ent::Character c(0, 1, nullptr);
    c.attach_animation(std::make_shared<const ent::AnimationSet>(ent::AnimationSet::from_anm(f.value(), "X").value()));
    c.clip_table() = {ent::ClipEntry{ent::ClipKind::Loop, ent::kNoNextStatus, 0, 2, 4096 * 30}};
    ent::AnimState s;
    s.flags = ent::kAnimAdvances;
    c.set_anim_state(s);
    const auto r = c.tick(2048);  // half a second at 30 keyframes/s: 15 keyframes, wraps mod 3 -> 0
    SH_CHECK(r.sample && c.anim_state().time == 0);
    c.set_anim_state({0, ent::kAnimAdvances, 0, 0, 0});
    c.tick(68);  // 30 * 68 = 2040 -> keyframe 0 at alpha 2040: x = 0 + (10 * 2040 >> 12) = 4
    SH_CHECK(c.pose()[1].translation[0] == 4);
    c.update_world();
    SH_CHECK(c.bone_world_position(1)[0] == 4);
    SH_CHECK(c.bone_world_position(9)[0] == 0);
}

// More than the original's 32 room slots: the world addresses every NPC slot up to its capacity.
SH_TEST(entities_world_addresses_more_than_32_npc_slots) {
    struct Fake final : ent::EntityWorld::Host {
        ent::CharacterSlots* slots = nullptr;
        int created = 0;
        const sh::chara::CharacterDef* def(int) override { return nullptr; }
        const sh::chara::BehaviorLayout* var_layout(int) override { return nullptr; }
        const sh::chara::BehaviorLayout* var_layout_naming(std::string_view) override { return nullptr; }
        ent::EntityWorld::BoneBinding bones(int) override { return {}; }
        int create(int id, const ent::Vec3&, const ent::Angles&, int) override {
            if (created >= ent::EntityWorld::kSlotCount - 1) {
                return -1;
            }
            slots->npcs[created].model.chara_id = static_cast<std::int8_t>(id);
            return ++created;
        }
        bool remove(int slot) override { return slot >= 1 && slot <= created; }
    };
    auto slots = std::make_unique<ent::CharacterSlots>();
    ent::EntityWorld world;
    world.set_storage(slots.get());
    auto fake = std::make_unique<Fake>();
    Fake* f = fake.get();
    f->slots = slots.get();
    world.set_host(std::move(fake));
    SH_CHECK(ent::EntityWorld::kSlotCount > 33);
    int last = 0;
    for (int i = 0; i < 100; ++i) {
        last = world.create(4, ent::Vec3{0, 0, 0}, ent::Angles{0, 0, 0}, 3);
        SH_CHECK(last == i + 1);
    }
    SH_CHECK(f->created == 100);
    SH_CHECK(world.character(100) != nullptr);  // well past the 32nd
    SH_CHECK(world.character(101) == nullptr);
    SH_CHECK(world.remove(100));
}
