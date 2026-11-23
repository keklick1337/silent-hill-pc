// The script group sh.world.* (engine/world/world_script.h): argument parsing, the query picked and the answer
// layout, against a recording host.

#include "engine/world/world_script.h"

#include "framework/sh_test.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

namespace wd = sh::world;

class FakeHost final : public wd::WorldScriptHost {
public:
    std::string last;
    wd::ScriptRay mode = wd::ScriptRay::Solid;
    wd::Vec3 from{}, to{};
    long long exclude = 0;

    wd::ScriptGround ground(std::int32_t x, std::int32_t z) override {
        last = "ground";
        return wd::ScriptGround{x + z, -3, 4, -1};
    }
    wd::ScriptRayHit ray(wd::ScriptRay m, const wd::Vec3& f, const wd::Vec3& t, long long ex) override {
        last = "ray";
        mode = m, from = f, to = t, exclude = ex;
        wd::ScriptRayHit h;
        h.hit = true;
        h.point = wd::Vec3{1, 2, 3};
        h.distance = 77;
        h.slot = 5;
        h.ground_type = 9;
        h.angle = -1024;
        h.ground_height = -8192;
        return h;
    }
    bool cell_has_collision(std::int32_t, std::int32_t) override {
        last = "cell";
        return true;
    }
    std::uint32_t collision_groups() override { return 0x8001u; }
    std::int32_t speed_zone(std::int32_t x, std::int32_t) override { return x > 0 ? 2 : 0; }
};

ShScriptArg integer(long long v) { return ShScriptArg{1, v, nullptr}; }
ShScriptArg text(const char* s) { return ShScriptArg{2, 0, s}; }

int call(FakeHost& host, const char* name, const std::vector<ShScriptArg>& args, long long* out, int cap = 8) {
    char err[64] = {};
    return wd::world_script_call(host, name, static_cast<int>(args.size()), args.data(), out, cap, err, sizeof(err));
}

}  // namespace

SH_TEST(world_script_names_and_passing) {
    FakeHost host;
    long long out[8] = {};
    SH_CHECK(call(host, "entity.get", {}, out) == wd::kNotWorldCall);
    SH_CHECK(call(host, "world.effects_update", {}, out) == wd::kNotWorldCall);
    SH_CHECK(call(host, nullptr, {}, out) == wd::kNotWorldCall);
    // A buffer too small is refused for every name of the group, before anything is asked.
    SH_CHECK(call(host, "world.collision_groups", {}, out, 7) == -1);
    SH_CHECK(host.last.empty());
    int n = 0;
    for (const char* const* p = wd::world_script_names(); *p != nullptr; ++p) {
        ++n;
    }
    SH_CHECK(n == 6);
}

SH_TEST(world_script_ground_cell_groups) {
    FakeHost host;
    long long out[8] = {};
    SH_CHECK(call(host, "world.ground", {integer(10), integer(20)}, out) == 4);
    SH_CHECK(out[0] == 30 && out[1] == -3 && out[2] == 4 && out[3] == -1);
    // cell(x, z): 40 m squares, floor division for negative coordinates.
    SH_CHECK(call(host, "world.cell", {integer(-1), integer(40 * 4096)}, out) == 3);
    SH_CHECK(out[0] == 1 && out[1] == -1 && out[2] == 1);
    SH_CHECK(call(host, "world.cell", {integer(40 * 4096 - 16), integer(-40 * 4096)}, out) == 3);
    SH_CHECK(out[1] == 0 && out[2] == -1);
    SH_CHECK(call(host, "world.collision_groups", {}, out) == 1 && out[0] == 0x8001);
    SH_CHECK(call(host, "world.speed_zone", {integer(5), integer(0)}, out) == 1 && out[0] == 2);
    // A non-integer argument reads as 0.
    SH_CHECK(call(host, "world.speed_zone", {text("x"), integer(0)}, out) == 1 && out[0] == 0);
}

SH_TEST(world_script_rays) {
    FakeHost host;
    long long out[8] = {};
    SH_CHECK(call(host, "world.ray", {integer(1), integer(2), integer(3), integer(4), integer(5), integer(6)}, out) ==
             8);
    SH_CHECK(host.mode == wd::ScriptRay::Solid && host.exclude == -1);
    SH_CHECK(host.from.x == 1 && host.to.z == 6);
    SH_CHECK(out[0] == 1 && out[1] == 1 && out[4] == 77 && out[5] == 5 && out[6] == 9 && out[7] == -1024);
    SH_CHECK(call(host, "world.ray",
                  {integer(0), integer(0), integer(0), integer(0), integer(0), integer(0), text("sight"), integer(3)},
                  out) == 8);
    SH_CHECK(host.mode == wd::ScriptRay::Sight && host.exclude == 3);
    SH_CHECK(call(host, "world.ray",
                  {integer(0), integer(0), integer(0), integer(0), integer(0), integer(0), text("chara")}, out) == 8);
    SH_CHECK(host.mode == wd::ScriptRay::Chara);
    SH_CHECK(call(host, "world.ray",
                  {integer(0), integer(0), integer(0), integer(0), integer(0), integer(0), text("probe")}, out) == 8);
    SH_CHECK(host.mode == wd::ScriptRay::Probe);
    host.last.clear();
    SH_CHECK(call(host, "world.ray",
                  {integer(0), integer(0), integer(0), integer(0), integer(0), integer(0), text("laser")}, out) == -1);
    SH_CHECK(host.last.empty());
    // ray_surface: the solid ray with the surface it met.
    SH_CHECK(call(host, "world.ray_surface", {integer(1), integer(2), integer(3), integer(4), integer(5), integer(6)},
                  out) == 5);
    SH_CHECK(host.mode == wd::ScriptRay::Solid);
    SH_CHECK(out[0] == 1 && out[1] == 77 && out[2] == 9 && out[3] == -1024 && out[4] == -8192);
    // Coordinates are 32-bit: a script integer keeps its low 32 bits.
    SH_CHECK(call(host, "world.ray", {integer(0x100000005LL), integer(0), integer(0), integer(0), integer(0), integer(0)},
                  out) == 8);
    SH_CHECK(host.from.x == 5);
}

SH_TEST(world_script_chain) {
    static int next_calls = 0;
    ShScriptGameApi api{};
    api.named_call = [](const char*, int, const ShScriptArg*, long long* out, int, char*, int) -> int {
        ++next_calls;
        out[0] = 42;
        return 1;
    };
    static const char* const before[] = {"entity.get", nullptr};
    api.named_list = []() -> const char* const* { return before; };
    static FakeHost host;
    wd::bind_world_script(api, host);
    long long out[8] = {};
    char err[32];
    SH_CHECK(api.named_call("entity.get", 0, nullptr, out, 8, err, sizeof(err)) == 1 && next_calls == 1);
    SH_CHECK(api.named_call("world.collision_groups", 0, nullptr, out, 8, err, sizeof(err)) == 1 && next_calls == 1);
    const char* const* names = api.named_list();
    SH_CHECK(std::strcmp(names[0], "entity.get") == 0);
    SH_CHECK(std::strcmp(names[1], "world.ground") == 0 && std::strcmp(names[6], "world.ray_surface") == 0);
    SH_CHECK(names[7] == nullptr);
}
