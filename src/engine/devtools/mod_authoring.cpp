// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The editor's mod authoring files (mod_authoring.h, docs/modding/editor.md).

#include "engine/devtools/mod_authoring.h"

#include <cctype>
#include <cstdio>
#include <set>

#include "formats/common/json.h"

namespace sh::devtools {

namespace {

namespace json = sh::json;

constexpr std::string_view kFormat = "sh-editor-objects";
constexpr std::string_view kBegin = "-- editor:begin";
constexpr std::string_view kEnd = "-- editor:end";

json::Value str(std::string_view s) { return json::Value::from_string(std::string(s)); }
json::Value num(std::int64_t n) { return json::Value::from_integer(n); }

// A Lua string literal: only printable ASCII and UTF-8 bytes pass, the rest is escaped.
std::string lua_quote(std::string_view s) {
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += ch;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c < 0x20 || c == 0x7f) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\%03u", c);
            out += buf;
        } else {
            out += ch;
        }
    }
    return out + "\"";
}

bool read_int(const json::Value& o, std::string_view key, std::int32_t* out, std::string* err) {
    const json::Value* v = o.find(key);
    if (v == nullptr) return true;
    if (!v->is_integer() || v->integer() < INT32_MIN || v->integer() > INT32_MAX) {
        *err = "line " + std::to_string(v->line()) + ": '" + std::string(key) + "' must be a Q12 integer";
        return false;
    }
    *out = static_cast<std::int32_t>(v->integer());
    return true;
}

bool read_str(const json::Value& o, std::string_view key, std::string* out, std::string* err) {
    const json::Value* v = o.find(key);
    if (v == nullptr) return true;
    if (!v->is_string()) {
        *err = "line " + std::to_string(v->line()) + ": '" + std::string(key) + "' must be a string";
        return false;
    }
    *out = v->string();
    return true;
}

bool read_bool(const json::Value& o, std::string_view key, bool* out, std::string* err) {
    const json::Value* v = o.find(key);
    if (v == nullptr) return true;
    if (!v->is_bool()) {
        *err = "line " + std::to_string(v->line()) + ": '" + std::string(key) + "' must be true or false";
        return false;
    }
    *out = v->boolean();
    return true;
}

bool is_trigger(ModObjectKind k) { return k != ModObjectKind::Pickup; }

}  // namespace

const char* mod_object_kind_name(ModObjectKind kind) {
    switch (kind) {
        case ModObjectKind::Message:
            return "message";
        case ModObjectKind::Script:
            return "script";
        case ModObjectKind::Spawn:
            return "spawn";
        case ModObjectKind::Pickup:
            return "pickup";
    }
    return "message";
}

bool parse_mod_object_kind(std::string_view text, ModObjectKind* kind) {
    for (ModObjectKind k :
         {ModObjectKind::Message, ModObjectKind::Script, ModObjectKind::Spawn, ModObjectKind::Pickup}) {
        if (text == mod_object_kind_name(k)) {
            *kind = k;
            return true;
        }
    }
    return false;
}

const char* mod_activation_name(ModActivation a) {
    switch (a) {
        case ModActivation::Enter:
            return "enter";
        case ModActivation::Action:
            return "action";
        case ModActivation::Load:
            return "load";
    }
    return "enter";
}

bool parse_mod_activation(std::string_view text, ModActivation* a) {
    for (ModActivation v : {ModActivation::Enter, ModActivation::Action, ModActivation::Load}) {
        if (text == mod_activation_name(v)) {
            *a = v;
            return true;
        }
    }
    return false;
}

bool valid_object_id(std::string_view id) {
    if (id.empty() || id.size() > 48 || std::isdigit(static_cast<unsigned char>(id[0]))) return false;
    for (const char c : id)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    return true;
}

std::string write_map_objects(const ModMapObjects& objects) {
    json::Value root = json::Value::empty_object();
    root.set("format", str(kFormat));
    root.set("version", num(1));
    root.set("description",
             str("Objects the editor placed on this map (docs/modding/editor.md). Positions, radius and height are "
                 "Q12 (4096 = 1 m, y grows downwards), yaw a Q12 angle. The editor writes the mod's runtime, "
                 "messages and flags from this file."));
    root.set("map", str(objects.map));
    json::Value list = json::Value::empty_array();
    for (const ModObject& o : objects.objects) {
        json::Value v = json::Value::empty_object();
        v.set("id", str(o.id));
        v.set("kind", str(mod_object_kind_name(o.kind)));
        v.set("x", num(o.x));
        v.set("y", num(o.y));
        v.set("z", num(o.z));
        v.set("yaw", num(o.yaw));
        if (is_trigger(o.kind)) {
            v.set("radius", num(o.radius));
            v.set("height", num(o.height));
            v.set("activation", str(mod_activation_name(o.activation)));
            v.set("once", json::Value::from_bool(o.once));
        }
        if (!o.flag.empty()) {
            v.set("flag", str(o.flag));
            v.set("flag_value", json::Value::from_bool(o.flag_value));
        }
        switch (o.kind) {
            case ModObjectKind::Message:
                v.set("text", str(o.text));
                break;
            case ModObjectKind::Script:
                break;
            case ModObjectKind::Spawn:
                v.set("chara", str(o.chara));
                v.set("ai", str(o.ai));
                break;
            case ModObjectKind::Pickup:
                v.set("item", str(o.item));
                v.set("count", num(o.count));
                break;
        }
        list.array_mut().push_back(std::move(v));
    }
    root.set("objects", std::move(list));
    return json::write(root, true) + "\n";
}

Result<ModMapObjects> parse_map_objects(std::string_view text) {
    auto parsed = json::parse(text);
    if (!parsed) return Result<ModMapObjects>::fail(parsed.error());
    const json::Value& root = parsed.value();
    const json::Value* format = root.find("format");
    if (format == nullptr || !format->is_string() || format->string() != kFormat)
        return Result<ModMapObjects>::fail("not an editor objects file (format must be \"sh-editor-objects\")");
    ModMapObjects out;
    std::string err;
    if (!read_str(root, "map", &out.map, &err)) return Result<ModMapObjects>::fail(err);
    const json::Value* list = root.find("objects");
    if (list == nullptr) return Result<ModMapObjects>::ok(std::move(out));
    if (!list->is_array()) return Result<ModMapObjects>::fail("'objects' must be an array");
    std::set<std::string> ids;
    for (const json::Value& v : list->array()) {
        if (!v.is_object()) return Result<ModMapObjects>::fail("line " + std::to_string(v.line()) + ": not an object");
        ModObject o;
        std::string kind = "message", activation = "enter";
        if (!read_str(v, "id", &o.id, &err) || !read_str(v, "kind", &kind, &err) || !read_int(v, "x", &o.x, &err) ||
            !read_int(v, "y", &o.y, &err) || !read_int(v, "z", &o.z, &err) || !read_int(v, "yaw", &o.yaw, &err) ||
            !read_int(v, "radius", &o.radius, &err) || !read_int(v, "height", &o.height, &err) ||
            !read_str(v, "activation", &activation, &err) || !read_bool(v, "once", &o.once, &err) ||
            !read_str(v, "flag", &o.flag, &err) || !read_bool(v, "flag_value", &o.flag_value, &err) ||
            !read_str(v, "text", &o.text, &err) || !read_str(v, "chara", &o.chara, &err) ||
            !read_str(v, "ai", &o.ai, &err) || !read_str(v, "item", &o.item, &err)) {
            return Result<ModMapObjects>::fail(err);
        }
        std::int32_t count = 0;
        if (!read_int(v, "count", &count, &err)) return Result<ModMapObjects>::fail(err);
        o.count = count;
        const std::string at = "line " + std::to_string(v.line()) + ": ";
        if (!valid_object_id(o.id)) return Result<ModMapObjects>::fail(at + "bad object id '" + o.id + "'");
        if (!ids.insert(o.id).second) return Result<ModMapObjects>::fail(at + "object id '" + o.id + "' repeats");
        if (!parse_mod_object_kind(kind, &o.kind)) return Result<ModMapObjects>::fail(at + "unknown kind " + kind);
        if (!parse_mod_activation(activation, &o.activation))
            return Result<ModMapObjects>::fail(at + "unknown activation " + activation);
        out.objects.push_back(std::move(o));
    }
    return Result<ModMapObjects>::ok(std::move(out));
}

std::string object_text_key(std::string_view mod, std::string_view map, std::string_view id) {
    return std::string(mod) + ":" + std::string(map) + "." + std::string(id);
}

std::string object_done_flag(const ModObject& o) {
    if (o.kind == ModObjectKind::Pickup) return o.id + "_taken";
    return o.once ? o.id + "_done" : std::string();
}

std::string object_event(std::string_view mod, std::string_view id) { return std::string(mod) + ":" + std::string(id); }

std::string write_text_doc(std::string_view mod, const ModMapObjects& objects) {
    json::Value strings = json::Value::empty_object();
    for (const ModObject& o : objects.objects)
        if (o.kind == ModObjectKind::Message) strings.set(object_text_key(mod, objects.map, o.id), str(o.text));
    if (strings.members().empty()) return {};
    json::Value root = json::Value::empty_object();
    root.set("format", str("sh-text"));
    root.set("version", num(1));
    root.set("lang", str("en"));
    root.set("map", str(objects.map));
    root.set("description", str("Messages of the editor's objects on this map; written from editor_objects.json."));
    root.set("strings", std::move(strings));
    return json::write(root, true) + "\n";
}

std::string write_flags_doc(std::string_view mod, const std::vector<ModMapObjects>& maps) {
    (void)mod;
    json::Value flags = json::Value::empty_object();
    for (const ModMapObjects& m : maps) {
        for (const ModObject& o : m.objects) {
            const std::string f = object_done_flag(o);
            if (f.empty()) continue;
            json::Value d = json::Value::empty_object();
            d.set("description", str(std::string(o.kind == ModObjectKind::Pickup ? "Taken: " : "Done: ") + "the " +
                                     mod_object_kind_name(o.kind) + " '" + o.id + "' on " + m.map + "."));
            flags.set(f, std::move(d));
        }
    }
    if (flags.members().empty()) return {};
    json::Value root = json::Value::empty_object();
    root.set("format", str("sh-flags"));
    root.set("version", num(1));
    root.set("description", str("Flags of the editor's objects; written from maps/*/editor_objects.json."));
    root.set("flags", std::move(flags));
    return json::write(root, true) + "\n";
}

std::string runtime_region(std::string_view mod, const std::vector<ModMapObjects>& maps) {
    const std::string ns = std::string(mod) + ":";
    std::string s(kBegin);
    s +=
        "\n-- Written by the editor's Mod panel from maps/*/editor_objects.json: edits inside this region are lost on\n"
        "-- the next save. Code outside the two marker lines is kept.\n"
        "local EDITOR_OBJECTS = {\n";
    for (const ModMapObjects& m : maps) {
        if (m.objects.empty()) continue;
        s += "    [" + lua_quote(m.map) + "] = {\n";
        for (const ModObject& o : m.objects) {
            char pos[160];
            std::snprintf(pos, sizeof pos, "x = %d, y = %d, z = %d, yaw = %d, r = %d, h = %d", o.x, o.y, o.z, o.yaw,
                          o.radius, o.height);
            s += "        { id = " + lua_quote(o.id) + ", kind = " + lua_quote(mod_object_kind_name(o.kind)) + ", " +
                 pos + ", on = " + lua_quote(mod_activation_name(o.activation));
            if (!o.flag.empty())
                s += ", flag = " + lua_quote(o.flag) + ", flag_value = " + (o.flag_value ? "true" : "false");
            const std::string done = object_done_flag(o);
            if (!done.empty()) s += ", done = " + lua_quote(ns + done);
            switch (o.kind) {
                case ModObjectKind::Message:
                    s += ", text = " + lua_quote(object_text_key(mod, m.map, o.id));
                    break;
                case ModObjectKind::Script:
                    s += ", event = " + lua_quote(object_event(mod, o.id));
                    break;
                case ModObjectKind::Spawn:
                    s += ", chara = " + lua_quote(o.chara) + ", ai = " + lua_quote(o.ai);
                    break;
                case ModObjectKind::Pickup:
                    s += ", item = " + lua_quote(o.item) + ", count = " + std::to_string(o.count);
                    break;
            }
            s += " },\n";
        }
        s += "    },\n";
    }
    s += "}\n";
    s += R"lua(
do
    local inside, hold = {}, 0

    local function flag_is(name, want)
        local ok, v = pcall(sh.flag.get, name)
        if not ok then
            sh.log(1, mod.id .. ": " .. tostring(v))
            return false
        end
        return v == want
    end

    local function allowed(o)
        if o.flag and not flag_is(o.flag, o.flag_value) then return false end
        if o.done and o.kind ~= "pickup" and flag_is(o.done, true) then return false end
        return true
    end

    local function fire(o, map)
        if o.done then sh.flag.set(o.done, true) end
        sh.log(string.format("%s: %s %s fired on %s", mod.id, o.kind, o.id, map))
        if o.kind == "message" then
            -- The game shows it like a message event of the map; the press that closes it must not fire again.
            local ok, err = sh.world.message(o.text)
            if not ok then sh.log(1, mod.id .. ": " .. tostring(err)) end
            hold = 15
        elseif o.kind == "script" then
            sh.hooks.emit(o.event, { object = o.id, map = map })
        elseif o.kind == "spawn" then
            local slot, err = sh.entity.spawn{ chara = o.chara, x = o.x, z = o.z, rot = o.yaw, ai = o.ai }
            if slot then
                sh.log(string.format("%s: %s spawned in slot %d", mod.id, o.chara, slot))
            else
                sh.log(1, mod.id .. ": spawn " .. o.id .. ": " .. tostring(err))
            end
        end
    end

    sh.hooks.on("map.load", function(ev)
        inside, hold = {}, 0
        for _, o in ipairs(EDITOR_OBJECTS[ev.map] or {}) do
            if o.kind == "pickup" then
                if o.flag == nil or flag_is(o.flag, o.flag_value) then
                    local id, err = sh.world.pickup_create{ item = o.item, id = o.id, flag = o.done,
                        count = o.count > 0 and o.count or nil, x = o.x, y = o.y, z = o.z, rot = o.yaw }
                    if id then
                        sh.log(string.format("%s: pickup %s (%s)%s", mod.id, id, o.item,
                            flag_is(o.done, true) and " already taken" or ""))
                    else
                        sh.log(1, mod.id .. ": pickup " .. o.id .. ": " .. tostring(err))
                    end
                end
            elseif o.on == "load" and allowed(o) then
                fire(o, ev.map)
            end
        end
    end, { id = "editor_objects" })

    sh.hooks.on("game.tick", function()
        local map = sh.map.name()
        local list = EDITOR_OBJECTS[map]
        if list == nil then return end
        -- Only while the player controls Harry: events and cutscenes take the control away.
        if sh.entity.player_control_disabled() ~= 0 then return end
        if hold > 0 then hold = hold - 1 end
        local px, py, pz = sh.player.pos()
        local action = hold == 0 and sh.input.action("action", 1)
        for _, o in ipairs(list) do
            if o.kind ~= "pickup" and o.on ~= "load" then
                local dx, dz = px - o.x, pz - o.z
                local now = dx * dx + dz * dz <= o.r * o.r and math.abs(py - o.y) <= o.h
                local was = inside[o.id]
                inside[o.id] = now
                if now and ((o.on == "enter" and not was) or (o.on == "action" and action)) and allowed(o) then
                    fire(o, map)
                end
            end
        end
    end, { id = "editor_objects" })
end
)lua";
    s += std::string(kEnd) + "\n";
    return s;
}

std::string merge_main_lua(std::string_view existing, std::string_view region) {
    const std::size_t b = existing.find(kBegin);
    if (b != std::string_view::npos) {
        std::size_t e = existing.find(kEnd, b);
        if (e != std::string_view::npos) {
            e += kEnd.size();
            if (e < existing.size() && existing[e] == '\n') ++e;
            return std::string(existing.substr(0, b)) + std::string(region) + std::string(existing.substr(e));
        }
    }
    if (existing.empty()) return std::string(region);
    std::string out(existing);
    if (out.back() != '\n') out += '\n';
    return out + "\n" + std::string(region);
}

std::string add_script_stub(std::string_view existing, std::string_view mod, std::string_view map,
                            std::string_view id) {
    const std::string event = object_event(mod, id);
    if (existing.find(lua_quote(event)) != std::string_view::npos) return std::string(existing);
    std::string out(existing);
    if (out.empty()) {
        out = "-- " + std::string(mod) + ": a layer over maps/" + std::string(map) +
              "/hooks.lua. The editor's script objects of this map\n-- send the events below when they fire "
              "(docs/modding/editor.md).\n";
    } else if (out.back() != '\n') {
        out += '\n';
    }
    out += "\n-- The script trigger '" + std::string(id) + "' (ev.object, ev.map).\n";
    out += "sh.hooks.on(" + lua_quote(event) + ", function(ev)\n";
    out += "    sh.log(\"" + std::string(mod) + ": " + std::string(id) + " runs on \" .. ev.map)\n";
    out += "end, { id = " + lua_quote(id) + " })\n";
    return out;
}

std::string new_manifest(std::string_view id, std::string_view name, std::string_view engine_version) {
    json::Value root = json::Value::empty_object();
    root.set("format", num(1));
    root.set("id", str(id));
    root.set("name", str(name.empty() ? id : name));
    root.set("version", str("0.1.0"));
    root.set("description", str("Made with the editor's Mod panel."));
    std::string major(engine_version.substr(0, engine_version.find('.')));
    root.set("engine", str(">=" + std::string(engine_version) + " <" + std::to_string(std::stoi(major) + 1) + ".0.0"));
    root.set("api", str("^1.0"));
    root.set("gameplay", json::Value::from_bool(true));
    root.set("scripts", json::Value::from_bool(true));
    return json::write(root, true) + "\n";
}

}  // namespace sh::devtools
