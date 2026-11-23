// The engine sound family (bind_sound.cpp): every sh.sound.* name the named bridge served is a
// function, calls reach the installed SoundHost/MusicHost with the bridge's argument rules, and
// a position is a reference string, a {x, y, z} table or nil.

#include "framework/sh_test.h"

#include "engine/script/host_interfaces.h"
#include "engine/script/script_host.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sh::script::Host;
using sh::script::HostConfig;

struct Recorder final : sh::script::SoundHost, sh::script::MusicHost {
    std::vector<std::string> calls;
    int flags = 0;
    void rec(const std::string& s) { calls.push_back(s); }
    static std::string pos(const int* p) {
        return p == nullptr ? "nil" : std::to_string(p[0]) + "," + std::to_string(p[1]) + "," + std::to_string(p[2]);
    }

    void play(int) override {}
    void stop(int id) override { rec("stop " + std::to_string(id)); }
    void play_3d(int, int) override {}
    void play_3d_at(int, int, int, int, int) override {}
    void play_flags(int, int, int, int, int, int) override {}
    void play_flags_pitch(int, int, int, int, int, int, int) override {}
    void play_pitch(int, int, int, int, int, int) override {}
    void play_at_pos(int, int, int, int, int, int) override {}
    void play_falloff_pitch(int, int, int, int, int, int, int) override {}
    void sd_call(int c) override { rec("sd " + std::to_string(c)); }
    int sd_command_number(const char*) override { return -1; }
    void attributes_update(int id, int b, int v, int p) override {
        rec("attr " + std::to_string(id) + " " + std::to_string(b) + " " + std::to_string(v) + " " + std::to_string(p));
    }
    int play_balanced(int id, int b, int v) override {
        rec("play " + std::to_string(id) + " " + std::to_string(b) + " " + std::to_string(v));
        return 7;
    }
    int position_ref(const char* ref, int out[3], char* err, int errCap) override {
        if (std::strcmp(ref, "vec:0") == 0) {
            out[0] = 1;
            out[1] = 2;
            out[2] = 3;
            return 1;
        }
        if (std::strcmp(ref, "null_ref") == 0) return 0;
        std::snprintf(err, static_cast<size_t>(errCap), "unknown reference '%s'", ref);
        return -1;
    }
    void play_with_flags(int id, const int* p, int v, int f) override {
        rec("flags " + std::to_string(id) + " " + pos(p) + " " + std::to_string(v) + " " + std::to_string(f));
    }
    void play_falloff(int id, const int* p, int v, int fo, int pi) override {
        rec("falloff " + std::to_string(id) + " " + pos(p) + " " + std::to_string(v) + " " + std::to_string(fo) + " " +
            std::to_string(pi));
    }
    void play_3d_typed(int id, const int* p, int v, int t) override {
        rec("3d " + std::to_string(id) + " " + pos(p) + " " + std::to_string(v) + " " + std::to_string(t));
    }
    void play_3d_falloff(int id, int x, int y, int z, int v, int fo, int pi) override {
        rec("3dfo " + std::to_string(id) + " " + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) +
            " " + std::to_string(v) + " " + std::to_string(fo) + " " + std::to_string(pi));
    }
    int streaming() override { return 1; }
    void xa_preload(int id) override { rec("xa " + std::to_string(id)); }
    int ambient_ready() override { return 1; }
    void radio_stop() override { rec("radio_stop"); }
    void radio_npc_clear() override { rec("radio_npc_clear"); }

    void change(int t) override { rec("bgm " + std::to_string(t)); }
    void crossfade(int t) override { rec("crossfade " + std::to_string(t)); }
    int status_flags() override { return flags; }
    void set_status_flags(int v) override { flags = v; }
};

}  // namespace

SH_TEST(script_sound_family_names_and_calls) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "script" / "sound_family";
    fs::remove_all(root);
    fs::create_directories(root / "scripts");
    std::ofstream(root / "scripts" / "game.lua", std::ios::binary) << R"(
function on_boot()
  for _, n in ipairs({"id", "name", "exists", "sd_call", "stop", "attributes", "play", "play_with_flags",
                      "play_falloff", "play_3d", "play_3d_falloff", "streaming", "xa_preload", "bgm_crossfade",
                      "bgm_change", "radio_stop", "radio_npc_clear", "bgm_flags", "set_bgm_flags"}) do
    if type(sh.sound[n]) ~= "function" then sh.log("MISSING sound." .. n) end
  end
  for _, n in ipairs({"ambient_sfx", "xa_preload", "chara_sfx"}) do
    if type(sh[n]) ~= "function" then sh.log("MISSING " .. n) end
  end
  sh.sound.sd_call(5)
  sh.sound.stop(9)
  sh.sound.attributes(1, 0, 255, true)
  sh.log("ret " .. sh.sound.play(2, nil, 128))
  sh.sound.play_with_flags(3, "vec:0", 128, 0)
  sh.sound.play_with_flags(3, nil, 255, 3)
  sh.sound.play_with_flags(3, "null_ref", 1, 0)
  sh.sound.play_falloff(4, {10, 20, 30}, 5, 65536, -2)
  sh.sound.play_3d(6, "vec:0", 128, 0)
  sh.sound.play_3d_falloff(1563, 665600, -4608, 210739, 128, 196608, 0)
  sh.log("stream " .. sh.sound.streaming())
  sh.sound.xa_preload(565)
  sh.sound.bgm_crossfade(40)
  sh.sound.bgm_change(4)
  sh.sound.radio_stop()
  sh.sound.radio_npc_clear()
  sh.sound.set_bgm_flags(6)
  sh.log("flags " .. sh.sound.bgm_flags())
  sh.log("amb " .. tostring(sh.ambient_sfx()))
  if pcall(sh.sound.play, 1, 2, 3, 4) then sh.log("BAD arity") end
  if pcall(sh.sound.play, 1, "x", 3) then sh.log("BAD string") end
  if pcall(sh.sound.play_3d, 1, "nowhere", 3, 0) then sh.log("BAD ref") end
  if pcall(sh.sound.play_3d, 1, 5, 3, 0) then sh.log("BAD pointer") end
  sh.log("done")
end
)";
    Recorder rec;
    sh::script::set_sound_host(&rec);
    sh::script::set_music_host(&rec);
    std::vector<std::string> lines;
    HostConfig c;
    c.gamedata_dir = root;
    c.log = [&lines](int, const std::string& m) { lines.push_back(m); };
    {
        Host host(c);
        SH_CHECK(host.load().value());
        host.on_boot();
    }
    sh::script::set_sound_host(nullptr);
    sh::script::set_music_host(nullptr);
    std::string all;
    for (const auto& l : lines) all += l + "\n";
    SH_CHECK(all.find("done") != std::string::npos);
    SH_CHECK(all.find("MISSING") == std::string::npos);
    SH_CHECK(all.find("BAD") == std::string::npos);
    SH_CHECK(all.find("ret 7") != std::string::npos);
    SH_CHECK(all.find("stream 1") != std::string::npos);
    SH_CHECK(all.find("flags 6") != std::string::npos);
    SH_CHECK(all.find("amb true") != std::string::npos);
    const std::vector<std::string> want = {
        "sd 5", "stop 9", "attr 1 0 255 1", "play 2 0 128", "flags 3 1,2,3 128 0", "flags 3 nil 255 3",
        "flags 3 nil 1 0", "falloff 4 10,20,30 5 65536 -2", "3d 6 1,2,3 128 0",
        "3dfo 1563 665600,-4608,210739 128 196608 0", "xa 565", "crossfade 40", "bgm 4", "radio_stop",
        "radio_npc_clear"};
    SH_CHECK(rec.calls == want);
    if (rec.calls != want) {
        std::string got;
        for (const auto& s : rec.calls) got += s + "; ";
        ::sh::test::record_failure(__FILE__, __LINE__, (got + "\n" + all).c_str());
    }
    std::error_code ec;
    fs::remove_all(root, ec);
}
