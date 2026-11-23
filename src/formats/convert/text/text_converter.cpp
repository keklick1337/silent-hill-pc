#include "formats/convert/text/text_converter.h"

#include "formats/common/crc32.h"
#include "formats/convert/text/mini_json.h"
#include "formats/convert/text/text_codec.h"
#include "formats/convert/text/text_layout.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace sh::convert::text {

namespace {

constexpr std::string_view kFormat = "sh-text/1";

std::string_view parent_dir(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view() : path.substr(0, slash);
}

std::string_view base_name(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// "VIN/MAP0_S00.BIN" -> "text/VIN/MAP0_S00"
std::string output_stem(const std::string& normalised_path) {
    std::string stem = normalised_path;
    const std::size_t dot = stem.rfind('.');
    if (dot != std::string::npos && dot > stem.rfind('/') + 1) {
        stem.erase(dot);
    }
    return "text/" + stem;
}

std::string entry_ref(const std::string& table, std::uint32_t id) {
    return table + "#" + std::to_string(id);
}

class TextConverter final : public Converter {
public:
    std::string_view name() const override { return "text"; }
    bool lossless() const override { return true; }

    bool accepts(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        return static_cast<bool>(analyze(raw, game_path));
    }

    Result<std::vector<Output>> decode(std::span<const std::uint8_t> raw, std::string_view game_path) const override {
        using R = Result<std::vector<Output>>;
        auto analysed = analyze(raw, game_path);
        if (!analysed) {
            return R::fail(analysed.error());
        }
        const Layout& layout = analysed.value();
        const std::string stem = output_stem(layout.game_path);
        const std::string frame_name = std::string(base_name(stem)) + ".frame.bin";

        std::vector<std::uint8_t> frame = layout.image;
        for (const Slot& slot : layout.slots) {
            std::fill_n(frame.begin() + slot.offset, slot.capacity, std::uint8_t{0});
        }

        std::string json;
        json += "{\n";
        json += "  \"format\": " + json::quote(kFormat) + ",\n";
        json += "  \"game\": \"SLUS-007.07\",\n";
        json += "  \"game_path\": " + json::quote(layout.game_path) + ",\n";
        json += "  \"source_size\": " + std::to_string(raw.size()) + ",\n";
        json += "  \"source_crc32\": " + std::to_string(sh::crc32(raw.data(), raw.size())) + ",\n";
        json += std::string("  \"encrypted\": ") + (layout.encrypted ? "true" : "false") + ",\n";
        json += "  \"image_base\": " + std::to_string(layout.base) + ",\n";
        json += "  \"frame\": " + json::quote(frame_name) + ",\n";
        json += "  \"tables\": [\n";
        for (std::size_t t = 0; t < layout.tables.size(); ++t) {
            const Table& table = layout.tables[t];
            json += "    {\n";
            json += "      \"name\": " + json::quote(table.name) + ",\n";
            json += "      \"vram\": " + std::to_string(table.vram) + ",\n";
            json += "      \"offset\": " + std::to_string(table.file_offset) + ",\n";
            json += "      \"count\": " + std::to_string(table.entries.size()) + ",\n";
            json += "      \"dialect\": " + json::quote(dialect_name(table.dialect)) + ",\n";
            json += "      \"entries\": [\n";
            for (std::uint32_t id = 0; id < table.entries.size(); ++id) {
                json += "        " + entry_json(layout, table, id);
                json += id + 1 < table.entries.size() ? ",\n" : "\n";
            }
            json += "      ]\n";
            json += t + 1 < layout.tables.size() ? "    },\n" : "    }\n";
        }
        json += "  ]\n}\n";

        std::vector<Output> outputs;
        outputs.push_back({stem + ".json", std::vector<std::uint8_t>(json.begin(), json.end())});
        outputs.push_back({stem + ".frame.bin", std::move(frame)});
        return R::ok(std::move(outputs));
    }

    Result<std::vector<std::uint8_t>> encode(std::span<const Output> outputs) const override {
        using R = Result<std::vector<std::uint8_t>>;
        const Output* json_output = nullptr;
        for (const Output& o : outputs) {
            if (o.relative_path.ends_with(".json")) {
                json_output = &o;
                break;
            }
        }
        if (json_output == nullptr) {
            return R::fail("no .json output");
        }
        const auto parsed = json::parse(std::string_view(reinterpret_cast<const char*>(json_output->bytes.data()),
                                                         json_output->bytes.size()));
        if (!parsed) {
            return R::fail(json_output->relative_path + ": " + parsed.error());
        }
        const json::Value& doc = parsed.value();

        const auto format = string_member(doc, "format");
        if (!format || *format != kFormat) {
            return R::fail("not an " + std::string(kFormat) + " file");
        }
        const auto frame_name = string_member(doc, "frame");
        const auto size = integer_member(doc, "source_size");
        const json::Value* encrypted = doc.find("encrypted");
        const json::Value* tables = doc.find("tables");
        if (!frame_name || !size || encrypted == nullptr || encrypted->kind != json::Value::Kind::Bool ||
            tables == nullptr || tables->kind != json::Value::Kind::Array) {
            return R::fail("missing frame, source_size, encrypted or tables");
        }

        const std::string frame_path =
            std::string(parent_dir(json_output->relative_path)) + "/" + *frame_name;
        const Output* frame_output = nullptr;
        for (const Output& o : outputs) {
            if (o.relative_path == frame_path) {
                frame_output = &o;
            }
        }
        if (frame_output == nullptr) {
            return R::fail("missing " + frame_path);
        }
        if (static_cast<std::int64_t>(frame_output->bytes.size()) != *size) {
            return R::fail(frame_path + " has the wrong size");
        }

        std::vector<Patch> patches;
        for (const json::Value& table : tables->items) {
            const auto table_name = string_member(table, "name");
            const auto table_dialect = string_member(table, "dialect");
            const json::Value* entries = table.find("entries");
            if (!table_name || !table_dialect || entries == nullptr || entries->kind != json::Value::Kind::Array) {
                return R::fail("a table has no name, dialect or entries");
            }
            for (const json::Value& entry : entries->items) {
                auto patch = entry_patch(entry, *table_name, *table_dialect, *size);
                if (!patch) {
                    return R::fail(patch.error());
                }
                if (patch.value().has_value()) {
                    patches.push_back(std::move(*patch.value()));
                }
            }
        }

        std::sort(patches.begin(), patches.end(), [](const Patch& a, const Patch& b) { return a.offset < b.offset; });
        for (std::size_t i = 1; i < patches.size(); ++i) {
            if (patches[i - 1].offset + patches[i - 1].capacity > patches[i].offset) {
                return R::fail("string slots overlap at file offset " + std::to_string(patches[i].offset));
            }
        }

        std::vector<std::uint8_t> image = frame_output->bytes;
        for (const Patch& patch : patches) {
            std::copy(patch.bytes.begin(), patch.bytes.end(), image.begin() + patch.offset);
        }
        if (encrypted->boolean) {
            if (image.size() % 4 != 0) {
                return R::fail("encrypted file size is not a multiple of 4");
            }
            image = xor_gamma(image);
        }
        return R::ok(std::move(image));
    }

private:
    static std::optional<std::string> string_member(const json::Value& object, std::string_view key) {
        const json::Value* v = object.find(key);
        if (v == nullptr || v->kind != json::Value::Kind::String) {
            return std::nullopt;
        }
        return v->string;
    }

    static std::optional<std::int64_t> integer_member(const json::Value& object, std::string_view key) {
        const json::Value* v = object.find(key);
        if (v == nullptr || v->kind != json::Value::Kind::Integer) {
            return std::nullopt;
        }
        return v->integer;
    }

    static std::string entry_json(const Layout& layout, const Table& table, std::uint32_t id) {
        const Entry& entry = table.entries[id];
        std::string out = "{\"id\": " + std::to_string(id);
        if (entry.null) {
            return out + ", \"null\": true}";
        }
        const Slot& slot = layout.slots[entry.slot];
        const std::span<const std::uint8_t> bytes(layout.image.data() + slot.offset, slot.length);
        Dialect dialect = table.dialect;
        auto text = bytes_to_text(bytes, dialect);
        if (!text) {
            dialect = Dialect::Ascii;
            text = bytes_to_text(bytes, dialect);
        }
        if (dialect != table.dialect) {
            out += ", \"dialect\": " + json::quote(dialect_name(dialect));
        }
        if (entry.alias) {
            out += ", \"alias_of\": " + json::quote(entry_ref(layout.tables[entry.owner_table].name, entry.owner_id));
        }
        out += ", \"text\": " + json::quote(*text);
        out += ", \"raw\": \"" + to_hex(bytes) + "\"";
        if (!entry.alias) {
            out += ", \"offset\": " + std::to_string(slot.offset) + ", \"capacity\": " + std::to_string(slot.capacity);
        }
        return out + "}";
    }

    struct Patch {
        std::uint32_t offset;
        std::uint32_t capacity;
        std::vector<std::uint8_t> bytes;
    };

    // The bytes to write for one entry, or no patch for null and alias entries.
    static Result<std::optional<Patch>> entry_patch(const json::Value& entry, const std::string& table_name,
                                                        const std::string& table_dialect, std::int64_t file_size) {
        using R = Result<std::optional<Patch>>;
        const auto id = integer_member(entry, "id");
        const std::string where = table_name + "#" + (id ? std::to_string(*id) : std::string("?"));
        if (!id) {
            return R::fail(table_name + ": an entry has no id");
        }
        const json::Value* null_flag = entry.find("null");
        if ((null_flag != nullptr && null_flag->kind == json::Value::Kind::Bool && null_flag->boolean) ||
            entry.find("alias_of") != nullptr) {
            return R::ok(std::nullopt);
        }

        std::string dialect_name_text = table_dialect;
        if (const auto own = string_member(entry, "dialect")) {
            dialect_name_text = *own;
        }
        const auto dialect = dialect_from_name(dialect_name_text);
        if (!dialect) {
            return R::fail(where + ": unknown dialect " + dialect_name_text);
        }
        const auto offset = integer_member(entry, "offset");
        const auto capacity = integer_member(entry, "capacity");
        if (!offset || !capacity || *offset < 0 || *capacity < 1 || *offset + *capacity > file_size) {
            return R::fail(where + ": offset or capacity is missing or outside the file");
        }

        const auto text = string_member(entry, "text");
        const auto raw_hex = string_member(entry, "raw");
        std::optional<std::vector<std::uint8_t>> raw;
        if (raw_hex) {
            raw = from_hex(*raw_hex);
            if (!raw) {
                return R::fail(where + ": raw is not hex");
            }
        }

        std::vector<std::uint8_t> bytes;
        if (raw && (!text || bytes_to_text(*raw, *dialect) == *text)) {
            bytes = std::move(*raw);
        } else if (text) {
            auto encoded = text_to_bytes(*text, *dialect);
            if (!encoded) {
                return R::fail(where + ": " + encoded.error());
            }
            bytes = std::move(encoded.value());
        } else {
            return R::fail(where + ": neither text nor raw");
        }
        if (std::find(bytes.begin(), bytes.end(), std::uint8_t{0}) != bytes.end()) {
            return R::fail(where + ": a string cannot contain a zero byte");
        }
        if (static_cast<std::int64_t>(bytes.size()) + 1 > *capacity) {
            return R::fail(where + ": " + std::to_string(bytes.size()) + " bytes do not fit the slot (" +
                           std::to_string(*capacity - 1) + " bytes and the NUL)");
        }
        return R::ok(Patch{static_cast<std::uint32_t>(*offset), static_cast<std::uint32_t>(*capacity),
                               std::move(bytes)});
    }
};

}  // namespace

std::unique_ptr<sh::convert::Converter> make_converter() {
    return std::make_unique<TextConverter>();
}

}  // namespace sh::convert::text
