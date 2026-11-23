#!/usr/bin/env python3
"""Validates the "rig" of gamedata/weapons/<Name>.json: the weapon's Harry-animation block the
equip pass installs into HARRY_BASE_ANIM_INFOS[56..75] and D_800C44F0 (GameFs_WeaponInfoUpdate,
player_control.cpp, read through the engine's ItemLibrary). The JSON is the source of truth —
the C++ tables it was generated from are gone (C4 step 2) — so this checks shape and name
resolution instead of regenerating:

  - shape: 18..20 clips (HyperBlaster: 18 — the equip pass leaves the tail slots alone),
    exactly 10 keyframes, the fields of each record, integer keyframes/ranges;
  - names: anim names exist in gamedata/chara/Harry/character.json "anims", sound names in
    gamedata/sounds/sounds.json (null: no sound);
  - packing inputs: the keyframe-slot nibbles are 0..9 (they pack into one byte,
    (attack << 4) | aim_stop, indexing the 10 keyframe records);
  - the ANM disc path exists in the disc's file list (tools/data/disc_files.USA.txt).

--check is the same validation (kept as the gate name). There is no --roundtrip: it compared
the JSON against the deleted C++ tables; the packing itself is covered by the engine reader
and tests/items/weapon_rig_test.cpp. Run from the repository root:

  python3 -I tools/scripts/gen_weapon_rig.py [--check]
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
WEAPONS_DIR = ROOT / "gamedata/weapons"
SOUNDS = ROOT / "gamedata/sounds/sounds.json"
HARRY = ROOT / "gamedata/chara/Harry/character.json"
DISC_FILES = ROOT / "tools/data/disc_files.USA.txt"

KINDS = {"blend_linear", "playback_once", "playback_loop"}
CLIP_KEYS = {"kind", "anim", "active", "variable", "next", "speed_q12", "start_kf", "end_kf"}
KEYFRAME_KEYS = {"status_a", "status_b", "value", "start_kf", "end_kf"}


def load_reference():
    doc = json.loads(SOUNDS.read_text(encoding="utf-8"))["sounds"]
    sounds = set(doc if not isinstance(doc, list) else
                 (entry if isinstance(entry, str) else entry["name"] for entry in doc))
    anims = set(json.loads(HARRY.read_text(encoding="utf-8"))["anims"])
    # The disc's file list: "ANIM/HB_WEP4.ANM", ...
    paths = set(re.findall(r"^\d+ (\S+)$", DISC_FILES.read_text(encoding="utf-8"), re.M))
    return anims, sounds, paths


def check_status(status, where, anims, problems):
    if not isinstance(status, dict) or set(status) != {"anim", "active"}:
        problems.append(f"{where}: a status must be an object with \"anim\" and \"active\"")
        return
    if status["anim"] not in anims:
        problems.append(f"{where}: unknown Harry anim \"{status['anim']}\"")
    if not isinstance(status["active"], bool):
        problems.append(f"{where}: \"active\" must be a boolean")


def check_rig(name, rig, anims, sounds, paths):
    problems = []
    if not isinstance(rig, dict):
        return [f"{name}: \"rig\" must be an object"]

    anim_file = rig.get("anim_file")
    if not isinstance(anim_file, str) or anim_file not in paths:
        problems.append(f"{name}.anim_file: \"{anim_file}\" is not a disc file")

    hand = rig.get("hand_collision_q12")
    if not isinstance(hand, int) or isinstance(hand, bool):
        problems.append(f"{name}.hand_collision_q12: must be an integer (Q.12)")

    clips = rig.get("clips")
    if not isinstance(clips, list) or not 18 <= len(clips) <= 20:
        problems.append(f"{name}.clips: must be 18..20 records (HyperBlaster: 18)")
        clips = clips if isinstance(clips, list) else []
    for i, clip in enumerate(clips):
        where = f"{name}.clips[{i}]"
        if not isinstance(clip, dict) or set(clip) != CLIP_KEYS:
            fields = sorted(clip) if isinstance(clip, dict) else clip
            problems.append(f"{where}: fields {fields} != {sorted(CLIP_KEYS)}")
            continue
        if clip["kind"] not in KINDS:
            problems.append(f"{where}: unknown kind \"{clip['kind']}\"")
        check_status({"anim": clip["anim"], "active": clip["active"]}, where, anims, problems)
        if not isinstance(clip["variable"], bool):
            problems.append(f"{where}: \"variable\" must be a boolean")
        if clip["next"] is not None:
            check_status(clip["next"], f"{where}.next", anims, problems)
        for key in ("speed_q12", "end_kf"):
            if not isinstance(clip[key], int) or isinstance(clip[key], bool):
                problems.append(f"{where}: \"{key}\" must be an integer")
        # start_kf null is "no start" (NO_VALUE); the blend phase of a pair has no own start.
        if clip["start_kf"] is not None and (not isinstance(clip["start_kf"], int)
                                             or isinstance(clip["start_kf"], bool)):
            problems.append(f"{where}: \"start_kf\" must be an integer or null")

    keyframes = rig.get("keyframes")
    if not isinstance(keyframes, list) or len(keyframes) != 10:
        problems.append(f"{name}.keyframes: must be exactly 10 records")
        keyframes = keyframes if isinstance(keyframes, list) else []
    for i, keyframe in enumerate(keyframes):
        where = f"{name}.keyframes[{i}]"
        if not isinstance(keyframe, dict) or set(keyframe) != KEYFRAME_KEYS:
            fields = sorted(keyframe) if isinstance(keyframe, dict) else keyframe
            problems.append(f"{where}: fields {fields} != {sorted(KEYFRAME_KEYS)}")
            continue
        check_status(keyframe["status_a"], f"{where}.status_a", anims, problems)
        check_status(keyframe["status_b"], f"{where}.status_b", anims, problems)
        for key in ("value", "start_kf", "end_kf"):
            if not isinstance(keyframe[key], int) or isinstance(keyframe[key], bool):
                problems.append(f"{where}: \"{key}\" must be an integer")

    sound_section = rig.get("sounds")
    if not isinstance(sound_section, dict) or set(sound_section) != {"attack", "reload", "out_of_ammo"}:
        problems.append(f"{name}.sounds: must hold attack/reload/out_of_ammo")
        sound_section = sound_section if isinstance(sound_section, dict) else {}
    for key, sound in sound_section.items():
        # null (no sound) or a registry name; the engine reads both.
        if sound is not None and sound not in sounds:
            problems.append(f"{name}.sounds.{key}: \"{sound}\" is not in gamedata/sounds/sounds.json")

    anims_section = rig.get("anims")
    if not isinstance(anims_section, dict):
        problems.append(f"{name}.anims: must be an object")
        anims_section = {}
    for key in ("stop_aiming", "attack", "attack_hold"):
        if key in anims_section:
            check_status(anims_section[key], f"{name}.anims.{key}", anims, problems)
        else:
            problems.append(f"{name}.anims.{key}: missing")
    offset = anims_section.get("reload_sound_offset")
    if not isinstance(offset, int) or isinstance(offset, bool):
        problems.append(f"{name}.anims.reload_sound_offset: must be an integer")
    # Optional, for a mod's weapon: Harry's script keeps these per base weapon.
    for key in ("follow_up_keyframes", "aim_hold_keyframes"):
        pair = anims_section.get(key)
        if pair is not None and (not isinstance(pair, list) or len(pair) != 2 or
                                 any(not isinstance(v, int) or isinstance(v, bool) for v in pair)):
            problems.append(f"{name}.anims.{key}: must be two integers (keyframes)")
    for key in ("attack_keyframe_slot", "aim_stop_keyframe_slot"):
        slot = anims_section.get(key)
        # The two slots pack into one byte, (attack << 4) | aim_stop, as indices 0..9 into the
        # ten keyframe records — a wider value would spill into the other nibble.
        if not isinstance(slot, int) or isinstance(slot, bool) or not 0 <= slot <= 9:
            problems.append(f"{name}.anims.{key}: must be an integer 0..9")
    return problems


def main():
    anims, sounds, paths = load_reference()
    problems = []
    weapons = sorted(WEAPONS_DIR.glob("*.json"))
    rigs = 0
    for path in weapons:
        name = path.stem
        try:
            doc = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            problems.append(f"{name}: unreadable ({error})")
            continue
        if not isinstance(doc, dict) or "rig" not in doc:
            continue  # unarmed weapons carry no rig; not this validator's business
        rigs += 1
        problems += check_rig(name, doc["rig"], anims, sounds, paths)
    for problem in problems:
        print(f"weapon rig: {problem}", file=sys.stderr)
    if problems:
        return 1
    print(f"weapon rig: {rigs} rigs valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
