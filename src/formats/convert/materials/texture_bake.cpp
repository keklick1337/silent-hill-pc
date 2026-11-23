// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "formats/convert/materials/texture_bake.h"

#include "formats/common/json.h"
#include "formats/gltf/cgltf_prefixed.h"
#include "formats/png/sh_png.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>
#include <utility>

namespace sh::convert::materials {

extern const char* const kBuiltinDrawUsageJson;

namespace {

namespace fs = std::filesystem;

constexpr std::uint32_t kGlbMagic = 0x46546C67;   // "glTF"
constexpr std::uint32_t kChunkJson = 0x4E4F534A;  // "JSON"

// ---- small helpers ------------------------------------------------------------------------------------------------

std::string read_file(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Temp file + rename, so a reader never sees half a file and an interrupted bake leaves the old one.
bool write_atomic(const fs::path& file, std::string_view bytes, std::string& error) {
    const fs::path temp = file.string() + ".bake-tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            error = "cannot write " + temp.string();
            std::error_code ec;
            fs::remove(temp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(temp, file, ec);
    if (ec) {
        error = "cannot replace " + file.string() + ": " + ec.message();
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

// Writes only when the bytes differ; true when the file changed.
bool write_if_changed(const fs::path& file, std::string_view bytes, std::string& error) {
    std::error_code ec;
    if (fs::is_regular_file(file, ec) && fs::file_size(file, ec) == bytes.size() && read_file(file) == bytes) {
        return false;
    }
    return write_atomic(file, bytes, error);
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// "CHARA/HERO.TIM" -> "CHARA/HERO" (the converter's stem: the game path without its extension).
std::string tim_stem(std::string_view tim) {
    const std::size_t slash = tim.rfind('/');
    const std::size_t dot = tim.rfind('.');
    if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash)) {
        return std::string(tim.substr(0, dot));
    }
    return std::string(tim);
}

std::string file_stem(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find('.');
    return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

template <typename F>
void parallel_for(std::size_t count, unsigned threads, F&& body) {
    std::atomic<std::size_t> next{0};
    auto worker = [&] {
        for (std::size_t i = next++; i < count; i = next++) {
            body(i);
        }
    };
    const std::size_t workers = std::min<std::size_t>(threads, count);
    if (workers <= 1) {
        worker();
        return;
    }
    std::vector<std::thread> pool;
    pool.reserve(workers - 1);
    for (std::size_t t = 1; t < workers; ++t) {
        pool.emplace_back(worker);
    }
    worker();
    for (std::thread& t : pool) {
        t.join();
    }
}

// ---- masks and the coverage rule ----------------------------------------------------------------------------------

class Bitmask {
public:
    Bitmask() = default;
    explicit Bitmask(std::size_t bits) : words_((bits + 63) / 64, 0) {}
    bool empty_storage() const { return words_.empty(); }
    void set(std::size_t i) { words_[i >> 6] |= std::uint64_t{1} << (i & 63); }
    bool get(std::size_t i) const { return !words_.empty() && (words_[i >> 6] >> (i & 63)) & 1; }
    void merge(const Bitmask& other) {
        if (words_.empty()) {
            words_ = other.words_;
            return;
        }
        for (std::size_t i = 0; i < words_.size() && i < other.words_.size(); ++i) {
            words_[i] |= other.words_[i];
        }
    }
    std::size_t count() const {
        std::size_t n = 0;
        for (std::uint64_t w : words_) {
            n += static_cast<std::size_t>(std::popcount(w));
        }
        return n;
    }

private:
    std::vector<std::uint64_t> words_;
};

// Corners in doubled texel coordinates (T = 2t, t = uv * size - 0.5), so every test is exact integer arithmetic.
struct Point2 {
    std::int64_t x;
    std::int64_t y;
};

std::int64_t floor_half(std::int64_t v) { return v >= 0 ? v / 2 : -((-v + 1) / 2); }

// u, v: doubled texel coordinates of the corners (texel_coordinate).
template <typename Mark>
void cover_triangle(int width, int height, const std::int64_t u[3], const std::int64_t v[3], Mark&& mark) {
    const std::int64_t lo_x = std::max<std::int64_t>(floor_half(std::min({u[0], u[1], u[2]})), 0);
    const std::int64_t hi_x = std::min<std::int64_t>(floor_half(std::max({u[0], u[1], u[2]})), width - 1);
    const std::int64_t lo_y = std::max<std::int64_t>(floor_half(std::min({v[0], v[1], v[2]})), 0);
    const std::int64_t hi_y = std::min<std::int64_t>(floor_half(std::max({v[0], v[1], v[2]})), height - 1);
    if (lo_x > hi_x || lo_y > hi_y) {
        return;
    }
    const Point2 p[3] = {{u[0], v[0]}, {u[1], v[1]}, {u[2], v[2]}};
    const std::int64_t area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    if (area == 0) {
        for (std::int64_t y = lo_y; y <= hi_y; ++y) {
            for (std::int64_t x = lo_x; x <= hi_x; ++x) {
                mark(static_cast<std::size_t>(y * width + x));
            }
        }
        return;
    }
    // Edge e: n . (q - a) with n pointing into the triangle. The square overlaps the open triangle when, for every
    // edge, its most inward corner is strictly inside (the box test above settles the two axis directions).
    std::int64_t nx[3], ny[3], ax[3], ay[3];
    for (int e = 0; e < 3; ++e) {
        const Point2& a = p[e];
        const Point2& b = p[(e + 1) % 3];
        const Point2& c = p[(e + 2) % 3];
        nx[e] = b.y - a.y;
        ny[e] = -(b.x - a.x);
        if (nx[e] * (c.x - a.x) + ny[e] * (c.y - a.y) < 0) {
            nx[e] = -nx[e];
            ny[e] = -ny[e];
        }
        ax[e] = a.x;
        ay[e] = a.y;
    }
    for (std::int64_t y = lo_y; y <= hi_y; ++y) {
        for (std::int64_t x = lo_x; x <= hi_x; ++x) {
            // Square (x - 0.5, x + 1.5): doubled 2x - 1 .. 2x + 3.
            bool inside = true;
            for (int e = 0; e < 3 && inside; ++e) {
                const std::int64_t d = nx[e] * (2 * x - 1 - ax[e]) + ny[e] * (2 * y - 1 - ay[e]) +
                                       std::max<std::int64_t>(0, 4 * nx[e]) + std::max<std::int64_t>(0, 4 * ny[e]);
                inside = d > 0;
            }
            if (inside) {
                mark(static_cast<std::size_t>(y * width + x));
            }
        }
    }
}

// The doubled texel coordinate 2 * (value * size - 0.5): the importer writes texel centres, (t + 0.5) / size, so this is
// 2t exactly. Returns false for a coordinate that is not a usable number.
bool texel_coordinate(float value, int size, std::int64_t& out) {
    const double scaled = static_cast<double>(value) * size;
    if (!std::isfinite(scaled) || std::fabs(scaled) > 1.0e7) {
        return false;
    }
    out = static_cast<std::int64_t>(std::llround(2.0 * scaled)) - 1;
    return true;
}

// ---- TIM descriptions ---------------------------------------------------------------------------------------------

struct TimInfo {
    std::string key;   // "CHARA/HERO.TIM", as the extras name it
    std::string stem;  // "CHARA/HERO"
    int width = 0;
    int height = 0;
    int bpp = 0;
    std::vector<std::vector<std::uint16_t>> palettes;
    bool bakeable() const { return (bpp == 4 || bpp == 8) && !palettes.empty() && width > 0 && height > 0; }
};

class TimCatalog {
public:
    explicit TimCatalog(fs::path convert) : convert_(std::move(convert)) {}

    // Null when textures/<stem>.tim.json is missing or malformed (one warning per TIM).
    std::shared_ptr<const TimInfo> get(const std::string& key) {
        std::lock_guard lock(mutex_);
        auto found = cache_.find(key);
        if (found != cache_.end()) {
            return found->second;
        }
        std::shared_ptr<const TimInfo> info = load(key);
        cache_.emplace(key, info);
        return info;
    }

    std::set<std::string> take_warnings() {
        std::lock_guard lock(mutex_);
        return std::move(warnings_);
    }

private:
    std::shared_ptr<const TimInfo> load(const std::string& key) {
        auto info = std::make_shared<TimInfo>();
        info->key = key;
        info->stem = tim_stem(key);
        const fs::path json_path = convert_ / "textures" / (info->stem + ".tim.json");
        const std::string text = read_file(json_path);
        if (text.empty()) {
            warnings_.insert(key + ": no " + json_path.generic_string());
            return nullptr;
        }
        auto doc = json::parse(text);
        if (!doc || !doc.value().is_object()) {
            warnings_.insert(json_path.generic_string() + ": " + (doc ? std::string("not an object") : doc.error()));
            return nullptr;
        }
        const json::Value& root = doc.value();
        auto integer = [&](const char* name) {
            const json::Value* v = root.find(name);
            return v != nullptr && v->is_integer() ? static_cast<int>(v->integer()) : 0;
        };
        info->width = integer("width");
        info->height = integer("height");
        info->bpp = integer("bpp");
        if (const json::Value* palettes = root.find("palettes"); palettes != nullptr && palettes->is_array()) {
            for (const json::Value& row : palettes->array()) {
                std::vector<std::uint16_t> words;
                if (row.is_array()) {
                    for (const json::Value& entry : row.array()) {
                        words.push_back(entry.is_string()
                                            ? static_cast<std::uint16_t>(std::strtoul(entry.string().c_str(), nullptr, 16))
                                            : 0);
                    }
                }
                info->palettes.push_back(std::move(words));
            }
        }
        return info;
    }

    fs::path convert_;
    std::mutex mutex_;
    std::map<std::string, std::shared_ptr<const TimInfo>> cache_;
    std::set<std::string> warnings_;
};

// ---- coverage -----------------------------------------------------------------------------------------------------

struct RowUse {
    Bitmask drawn;
    Bitmask semi;
    std::set<std::string> consumers;
};

struct TimUse {
    std::shared_ptr<const TimInfo> info;
    std::map<int, RowUse> rows;
};

struct Coverage {
    std::mutex mutex;
    std::map<std::string, TimUse> tims;

    void merge(const std::shared_ptr<const TimInfo>& info, int row, const Bitmask& drawn, const Bitmask& semi,
               const std::string& consumer) {
        std::lock_guard lock(mutex);
        TimUse& tim = tims[info->key];
        tim.info = info;
        RowUse& use = tim.rows[row];
        if (use.drawn.empty_storage()) {
            use.drawn = Bitmask(static_cast<std::size_t>(info->width) * info->height);
            use.semi = Bitmask(static_cast<std::size_t>(info->width) * info->height);
        }
        use.drawn.merge(drawn);
        use.semi.merge(semi);
        use.consumers.insert(consumer);
    }
};

struct MaterialUse {
    bool handled = false;
    std::string tim;
    int row = 0;
    bool semi = false;
};

// tim (non-empty), palette (integer >= 0) and palette_in_tim (not false) in the material extras.
std::vector<MaterialUse> material_uses(const json::Value& doc) {
    std::vector<MaterialUse> uses;
    const json::Value* materials = doc.find("materials");
    if (materials == nullptr || !materials->is_array()) {
        return uses;
    }
    for (const json::Value& material : materials->array()) {
        MaterialUse use;
        const json::Value* extras = material.find("extras");
        if (extras != nullptr && extras->is_object()) {
            const json::Value* tim = extras->find("tim");
            const json::Value* palette = extras->find("palette");
            const json::Value* in_tim = extras->find("palette_in_tim");
            const json::Value* semi = extras->find("semi_transparent");
            if (tim != nullptr && tim->is_string() && !tim->string().empty() && palette != nullptr &&
                palette->is_integer() && palette->integer() >= 0 && palette->integer() < 4096 &&
                !(in_tim != nullptr && in_tim->is_bool() && !in_tim->boolean())) {
                use.handled = true;
                use.tim = tim->string();
                use.row = static_cast<int>(palette->integer());
                use.semi = semi != nullptr && semi->is_bool() && semi->boolean();
            }
        }
        uses.push_back(std::move(use));
    }
    return uses;
}

struct GlbFile {
    std::string bytes;
    std::size_t json_begin = 0;
    std::size_t json_size = 0;
};

bool split_glb(std::string bytes, GlbFile& out, std::string& error) {
    auto u32 = [&](std::size_t at) {
        std::uint32_t v = 0;
        std::memcpy(&v, bytes.data() + at, 4);
        return v;
    };
    if (bytes.size() < 20 || u32(0) != kGlbMagic || u32(4) != 2 || u32(16) != kChunkJson) {
        error = "not a glb 2.0 file with a JSON chunk";
        return false;
    }
    const std::uint32_t json_size = u32(12);
    if (u32(8) != bytes.size() || 20 + static_cast<std::size_t>(json_size) > bytes.size()) {
        error = "bad glb lengths";
        return false;
    }
    out.bytes = std::move(bytes);
    out.json_begin = 20;
    out.json_size = json_size;
    return true;
}

struct Consumer {
    fs::path path;
    std::string name;  // relative to the gamedata dir: "convert/models/CHARA/HERO.glb"
    bool scene = false;
};

struct ScanResult {
    bool handled = false;
    std::vector<std::string> warnings;
};

ScanResult scan_glb(const Consumer& consumer, TimCatalog& catalog, Coverage& coverage) {
    ScanResult result;
    GlbFile glb;
    std::string error;
    if (!split_glb(read_file(consumer.path), glb, error)) {
        result.warnings.push_back(consumer.name + ": " + error);
        return result;
    }
    auto doc = json::parse(std::string_view(glb.bytes).substr(glb.json_begin, glb.json_size));
    if (!doc) {
        result.warnings.push_back(consumer.name + ": " + doc.error());
        return result;
    }
    const std::vector<MaterialUse> uses = material_uses(doc.value());
    if (std::none_of(uses.begin(), uses.end(), [](const MaterialUse& u) { return u.handled; })) {
        return result;
    }

    cgltf_options options;
    std::memset(&options, 0, sizeof options);
    cgltf_data* raw = nullptr;
    if (cgltf_parse(&options, glb.bytes.data(), glb.bytes.size(), &raw) != cgltf_result_success) {
        result.warnings.push_back(consumer.name + ": cgltf cannot parse it");
        return result;
    }
    std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> data(raw, cgltf_free);
    if (cgltf_load_buffers(&options, data.get(), nullptr) != cgltf_result_success ||
        cgltf_validate(data.get()) != cgltf_result_success) {
        result.warnings.push_back(consumer.name + ": cgltf rejects its buffers");
        return result;
    }

    struct Local {
        std::shared_ptr<const TimInfo> info;
        Bitmask drawn;
        Bitmask semi;
    };
    std::map<std::pair<std::string, int>, Local> local;
    std::set<std::string> reported;
    std::vector<float> uv;
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        const cgltf_mesh& mesh = data->meshes[m];
        for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
            const cgltf_primitive& prim = mesh.primitives[p];
            if (prim.type != cgltf_primitive_type_triangles || prim.material == nullptr) {
                continue;
            }
            const cgltf_size material = cgltf_material_index(data.get(), prim.material);
            if (material >= uses.size() || !uses[material].handled) {
                continue;
            }
            const MaterialUse& use = uses[material];
            const cgltf_accessor* texcoord = cgltf_find_accessor(&prim, cgltf_attribute_type_texcoord, 0);
            if (texcoord == nullptr) {
                continue;
            }
            std::shared_ptr<const TimInfo> info = catalog.get(use.tim);
            if (!info || !info->bakeable()) {
                continue;
            }
            if (static_cast<std::size_t>(use.row) >= info->palettes.size()) {
                if (reported.insert(use.tim + "#" + std::to_string(use.row)).second) {
                    result.warnings.push_back(consumer.name + ": " + use.tim + " has no CLUT row " +
                                              std::to_string(use.row));
                }
                continue;
            }
            Local& slot = local[{use.tim, use.row}];
            if (!slot.info) {
                const std::size_t bits = static_cast<std::size_t>(info->width) * info->height;
                slot.info = info;
                slot.drawn = Bitmask(bits);
                slot.semi = Bitmask(bits);
            }
            uv.assign(texcoord->count * 2, 0.0f);
            for (cgltf_size v = 0; v < texcoord->count; ++v) {
                cgltf_accessor_read_float(texcoord, v, &uv[v * 2], 2);
            }
            const bool semi = use.semi && consumer.scene;
            const cgltf_size corners = prim.indices ? prim.indices->count : texcoord->count;
            for (cgltf_size i = 0; i + 2 < corners; i += 3) {
                std::int64_t tu[3], tv[3];
                bool usable = true;
                for (int k = 0; k < 3 && usable; ++k) {
                    const cgltf_size vertex = prim.indices ? cgltf_accessor_read_index(prim.indices, i + k) : i + k;
                    usable = vertex < texcoord->count && texel_coordinate(uv[vertex * 2], info->width, tu[k]) &&
                             texel_coordinate(uv[vertex * 2 + 1], info->height, tv[k]);
                }
                if (!usable) {
                    continue;
                }
                cover_triangle(info->width, info->height, tu, tv, [&](std::size_t texel) {
                    slot.drawn.set(texel);
                    if (semi) {
                        slot.semi.set(texel);
                    }
                });
            }
        }
    }
    for (const auto& [key, slot] : local) {
        coverage.merge(slot.info, key.second, slot.drawn, slot.semi, consumer.name);
        result.handled = true;
    }
    return result;
}

unsigned apply_draw_usage(std::string_view text, TimCatalog& catalog, Coverage& coverage,
                          std::vector<std::string>& warnings) {
    auto doc = json::parse(text);
    if (!doc) {
        warnings.push_back("draw_usage.json: " + doc.error());
        return 0;
    }
    const json::Value* usages = doc.value().find("usages");
    if (usages == nullptr || !usages->is_array()) {
        warnings.push_back("draw_usage.json: no \"usages\" array");
        return 0;
    }
    unsigned applied = 0;
    for (const json::Value& usage : usages->array()) {
        const json::Value* tim = usage.find("tim");
        const json::Value* row = usage.find("row");
        const json::Value* rects = usage.find("rects");
        const json::Value* semi = usage.find("semi");
        const json::Value* label = usage.find("label");
        if (tim == nullptr || !tim->is_string() || row == nullptr || !row->is_integer() || row->integer() < 0 ||
            rects == nullptr || !rects->is_array()) {
            warnings.push_back("draw_usage.json: line " + std::to_string(usage.line()) + ": incomplete entry");
            continue;
        }
        std::shared_ptr<const TimInfo> info = catalog.get(tim->string());
        if (!info || !info->bakeable()) {
            continue;  // not on this disc / not imported: nothing to bake
        }
        if (static_cast<std::size_t>(row->integer()) >= info->palettes.size()) {
            warnings.push_back("draw_usage.json: " + tim->string() + " has no CLUT row " +
                               std::to_string(row->integer()));
            continue;
        }
        const std::size_t bits = static_cast<std::size_t>(info->width) * info->height;
        Bitmask drawn(bits);
        Bitmask semi_mask(bits);
        const bool is_semi = semi != nullptr && semi->is_bool() && semi->boolean();
        for (const json::Value& rect : rects->array()) {
            if (!rect.is_array() || rect.array().size() != 4 ||
                !std::all_of(rect.array().begin(), rect.array().end(), [](const json::Value& v) { return v.is_integer(); })) {
                warnings.push_back("draw_usage.json: line " + std::to_string(rect.line()) + ": a rect is [x, y, w, h]");
                continue;
            }
            const std::int64_t x0 = std::max<std::int64_t>(rect.array()[0].integer(), 0);
            const std::int64_t y0 = std::max<std::int64_t>(rect.array()[1].integer(), 0);
            const std::int64_t x1 = std::min<std::int64_t>(rect.array()[0].integer() + rect.array()[2].integer(), info->width);
            const std::int64_t y1 = std::min<std::int64_t>(rect.array()[1].integer() + rect.array()[3].integer(), info->height);
            for (std::int64_t y = y0; y < y1; ++y) {
                for (std::int64_t x = x0; x < x1; ++x) {
                    const std::size_t texel = static_cast<std::size_t>(y * info->width + x);
                    drawn.set(texel);
                    if (is_semi) {
                        semi_mask.set(texel);
                    }
                }
            }
        }
        const std::string name =
            "draw_usage:" + (label != nullptr && label->is_string() ? label->string() : std::string("usage"));
        coverage.merge(info, static_cast<int>(row->integer()), drawn, semi_mask, name);
        ++applied;
    }
    return applied;
}

// ---- the bake of one TIM ------------------------------------------------------------------------------------------

std::uint8_t expand5(unsigned v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); }

std::uint32_t rgba_of(std::uint16_t word, bool semi) {
    const std::uint32_t r = expand5(word & 31);
    const std::uint32_t g = expand5((word >> 5) & 31);
    const std::uint32_t b = expand5((word >> 10) & 31);
    const std::uint32_t a = word == 0 ? 0 : ((word & 0x8000) && semi ? 128 : 255);
    return r | (g << 8) | (b << 16) | (a << 24);
}

struct VariantOut {
    std::string file;  // "textures/CHARA/HERO.prs2.png", relative to convert/
    std::string label;
    std::vector<int> rows;
    std::set<std::string> consumers;
};

struct TimBake {
    bool baked = false;
    std::string key;
    std::string base_file;
    std::vector<int> rows;       // every row some consumer draws
    std::vector<int> base_rows;  // rows that the base picture serves
    std::set<std::string> base_consumers;
    std::vector<VariantOut> variants;
    std::size_t texels = 0;
    std::size_t covered = 0;
    unsigned written = 0;
    std::map<int, std::string> file_of_row;
    std::vector<std::string> warnings;
};

bool reserved_label(const std::string& label) {
    if (label.empty() || label == "tim" || label == "indexed" || label == "png" || label == "json") {
        return true;
    }
    return label.size() >= 2 && label[0] == 'p' &&
           std::all_of(label.begin() + 1, label.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string sanitize_label(std::string_view text) {
    std::string out = lower(text);
    for (char& c : out) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) {
            c = '_';
        }
    }
    return out;
}

// A readable name for the consumers that need a variant: the one stem they share (the model, the map area, the draw
// usage label), unless it only repeats the TIM's own name; empty when there is none.
std::string preferred_label(const std::set<std::string>& consumers, const std::string& tim_name) {
    constexpr std::string_view kUsage = "draw_usage:";
    std::set<std::string> stems;
    bool all_usages = true;
    for (const std::string& name : consumers) {
        const bool usage = name.rfind(kUsage, 0) == 0;
        stems.insert(sanitize_label(usage ? std::string_view(name).substr(kUsage.size()) : file_stem(name)));
        all_usages = all_usages && usage;
    }
    if (stems.size() != 1) {
        return {};
    }
    const std::string& stem = *stems.begin();
    // A usage label is chosen by hand, so it stays even when it repeats the TIM name.
    return all_usages || stem != lower(tim_name) ? stem : std::string();
}

TimBake bake_tim(const fs::path& convert, const TimUse& use) {
    TimBake out;
    const TimInfo& info = *use.info;
    out.key = info.key;
    out.base_file = "textures/" + info.stem + ".png";
    const std::size_t texels = static_cast<std::size_t>(info.width) * info.height;
    out.texels = texels;

    const fs::path indexed_path = convert / "indexed" / (info.stem + ".png");
    const std::string indexed_bytes = read_file(indexed_path);
    if (indexed_bytes.empty()) {
        out.warnings.push_back(info.key + ": no " + indexed_path.generic_string() + "; left as it is");
        return out;
    }
    auto image = png::decode({reinterpret_cast<const std::uint8_t*>(indexed_bytes.data()), indexed_bytes.size()});
    if (!image || image.value().color_type != png::ColorType::Indexed ||
        image.value().width != static_cast<std::uint32_t>(info.width) ||
        image.value().height != static_cast<std::uint32_t>(info.height)) {
        out.warnings.push_back(indexed_path.generic_string() + ": " +
                               (image ? std::string("not an indexed picture of the TIM's size") : image.error()));
        return out;
    }
    const std::vector<std::uint8_t>& index = image.value().pixels;

    // Rows by total coverage, largest first, ties to the lower row.
    std::vector<std::pair<int, std::size_t>> order;
    for (const auto& [row, row_use] : use.rows) {
        out.rows.push_back(row);
        order.emplace_back(row, row_use.drawn.count());
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    auto word_at = [&](int row, std::size_t texel) -> std::uint16_t {
        const std::vector<std::uint16_t>& palette = info.palettes[static_cast<std::size_t>(row)];
        const std::size_t i = index[texel];
        return i < palette.size() ? palette[i] : 0;
    };
    auto colour = [&](int row, std::size_t texel) {
        return rgba_of(word_at(row, texel), use.rows.at(row).semi.get(texel));
    };

    // Base row per texel; uncovered texels grow from their covered neighbours.
    std::vector<int> base(texels, -1);
    for (const auto& [row, count] : order) {
        const Bitmask& drawn = use.rows.at(row).drawn;
        for (std::size_t t = 0; t < texels; ++t) {
            if (base[t] < 0 && drawn.get(t)) {
                base[t] = row;
            }
        }
    }
    out.covered = static_cast<std::size_t>(std::count_if(base.begin(), base.end(), [](int r) { return r >= 0; }));
    std::vector<int> next = base;
    for (int round = 0; round < 8; ++round) {
        bool changed = false;
        for (int y = 0; y < info.height; ++y) {
            for (int x = 0; x < info.width; ++x) {
                const std::size_t t = static_cast<std::size_t>(y) * info.width + x;
                if (base[t] >= 0) {
                    continue;
                }
                std::pair<int, int> votes[8];  // row, count
                int distinct = 0;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx;
                        const int ny = y + dy;
                        if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= info.width || ny >= info.height) {
                            continue;
                        }
                        const int row = base[static_cast<std::size_t>(ny) * info.width + nx];
                        if (row < 0) {
                            continue;
                        }
                        int k = 0;
                        while (k < distinct && votes[k].first != row) {
                            ++k;
                        }
                        if (k == distinct) {
                            votes[distinct++] = {row, 0};
                        }
                        ++votes[k].second;
                    }
                }
                if (distinct == 0) {
                    continue;
                }
                std::pair<int, int> best = votes[0];
                for (int k = 1; k < distinct; ++k) {
                    if (votes[k].second > best.second || (votes[k].second == best.second && votes[k].first < best.first)) {
                        best = votes[k];
                    }
                }
                next[t] = best.first;
                changed = true;
            }
        }
        base = next;
        if (!changed) {
            break;
        }
    }

    std::vector<std::uint32_t> base_rgba(texels);
    for (std::size_t t = 0; t < texels; ++t) {
        base_rgba[t] = base[t] >= 0 ? colour(base[t], t) : rgba_of(word_at(0, t), false);
    }

    // Greedy layers: a row joins the first layer none of whose texels it contradicts.
    struct Layer {
        std::vector<std::int64_t> colour;  // -1: texel not set by the layer
        std::vector<int> rows;
    };
    std::vector<Layer> layers;
    std::map<int, int> layer_of_row;  // -1: the base
    std::vector<std::size_t> need;
    for (const auto& [row, count] : order) {
        const Bitmask& drawn = use.rows.at(row).drawn;
        need.clear();
        for (std::size_t t = 0; t < texels; ++t) {
            if (drawn.get(t) && colour(row, t) != base_rgba[t]) {
                need.push_back(t);
            }
        }
        if (need.empty()) {
            layer_of_row[row] = -1;
            continue;
        }
        // A layer pins every texel its rows draw, also those equal to the base: a later row may only join when it
        // agrees on all of them, or it would repaint a texel an earlier row reads from this picture.
        need.clear();
        for (std::size_t t = 0; t < texels; ++t) {
            if (drawn.get(t)) {
                need.push_back(t);
            }
        }
        std::size_t chosen = layers.size();
        for (std::size_t l = 0; l < layers.size(); ++l) {
            const bool fits = std::all_of(need.begin(), need.end(), [&](std::size_t t) {
                return layers[l].colour[t] < 0 || layers[l].colour[t] == colour(row, t);
            });
            if (fits) {
                chosen = l;
                break;
            }
        }
        if (chosen == layers.size()) {
            layers.push_back({std::vector<std::int64_t>(texels, -1), {}});
        }
        for (std::size_t t : need) {
            layers[chosen].colour[t] = colour(row, t);
        }
        layers[chosen].rows.push_back(row);
        layer_of_row[row] = static_cast<int>(chosen);
    }

    // Labels, unique per TIM.
    const std::string tim_name = file_stem(info.stem);
    std::set<std::string> taken;
    for (Layer& layer : layers) {
        std::sort(layer.rows.begin(), layer.rows.end());
        VariantOut variant;
        variant.rows = layer.rows;
        for (int row : layer.rows) {
            const auto& names = use.rows.at(row).consumers;
            variant.consumers.insert(names.begin(), names.end());
        }
        std::string label = preferred_label(variant.consumers, tim_name);
        if (!label.empty() && (reserved_label(label) || taken.count(label))) {
            std::string numbered;
            for (int n = 2; numbered.empty() || taken.count(numbered); ++n) {
                numbered = label + "-" + std::to_string(n);
            }
            label = numbered;
        }
        if (label.empty()) {
            for (int n = 1; label.empty() || taken.count(label); ++n) {
                label = "alt" + std::to_string(n);
            }
        }
        taken.insert(label);
        variant.label = label;
        variant.file = "textures/" + info.stem + "." + label + ".png";
        out.variants.push_back(std::move(variant));
    }
    for (const auto& [row, layer] : layer_of_row) {
        if (layer < 0) {
            out.base_rows.push_back(row);
            const auto& names = use.rows.at(row).consumers;
            out.base_consumers.insert(names.begin(), names.end());
            out.file_of_row[row] = out.base_file;
        } else {
            out.file_of_row[row] = out.variants[static_cast<std::size_t>(layer)].file;
        }
    }

    // Pictures.
    auto write_picture = [&](const std::string& file, const std::vector<std::uint32_t>& rgba) {
        png::Image picture;
        picture.width = static_cast<std::uint32_t>(info.width);
        picture.height = static_cast<std::uint32_t>(info.height);
        picture.color_type = png::ColorType::Rgba;
        picture.pixels.resize(texels * 4);
        for (std::size_t t = 0; t < texels; ++t) {
            for (int c = 0; c < 4; ++c) {
                picture.pixels[t * 4 + c] = static_cast<std::uint8_t>(rgba[t] >> (8 * c));
            }
        }
        auto encoded = png::encode(picture);
        if (!encoded) {
            out.warnings.push_back(file + ": " + encoded.error());
            return false;
        }
        std::string error;
        const fs::path path = convert / file;
        const std::string_view bytes(reinterpret_cast<const char*>(encoded.value().data()), encoded.value().size());
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        const bool changed = write_if_changed(path, bytes, error);
        if (!error.empty()) {
            out.warnings.push_back(error);
            return false;
        }
        out.written += changed ? 1u : 0u;
        return true;
    };
    if (!write_picture(out.base_file, base_rgba)) {
        out.file_of_row.clear();
        return out;
    }
    std::vector<std::uint32_t> variant_rgba;
    for (std::size_t l = 0; l < layers.size(); ++l) {
        variant_rgba = base_rgba;
        for (std::size_t t = 0; t < texels; ++t) {
            if (layers[l].colour[t] >= 0) {
                variant_rgba[t] = static_cast<std::uint32_t>(layers[l].colour[t]);
            }
        }
        if (!write_picture(out.variants[l].file, variant_rgba)) {
            out.file_of_row.clear();
            return out;
        }
    }
    out.baked = true;
    return out;
}

// ---- the glb rewrite ----------------------------------------------------------------------------------------------

struct Edit {
    std::size_t begin;
    std::size_t end;
    std::string text;
};

// Byte offset of a parsed value in `text` (the parser counts columns in bytes).
class Offsets {
public:
    explicit Offsets(std::string_view text) {
        starts_.push_back(text.substr(0, 3) == "\xEF\xBB\xBF" ? 3 : 0);
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                starts_.push_back(i + 1);
            }
        }
    }
    std::size_t of(const json::Value& v) const {
        return v.line() == 0 || v.line() > starts_.size() ? std::string::npos : starts_[v.line() - 1] + v.column() - 1;
    }

private:
    std::vector<std::size_t> starts_;
};

// [begin, end) of the string literal that starts at `begin`; false when there is none.
bool string_extent(std::string_view text, std::size_t begin, std::size_t& end) {
    if (begin >= text.size() || text[begin] != '"') {
        return false;
    }
    for (std::size_t i = begin + 1; i < text.size(); ++i) {
        if (text[i] == '\\') {
            ++i;
        } else if (text[i] == '"') {
            end = i + 1;
            return true;
        }
    }
    return false;
}

// End of the array or object that starts at `begin` (the text is valid JSON: the parser read it).
bool value_end(std::string_view text, std::size_t begin, std::size_t& end) {
    if (begin >= text.size() || (text[begin] != '[' && text[begin] != '{')) {
        return false;
    }
    int depth = 0;
    for (std::size_t i = begin; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') {
            std::size_t after = 0;
            if (!string_extent(text, i, after)) {
                return false;
            }
            i = after - 1;
        } else if (c == '[' || c == '{') {
            ++depth;
        } else if ((c == ']' || c == '}') && --depth == 0) {
            end = i + 1;
            return true;
        }
    }
    return false;
}

struct RewriteResult {
    bool changed = false;
    std::vector<std::string> warnings;
};

RewriteResult rewrite_glb(const Consumer& consumer, const fs::path& convert,
                          const std::map<std::pair<std::string, int>, std::string>& file_of) {
    RewriteResult result;
    GlbFile glb;
    std::string error;
    if (!split_glb(read_file(consumer.path), glb, error)) {
        result.warnings.push_back(consumer.name + ": " + error);
        return result;
    }
    const std::string_view text = std::string_view(glb.bytes).substr(glb.json_begin, glb.json_size);
    auto parsed = json::parse(text);
    if (!parsed) {
        result.warnings.push_back(consumer.name + ": " + parsed.error());
        return result;
    }
    const json::Value& doc = parsed.value();
    const Offsets offsets(text);
    const std::vector<MaterialUse> uses = material_uses(doc);
    const json::Value* materials = doc.find("materials");
    const json::Value* textures = doc.find("textures");
    const json::Value* images = doc.find("images");

    std::vector<Edit> edits;
    auto replace_string = [&](const json::Value& value, const std::string& replacement) {
        std::size_t end = 0;
        const std::size_t begin = offsets.of(value);
        if (!value.is_string() || !string_extent(text, begin, end)) {
            result.warnings.push_back(consumer.name + ": cannot locate a string value at line " +
                                      std::to_string(value.line()));
            return;
        }
        if (value.string() != replacement) {
            edits.push_back({begin, end, json::quote(replacement)});
        }
    };

    // Per image, in material order: the picture each material needs ("" for a material the bake leaves alone).
    struct Need {
        std::string target;
        const json::Value* index;  // the material's baseColorTexture.index
        std::size_t texture;
    };
    std::map<std::size_t, std::vector<Need>> wanted;
    for (std::size_t m = 0; materials != nullptr && materials->is_array() && m < materials->array().size(); ++m) {
        const json::Value& material = materials->array()[m];
        std::string target;
        if (m < uses.size() && uses[m].handled) {
            const auto found = file_of.find({uses[m].tim, uses[m].row});
            if (found != file_of.end()) {
                target = found->second;
            }
        }
        if (!target.empty()) {
            if (const json::Value* extras = material.find("extras"); extras != nullptr) {
                if (const json::Value* png = extras->find("png"); png != nullptr && png->is_string()) {
                    replace_string(*png, target);
                }
            }
        }
        const json::Value* pbr = material.find("pbrMetallicRoughness");
        const json::Value* base = pbr != nullptr ? pbr->find("baseColorTexture") : nullptr;
        const json::Value* index = base != nullptr ? base->find("index") : nullptr;
        if (index == nullptr || !index->is_integer() || textures == nullptr || !textures->is_array() ||
            index->integer() < 0 || static_cast<std::size_t>(index->integer()) >= textures->array().size()) {
            continue;
        }
        const json::Value* source = textures->array()[static_cast<std::size_t>(index->integer())].find("source");
        if (source == nullptr || !source->is_integer() || source->integer() < 0) {
            continue;
        }
        wanted[static_cast<std::size_t>(source->integer())].push_back(
            {target, index, static_cast<std::size_t>(index->integer())});
    }
    auto relative_uri = [&](const std::string& target) {
        return (convert / target).lexically_relative(consumer.path.parent_path()).generic_string();
    };
    // An image that materials of several rows share (one texture per TIM, as the inventory models have) keeps one
    // picture; every other picture gets an image and textures of its own, appended to the arrays, and those
    // materials point at them. On the next run each image has one picture, so nothing more is added.
    std::string new_images;
    std::string new_textures;
    std::size_t image_count = images != nullptr && images->is_array() ? images->array().size() : 0;
    std::size_t texture_count = textures != nullptr && textures->is_array() ? textures->array().size() : 0;
    for (const auto& [image, needs] : wanted) {
        if (image >= image_count) {
            continue;
        }
        // The picture the image keeps: the untouched one if any material needs it, else the first material's.
        std::string keep = needs.front().target;
        for (const Need& need : needs) {
            if (need.target.empty()) {
                keep.clear();
            }
        }
        if (!keep.empty()) {
            if (const json::Value* uri = images->array()[image].find("uri"); uri != nullptr) {
                replace_string(*uri, relative_uri(keep));
            }
        }
        std::map<std::string, std::size_t> image_of_target;
        std::map<std::pair<std::string, std::size_t>, std::size_t> texture_of;
        for (const Need& need : needs) {
            if (need.target == keep) {
                continue;
            }
            auto [at, added] = image_of_target.emplace(need.target, image_count);
            if (added) {
                new_images += ",{\"uri\":" + json::quote(relative_uri(need.target)) + "}";
                ++image_count;
            }
            auto [tex, tex_added] = texture_of.emplace(std::make_pair(need.target, need.texture), texture_count);
            if (tex_added) {
                const json::Value* sampler = textures->array()[need.texture].find("sampler");
                new_textures += ",{";
                if (sampler != nullptr && sampler->is_integer()) {
                    new_textures += "\"sampler\":" + std::to_string(sampler->integer()) + ",";
                }
                new_textures += "\"source\":" + std::to_string(at->second) + "}";
                ++texture_count;
            }
            const std::size_t begin = offsets.of(*need.index);
            std::size_t end = begin;
            while (end < text.size() && ((text[end] >= '0' && text[end] <= '9') || text[end] == '-')) {
                ++end;
            }
            if (begin == std::string::npos || end == begin) {
                result.warnings.push_back(consumer.name + ": cannot locate a texture index");
                continue;
            }
            edits.push_back({begin, end, std::to_string(tex->second)});
        }
    }
    auto append_to = [&](const json::Value* array, const std::string& items) {
        std::size_t end = 0;
        if (items.empty()) {
            return;
        }
        if (array == nullptr || !value_end(text, offsets.of(*array), end) || end == 0 || text[end - 1] != ']') {
            result.warnings.push_back(consumer.name + ": cannot extend an array");
            return;
        }
        edits.push_back({end - 1, end - 1, items});
    };
    append_to(images, new_images);
    append_to(textures, new_textures);
    if (edits.empty()) {
        return result;
    }
    std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
    std::string json_text(text);
    for (const Edit& edit : edits) {
        json_text.replace(edit.begin, edit.end - edit.begin, edit.text);
    }
    while (json_text.size() % 4 != 0) {
        json_text += ' ';
    }
    // Header, the new JSON chunk, then every following chunk (the BIN) byte for byte.
    const std::string_view rest = std::string_view(glb.bytes).substr(glb.json_begin + glb.json_size);
    std::string out;
    out.reserve(20 + json_text.size() + rest.size());
    auto put32 = [&](std::uint32_t v) {
        char b[4];
        std::memcpy(b, &v, 4);
        out.append(b, 4);
    };
    put32(kGlbMagic);
    put32(2);
    put32(static_cast<std::uint32_t>(20 + json_text.size() + rest.size()));
    put32(static_cast<std::uint32_t>(json_text.size()));
    put32(kChunkJson);
    out += json_text;
    out += rest;
    std::string write_error;
    result.changed = write_if_changed(consumer.path, out, write_error);
    if (!write_error.empty()) {
        result.warnings.push_back(write_error);
    }
    return result;
}

// ---- driver -------------------------------------------------------------------------------------------------------

std::vector<Consumer> find_consumers(const fs::path& gamedata, std::vector<std::string>& warnings) {
    std::vector<Consumer> found;
    const std::pair<fs::path, bool> dirs[] = {{gamedata / "convert" / "models", true},
                                              {gamedata / "convert" / "levels", true},
                                              {gamedata / "maps" / "areas", true}};
    for (const auto& [dir, scene] : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) {
            continue;
        }
        for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".glb") {
                Consumer consumer;
                consumer.path = it->path();
                consumer.name = it->path().lexically_relative(gamedata).generic_string();
                consumer.scene = scene;
                found.push_back(std::move(consumer));
            }
        }
        if (ec) {
            warnings.push_back("walk " + dir.generic_string() + ": " + ec.message());
        }
    }
    std::sort(found.begin(), found.end(), [](const Consumer& a, const Consumer& b) { return a.name < b.name; });
    return found;
}

// Variant files an earlier bake listed in texture_bake.json.
std::set<std::string> previous_variants(const fs::path& json_path) {
    std::set<std::string> files;
    const std::string text = read_file(json_path);
    auto doc = json::parse(text);
    if (text.empty() || !doc) {
        return files;
    }
    const json::Value* textures = doc.value().find("textures");
    if (textures == nullptr || !textures->is_array()) {
        return files;
    }
    for (const json::Value& texture : textures->array()) {
        const json::Value* variants = texture.find("variants");
        if (variants == nullptr || !variants->is_object()) {
            continue;
        }
        for (const auto& [file, info] : variants->members()) {
            files.insert(file);
        }
    }
    return files;
}

std::string bake_json(const std::vector<TimBake>& bakes, const BakeReport& report) {
    json::Writer j(true);
    j.begin_object();
    j.key("description")
        .value("The texture bake of the import (importer 3). Every 4/8-bit TIM that a converted model, level or map "
               "area draws (or that draw_usage.json lists) is baked into true-colour pictures: 'file' shows every "
               "texel in the CLUT row most of the geometry draws it with ('base_rows'); each entry of 'variants' is "
               "the same picture with the texels of 'rows' recoloured, for the consumers listed. The glb files "
               "name these pictures in their images and material extras.png; extras tim/palette keep the original "
               "TIM and row. Alpha 128 marks STP texels drawn semi-transparent by some geometry. To retexture, edit "
               "'file' and the variants together; the importer rewrites all of them on a new import.");
    j.member("version", 1);
    j.key("stats").inline_next().begin_object();
    j.member("tims", report.tims).member("variants", report.variants).member("consumers", report.consumers);
    j.member("draw_usages", report.usages);
    j.end_object();
    j.key("textures").begin_array();
    for (const TimBake& bake : bakes) {
        if (!bake.baked) {
            continue;
        }
        j.begin_object();
        j.member("tim", bake.key).member("file", bake.base_file);
        j.key("rows").inline_next().begin_array();
        for (int row : bake.rows) {
            j.value(row);
        }
        j.end_array();
        j.key("base_rows").inline_next().begin_array();
        for (int row : bake.base_rows) {
            j.value(row);
        }
        j.end_array();
        j.member("texels", static_cast<std::uint64_t>(bake.texels))
            .member("covered", static_cast<std::uint64_t>(bake.covered));
        j.key("consumers").begin_array();
        for (const std::string& name : bake.base_consumers) {
            j.value(name);
        }
        j.end_array();
        j.key("variants").begin_object();
        for (const VariantOut& variant : bake.variants) {
            j.key(variant.file).begin_object();
            j.member("label", variant.label);
            j.key("rows").inline_next().begin_array();
            for (int row : variant.rows) {
                j.value(row);
            }
            j.end_array();
            j.key("consumers").begin_array();
            for (const std::string& name : variant.consumers) {
                j.value(name);
            }
            j.end_array();
            j.end_object();
        }
        j.end_object();
        j.end_object();
    }
    j.end_array();
    j.end_object();
    std::string text = j.take();
    text += '\n';
    return text;
}

}  // namespace

std::vector<std::uint8_t> coverage_mask(int width, int height, std::span<const float> uv) {
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(std::max(width, 0)) * std::max(height, 0), 0);
    if (width <= 0 || height <= 0) {
        return mask;
    }
    for (std::size_t i = 0; i + 6 <= uv.size(); i += 6) {
        std::int64_t u[3], v[3];
        bool usable = true;
        for (int k = 0; k < 3 && usable; ++k) {
            usable = texel_coordinate(uv[i + 2 * k], width, u[k]) && texel_coordinate(uv[i + 2 * k + 1], height, v[k]);
        }
        if (usable) {
            cover_triangle(width, height, u, v, [&](std::size_t texel) { mask[texel] = 1; });
        }
    }
    return mask;
}

std::string_view builtin_draw_usage_json() { return kBuiltinDrawUsageJson; }

Result<BakeReport> bake_textures(const fs::path& gamedata_dir, unsigned threads) {
    return bake_textures_with_usage(gamedata_dir, threads, builtin_draw_usage_json());
}

Result<BakeReport> bake_textures_with_usage(const fs::path& gamedata_dir, unsigned threads,
                                            std::string_view draw_usage_json) {
    const fs::path convert = gamedata_dir / "convert";
    std::error_code ec;
    if (!fs::is_directory(convert / "textures", ec)) {
        return Result<BakeReport>::fail("no " + (convert / "textures").generic_string() + "; run the import first");
    }
    if (threads == 0) {
        threads = std::max(1u, std::thread::hardware_concurrency());
    }
    BakeReport report;
    TimCatalog catalog(convert);
    Coverage coverage;

    // 1. Coverage from the consumers and the draw usage table.
    const std::vector<Consumer> consumers = find_consumers(gamedata_dir, report.warnings);
    std::vector<ScanResult> scans(consumers.size());
    parallel_for(consumers.size(), threads,
                 [&](std::size_t i) { scans[i] = scan_glb(consumers[i], catalog, coverage); });
    report.usages = apply_draw_usage(draw_usage_json, catalog, coverage, report.warnings);
    for (const ScanResult& scan : scans) {
        report.warnings.insert(report.warnings.end(), scan.warnings.begin(), scan.warnings.end());
        report.consumers += scan.handled ? 1u : 0u;
    }

    // 2. One bake per TIM.
    std::vector<const TimUse*> uses;
    for (const auto& [key, use] : coverage.tims) {
        uses.push_back(&use);
    }
    std::vector<TimBake> bakes(uses.size());
    parallel_for(uses.size(), threads, [&](std::size_t i) {
        bakes[i] = bake_tim(convert, *uses[i]);
    });
    std::map<std::pair<std::string, int>, std::string> file_of;
    std::set<std::string> variant_files;
    for (const TimBake& bake : bakes) {
        report.warnings.insert(report.warnings.end(), bake.warnings.begin(), bake.warnings.end());
        report.pictures_written += bake.written;
        if (!bake.baked) {
            continue;
        }
        ++report.tims;
        report.variants += static_cast<unsigned>(bake.variants.size());
        for (const auto& [row, file] : bake.file_of_row) {
            file_of[{bake.key, row}] = file;
        }
        for (const VariantOut& variant : bake.variants) {
            variant_files.insert(variant.file);
        }
    }

    // 3. The consumers name the baked pictures.
    std::vector<RewriteResult> rewrites(consumers.size());
    parallel_for(consumers.size(), threads, [&](std::size_t i) {
        if (scans[i].handled) {
            rewrites[i] = rewrite_glb(consumers[i], convert, file_of);
        }
    });
    for (const RewriteResult& rewrite : rewrites) {
        report.warnings.insert(report.warnings.end(), rewrite.warnings.begin(), rewrite.warnings.end());
        report.glbs_rewritten += rewrite.changed ? 1u : 0u;
    }
    for (const std::string& w : catalog.take_warnings()) {
        report.warnings.push_back(w);
    }

    // 4. Variants an earlier bake wrote and this one does not: only names this bake's own record lists, so a
    //    picture a mod or a user put next to the textures is never touched.
    const fs::path json_path = convert / "texture_bake.json";
    for (const std::string& file : previous_variants(json_path)) {
        if (variant_files.count(file) || file.rfind("textures/", 0) != 0 || file.find("..") != std::string::npos) {
            continue;
        }
        if (fs::remove(convert / file, ec)) {
            ++report.stale_removed;
        }
    }

    std::string error;
    write_atomic(json_path, bake_json(bakes, report), error);
    if (!error.empty()) {
        report.warnings.push_back(error);
    } else {
        report.json_path = json_path.generic_string();
    }
    return Result<BakeReport>::ok(std::move(report));
}

std::string format_bake_line(const BakeReport& report) {
    std::string line = "textures: " + std::to_string(report.tims) + " TIMs baked, " +
                       std::to_string(report.variants) + " variants, " + std::to_string(report.consumers) +
                       " consumers (" + std::to_string(report.glbs_rewritten) + " rewritten), " +
                       std::to_string(report.usages) + " draw usages, " + std::to_string(report.pictures_written) +
                       " pictures written";
    if (report.stale_removed != 0) {
        line += ", " + std::to_string(report.stale_removed) + " stale variants removed";
    }
    if (!report.warnings.empty()) {
        line += ", " + std::to_string(report.warnings.size()) + " warnings";
    }
    return line;
}

}  // namespace sh::convert::materials
