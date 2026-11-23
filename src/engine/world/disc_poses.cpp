#include "engine/world/disc_poses.h"

#include "formats/common/json.h"

#include <cstdlib>
#include <cstring>

namespace sh::world {

namespace {

bool hex_of(const json::Value* v, std::uint32_t* out) {
    if (v == nullptr || !v->is_string() || v->string().empty()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long long n = std::strtoull(v->string().c_str(), &end, 16);
    if (end == nullptr || *end != '\0' || n > 0xFFFFFFFFull) {
        return false;
    }
    *out = static_cast<std::uint32_t>(n);
    return true;
}

}  // namespace

Result<OverlayTableRegistry> parse_overlay_tables(std::string_view json_text) {
    using R = Result<OverlayTableRegistry>;
    const Result<json::Value> parsed = json::parse(json_text);
    if (!parsed) {
        return R::fail(parsed.error());
    }
    const json::Value& root = parsed.value();
    const json::Value* bin = root.find("bin");
    const json::Value* symbols = root.find("symbols");
    OverlayTableRegistry out;
    if (bin == nullptr || !bin->is_string() || symbols == nullptr || !symbols->is_array() ||
        !hex_of(root.find("base"), &out.base)) {
        return R::fail("a table registry needs \"bin\", \"base\" (hex) and \"symbols\"");
    }
    out.bin = bin->string();
    for (const json::Value& s : symbols->array()) {
        const json::Value* name = s.find("symbol");
        if (name == nullptr || !name->is_string()) {
            return R::fail("a table needs \"symbol\"");
        }
        const json::Value* port = s.find("port_word");
        if (port != nullptr && port->is_bool() && port->boolean()) {
            continue;  // storage of the engine, not on the disc
        }
        OverlayTable t;
        t.name = name->string();
        if (!hex_of(s.find("va"), &t.address)) {
            return R::fail("table " + t.name + " needs \"va\" (hex)");
        }
        // Tables relocated per element (pointers inside) carry no "bytes": they are not flat copies of the disc.
        const json::Value* bytes = s.find("bytes");
        if (bytes != nullptr && bytes->is_integer() && bytes->integer() > 0) {
            t.bytes = static_cast<std::uint32_t>(bytes->integer());
        }
        out.tables.push_back(std::move(t));
    }
    return R::ok(std::move(out));
}

Result<DiscPose> resolve_disc_pose(const OverlayTableRegistry& registry, const WorldObjectPoseRef& ref,
                                   std::span<const std::uint8_t> overlay) {
    using R = Result<DiscPose>;
    std::uint64_t row0 = 0;
    const std::string wanted = "g_" + ref.disc_table;
    for (const OverlayTable& t : registry.tables) {
        if (t.name == wanted) {
            row0 = t.address;
            break;
        }
    }
    if (row0 == 0) {
        row0 = ref.disc_address;
    }
    if (row0 == 0) {
        return R::fail("no " + ref.disc_table + " table");
    }
    const std::uint64_t at = row0 + std::uint64_t{kDiscPoseBytes} * static_cast<std::uint64_t>(ref.index);
    bool held = false;
    for (const OverlayTable& t : registry.tables) {
        if (t.bytes > 0 && t.address <= at && at + kDiscPoseBytes <= std::uint64_t{t.address} + t.bytes) {
            held = true;
            break;
        }
    }
    if (!held) {
        return R::fail("index out of the table");
    }
    if (at < registry.base || at - registry.base + kDiscPoseBytes > overlay.size()) {
        return R::fail("row outside " + registry.bin);
    }
    const std::uint8_t* p = overlay.data() + (at - registry.base);
    DiscPose pose;
    for (int i = 0; i < 3; ++i) {
        const std::uint8_t* w = p + 4 * i;
        pose.position[static_cast<std::size_t>(i)] = static_cast<std::int32_t>(
            std::uint32_t{w[0]} | std::uint32_t{w[1]} << 8 | std::uint32_t{w[2]} << 16 | std::uint32_t{w[3]} << 24);
        const std::uint8_t* h = p + 12 + 2 * i;
        pose.rotation[static_cast<std::size_t>(i)] =
            static_cast<std::int16_t>(static_cast<std::uint16_t>(h[0] | h[1] << 8));
    }
    return R::ok(pose);
}

}  // namespace sh::world
