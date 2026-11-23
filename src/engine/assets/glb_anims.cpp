#include "engine/assets/glb_anims.h"

#include "formats/common/json.h"
#include "formats/gltf/glb_packed.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <system_error>
#include <vector>

namespace sh::assets {
namespace {

namespace fs = std::filesystem;
namespace an = sh::convert::anim;

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// The JSON chunk of a glb, empty when the file is not one.
std::string json_chunk(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    unsigned char head[20];
    if (!in.read(reinterpret_cast<char*>(head), sizeof head) || le32(head) != 0x46546C67u ||
        le32(head + 16) != 0x4E4F534Au) {
        return {};
    }
    std::string text(le32(head + 12), '\0');
    if (!in.read(text.data(), static_cast<std::streamsize>(text.size()))) {
        return {};
    }
    return text;
}

// The packed files of the last glb read: a map load asks for the base file and the map's file one after the other.
struct Cache {
    std::mutex mutex;
    fs::path file;
    std::shared_ptr<const std::vector<sh::gltf::PackedFile>> files;
};

Cache& cache() {
    static Cache c;
    return c;
}

Result<std::shared_ptr<const std::vector<sh::gltf::PackedFile>>> files_of(const fs::path& glb_file) {
    using Out = Result<std::shared_ptr<const std::vector<sh::gltf::PackedFile>>>;
    Cache& c = cache();
    std::lock_guard<std::mutex> lock(c.mutex);
    if (c.files && c.file == glb_file) {
        return Out::ok(c.files);
    }
    std::ifstream in(glb_file, std::ios::binary);
    if (!in) {
        return Out::fail(glb_file.string() + ": cannot open");
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto files = sh::gltf::read_packed_files(bytes, kAnimFilesExtension);
    if (!files) {
        return Out::fail(glb_file.string() + ": " + files.error());
    }
    c.file = glb_file;
    c.files = std::make_shared<const std::vector<sh::gltf::PackedFile>>(std::move(files.value()));
    return Out::ok(c.files);
}

}  // namespace

std::map<std::string, std::string, std::less<>> packed_anim_index(const fs::path& convert_dir) {
    std::map<std::string, std::string, std::less<>> index;
    const fs::path dir = convert_dir / "models";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return index;
    }
    std::vector<fs::path> glbs;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".glb") {
            glbs.push_back(it->path());
        }
    }
    std::sort(glbs.begin(), glbs.end());
    for (const fs::path& glb : glbs) {
        const std::string text = json_chunk(glb);
        // Most models pack nothing: skip their JSON without parsing it.
        if (text.find(kAnimFilesExtension) == std::string::npos) {
            continue;
        }
        auto doc = sh::json::parse(text);
        if (!doc) {
            continue;
        }
        const sh::json::Value* exts = doc.value().find("extensions");
        const sh::json::Value* ext = exts != nullptr ? exts->find(kAnimFilesExtension) : nullptr;
        const sh::json::Value* files = ext != nullptr ? ext->find("files") : nullptr;
        if (files == nullptr || !files->is_array()) {
            continue;
        }
        const std::string relative = fs::relative(glb, convert_dir, ec).generic_string();
        for (const sh::json::Value& f : files->array()) {
            const sh::json::Value* path = f.find("path");
            if (path != nullptr && path->is_string()) {
                index.emplace(path->string(), relative);
            }
        }
    }
    return index;
}

Result<PackedAnm> packed_anm(const fs::path& glb_file, std::string_view game_path) {
    using Out = Result<PackedAnm>;
    auto files = files_of(glb_file);
    if (!files) {
        return Out::fail(files.error());
    }
    const sh::gltf::PackedFile* wanted = nullptr;
    const sh::gltf::PackedFile* base = nullptr;
    for (const sh::gltf::PackedFile& f : *files.value()) {
        if (f.path == game_path) {
            wanted = &f;
        }
        if (f.kind == "anm") {
            base = &f;
        }
    }
    if (wanted == nullptr) {
        return Out::fail(glb_file.string() + ": packs no " + std::string(game_path));
    }
    PackedAnm out;
    if (an::AnmFile::has_header(wanted->bytes)) {
        auto file = an::AnmFile::parse(wanted->bytes);
        if (!file) {
            return Out::fail(std::string(game_path) + ": " + file.error());
        }
        out.file = std::move(file.value());
        return Out::ok(std::move(out));
    }
    if (base == nullptr) {
        return Out::fail(glb_file.string() + ": " + std::string(game_path) + " has no base file packed with it");
    }
    auto parsed_base = an::AnmFile::parse(base->bytes);
    if (!parsed_base) {
        return Out::fail(base->path + ": " + parsed_base.error());
    }
    auto file = an::AnmFile::parse_extension(wanted->bytes, parsed_base.value());
    if (!file) {
        return Out::fail(std::string(game_path) + ": " + file.error());
    }
    out.file = std::move(file.value());
    out.base = base->path;
    return Out::ok(std::move(out));
}

}  // namespace sh::assets
