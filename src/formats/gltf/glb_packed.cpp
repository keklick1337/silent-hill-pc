#include "formats/gltf/glb_packed.h"

#include "formats/common/crc32.h"
#include "formats/common/json.h"

#include <algorithm>

namespace sh::gltf {
namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67;  // "glTF"
constexpr std::uint32_t kChunkJson = 0x4E4F534A;
constexpr std::uint32_t kChunkBin = 0x004E4942;

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool unsigned_of(const json::Value* v, std::uint64_t& out) {
    if (v == nullptr || !v->is_integer() || v->integer() < 0) {
        return false;
    }
    out = static_cast<std::uint64_t>(v->integer());
    return true;
}

}  // namespace

Result<std::vector<PackedFile>> read_packed_files(std::span<const std::uint8_t> glb, std::string_view extension) {
    using Out = Result<std::vector<PackedFile>>;
    if (glb.size() < 20 || le32(glb.data()) != kGlbMagic || le32(glb.data() + 4) != 2) {
        return Out::fail("not a glTF 2.0 binary");
    }
    const std::uint64_t total = std::min<std::uint64_t>(le32(glb.data() + 8), glb.size());
    const std::uint64_t json_size = le32(glb.data() + 12);
    if (le32(glb.data() + 16) != kChunkJson || 20 + json_size + 8 > total) {
        return Out::fail("broken GLB header");
    }
    const std::uint64_t bin_at = 20 + json_size;
    const std::uint64_t bin_size = le32(glb.data() + bin_at);
    const std::uint64_t bin_start = bin_at + 8;
    if (le32(glb.data() + bin_at + 4) != kChunkBin || bin_start + bin_size > total) {
        return Out::fail("no BIN chunk");
    }
    auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(glb.data() + 20), json_size));
    if (!parsed) {
        return Out::fail("JSON chunk: " + parsed.error());
    }
    const json::Value& root = parsed.value();
    const json::Value* exts = root.find("extensions");
    const json::Value* ext = exts != nullptr ? exts->find(extension) : nullptr;
    const json::Value* files = ext != nullptr ? ext->find("files") : nullptr;
    const json::Value* views = root.find("bufferViews");
    if (files == nullptr || !files->is_array() || views == nullptr || !views->is_array()) {
        return Out::fail("no " + std::string(extension) + " files");
    }
    std::vector<PackedFile> out;
    for (std::size_t i = 0; i < files->array().size(); ++i) {
        const json::Value& f = files->array()[i];
        const std::string where = std::string(extension) + ".files[" + std::to_string(i) + "]: ";
        const json::Value* path = f.find("path");
        std::uint64_t size = 0, crc = 0, view = 0, offset = 0, length = 0;
        if (path == nullptr || !path->is_string() || !unsigned_of(f.find("size"), size) ||
            !unsigned_of(f.find("crc32"), crc) || !unsigned_of(f.find("bufferView"), view) ||
            view >= views->array().size()) {
            return Out::fail(where + "needs path, size, crc32 and an existing bufferView");
        }
        const json::Value& bv = views->array()[view];
        if (const json::Value* o = bv.find("byteOffset"); o != nullptr && !unsigned_of(o, offset)) {
            return Out::fail(where + "bad byteOffset");
        }
        if (!unsigned_of(bv.find("byteLength"), length) || length < size || offset + length > bin_size) {
            return Out::fail(where + "the bufferView does not hold the file");
        }
        PackedFile pf;
        pf.path = path->string();
        if (const json::Value* k = f.find("kind"); k != nullptr && k->is_string()) {
            pf.kind = k->string();
        }
        const std::uint8_t* at = glb.data() + bin_start + offset;
        pf.bytes.assign(at, at + size);
        if (crc32(pf.bytes.data(), pf.bytes.size()) != crc) {
            return Out::fail(where + pf.path + ": CRC-32 differs from the one recorded (the .glb is damaged)");
        }
        out.push_back(std::move(pf));
    }
    return Out::ok(std::move(out));
}

}  // namespace sh::gltf
