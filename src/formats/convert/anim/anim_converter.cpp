#include "formats/convert/anim/anim_converter.h"

#include "formats/convert/anim/anim_json.h"

#include <algorithm>
#include <cctype>
#include <mutex>

namespace sh::convert::anim {
namespace {

constexpr std::string_view kBase = kHarryBaseAnm;

constexpr std::string_view kHarryMapFiles[] = {
    "ANIM/HB_M0S00.ANM", "ANIM/HB_M0S01.ANM", "ANIM/HB_M0S02.ANM", "ANIM/HB_M1S00.ANM", "ANIM/HB_M1S01.ANM",
    "ANIM/HB_M1S02.ANM", "ANIM/HB_M1S03.ANM", "ANIM/HB_M1S04.ANM", "ANIM/HB_M1S05.ANM", "ANIM/HB_M1S06.ANM",
    "ANIM/HB_M2S00.ANM", "ANIM/HB_M2S01.ANM", "ANIM/HB_M2S02.ANM", "ANIM/HB_M2S03.ANM", "ANIM/HB_M2S04.ANM",
    "ANIM/HB_M3S00.ANM", "ANIM/HB_M3S01.ANM", "ANIM/HB_M3S02.ANM", "ANIM/HB_M3S03.ANM", "ANIM/HB_M3S04.ANM",
    "ANIM/HB_M3S05.ANM", "ANIM/HB_M3S06.ANM", "ANIM/HB_M4S00.ANM", "ANIM/HB_M4S01.ANM", "ANIM/HB_M4S02.ANM",
    "ANIM/HB_M4S03.ANM", "ANIM/HB_M4S04.ANM", "ANIM/HB_M4S05.ANM", "ANIM/HB_M4S06.ANM", "ANIM/HB_M5S00.ANM",
    "ANIM/HB_M5S01.ANM", "ANIM/HB_M5S02.ANM", "ANIM/HB_M5S03.ANM", "ANIM/HB_M6S00.ANM", "ANIM/HB_M6S01.ANM",
    "ANIM/HB_M6S02.ANM", "ANIM/HB_M6S03.ANM", "ANIM/HB_M6S04.ANM", "ANIM/HB_M6S05.ANM", "ANIM/HBM6_S4B.ANM",
    "ANIM/HBM6_S4C.ANM", "ANIM/HB_M7S00.ANM", "ANIM/HB_M7S01.ANM", "ANIM/HB_M7S02.ANM", "ANIM/HB_M7S03.ANM",
    "ANIM/HBM7_SLS.ANM", "ANIM/HB_MTS00.ANM", "ANIM/HB_MXS00.ANM",
};

std::string upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

bool is_anm_path(std::string_view path) {
    const std::string u = upper(path);
    return u.size() > 4 && u.compare(u.size() - 4, 4, ".ANM") == 0;
}

std::string stem_upper(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    return upper(name.substr(0, name.rfind('.')));
}

class AnimConverter final : public sh::convert::Converter {
public:
    explicit AnimConverter(SiblingReader sibling) : sibling_(std::move(sibling)) {}

    std::string_view name() const override { return "anim"; }
    bool lossless() const override { return true; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        if (!is_anm_path(game_path) || raw.empty()) {
            return false;
        }
        if (AnmFile::has_header(raw)) {
            return true;
        }
        // Headerless keyframes: only Harry's map files, and only with the base at hand.
        return stem_upper(game_path).rfind("HB", 0) == 0 && base() != nullptr;
    }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        using Out = Result<std::vector<Output>>;
        Result<AnmFile> file = Result<AnmFile>::fail("not an ANM");
        std::string base_path;
        if (AnmFile::has_header(raw)) {
            file = AnmFile::parse(raw);
        } else if (const AnmFile* b = base()) {
            file = AnmFile::parse_extension(raw, *b);
            base_path = std::string(kBase);
        }
        if (!file) {
            return Out::fail(std::string(game_path) + ": " + file.error());
        }
        if (file.value().serialize() != std::vector<std::uint8_t>(raw.begin(), raw.end())) {
            return Out::fail(std::string(game_path) + ": ANM does not serialise back to its bytes");
        }
        const std::string json = anm_to_json(file.value(), game_path, raw, base_path);
        std::string rel = std::string(game_path);
        rel = "anim/" + rel.substr(0, rel.rfind('.')) + ".anim.json";
        std::vector<Output> out;
        out.push_back({rel, std::vector<std::uint8_t>(json.begin(), json.end())});
        return Out::ok(std::move(out));
    }

    Result<std::vector<std::uint8_t>> encode(std::span<const Output> outputs) const override {
        using Out = Result<std::vector<std::uint8_t>>;
        for (const Output& o : outputs) {
            if (o.relative_path.size() > 10 && o.relative_path.ends_with(".anim.json")) {
                auto file = anm_from_json(std::string_view(reinterpret_cast<const char*>(o.bytes.data()), o.bytes.size()));
                if (!file) {
                    return Out::fail(o.relative_path + ": " + file.error());
                }
                return Out::ok(file.value().serialize());
            }
        }
        return Out::fail("anim: no .anim.json among the outputs");
    }

private:
    const AnmFile* base() const {
        // Converters run on several import threads at once.
        std::call_once(base_once_, [this] {
            if (sibling_) {
                if (auto bytes = sibling_(kBase)) {
                    if (auto parsed = AnmFile::parse(*bytes)) {
                        base_ = std::move(parsed.value());
                    }
                }
            }
        });
        return base_ ? &*base_ : nullptr;
    }

    SiblingReader sibling_;
    mutable std::once_flag base_once_;
    mutable std::optional<AnmFile> base_;
};

}  // namespace

std::span<const std::string_view> harry_map_keyframe_files() { return kHarryMapFiles; }

std::unique_ptr<sh::convert::Converter> make_converter(SiblingReader sibling) {
    return std::make_unique<AnimConverter>(std::move(sibling));
}

}  // namespace sh::convert::anim
