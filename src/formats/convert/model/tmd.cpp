#include "formats/convert/model/tmd.h"

#include <initializer_list>

namespace sh::convert::model {
namespace {

constexpr std::size_t kHeaderBytes = 12;
constexpr std::size_t kObjectBytes = 28;
constexpr std::uint32_t kTmdId = 0x41;

std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}

bool inside(std::size_t size, std::size_t offset, std::size_t bytes) {
    return offset <= size && bytes <= size - offset;
}

}  // namespace

bool looks_like_tmd(std::span<const std::uint8_t> raw) {
    return raw.size() >= kHeaderBytes && le32(raw.data()) == kTmdId;
}

Result<Tmd> parse_tmd(std::span<const std::uint8_t> raw) {
    using Out = Result<Tmd>;
    if (!looks_like_tmd(raw)) {
        return Out::fail("not a TMD (id 0x41)");
    }
    const std::uint8_t* d = raw.data();
    Tmd tmd;
    tmd.flags = le32(d + 4);
    if (tmd.flags & 1) {
        return Out::fail("TMD with absolute addresses (flags bit 0) is not supported");
    }
    const std::size_t object_count = le32(d + 8);
    if (object_count == 0 || object_count > 4096 || !inside(raw.size(), kHeaderBytes, object_count * kObjectBytes)) {
        return Out::fail("TMD: object table outside the file");
    }

    for (std::size_t oi = 0; oi < object_count; ++oi) {
        const std::uint8_t* t = d + kHeaderBytes + oi * kObjectBytes;
        const std::string where = "object " + std::to_string(oi) + ": ";
        TmdObject obj;
        obj.table_offset = static_cast<std::uint32_t>(kHeaderBytes + oi * kObjectBytes);
        obj.vertex_offset = le32(t);
        const std::size_t vertex_count = le32(t + 4);
        obj.normal_offset = le32(t + 8);
        const std::size_t normal_count = le32(t + 12);
        obj.prim_offset = le32(t + 16);
        const std::size_t prim_count = le32(t + 20);
        obj.scale = static_cast<std::int32_t>(le32(t + 24));
        if (obj.scale < -16 || obj.scale > 16) {
            return Out::fail(where + "implausible scale exponent " + std::to_string(obj.scale));
        }

        const std::size_t vertex_at = kHeaderBytes + std::size_t{obj.vertex_offset};
        const std::size_t normal_at = kHeaderBytes + std::size_t{obj.normal_offset};
        const std::size_t prim_at = kHeaderBytes + std::size_t{obj.prim_offset};
        if (!inside(raw.size(), vertex_at, vertex_count * 8)) {
            return Out::fail(where + "vertices outside the file");
        }
        if (!inside(raw.size(), normal_at, normal_count * 8)) {
            return Out::fail(where + "normals outside the file");
        }
        for (std::size_t i = 0; i < vertex_count; ++i) {
            const std::uint8_t* v = d + vertex_at + i * 8;
            obj.vertices.push_back({static_cast<std::int16_t>(le16(v)), static_cast<std::int16_t>(le16(v + 2)),
                                    static_cast<std::int16_t>(le16(v + 4))});
        }
        for (std::size_t i = 0; i < normal_count; ++i) {
            const std::uint8_t* v = d + normal_at + i * 8;
            obj.normals.push_back({static_cast<std::int16_t>(le16(v)), static_cast<std::int16_t>(le16(v + 2)),
                                   static_cast<std::int16_t>(le16(v + 4))});
        }

        std::size_t pos = prim_at;
        for (std::size_t pi = 0; pi < prim_count; ++pi) {
            const std::string pwhere = where + "packet " + std::to_string(pi) + ": ";
            if (!inside(raw.size(), pos, 4)) {
                return Out::fail(pwhere + "header outside the file");
            }
            const std::size_t input_words = d[pos + 1];
            const std::size_t packet_bytes = 4 + input_words * 4;
            if (!inside(raw.size(), pos, packet_bytes)) {
                return Out::fail(pwhere + "packet outside the file");
            }

            TmdPrimitive p;
            p.offset = static_cast<std::uint32_t>(pos);
            p.flag = d[pos + 2];
            p.mode = d[pos + 3];
            if ((p.mode >> 5) != 1) {
                return Out::fail(pwhere + "unsupported packet kind " + std::to_string(p.mode >> 5) +
                                 " (only polygons are decoded)");
            }
            p.quad = (p.mode & 0x08) != 0;
            p.textured = (p.mode & 0x04) != 0;
            p.gouraud = (p.mode & 0x10) != 0;
            p.semi_transparent = (p.mode & 0x02) != 0;
            p.unlit = (p.flag & 0x01) != 0;
            p.double_sided = (p.flag & 0x02) != 0;
            p.corners = p.quad ? 4 : 3;
            const std::size_t corners = static_cast<std::size_t>(p.corners);

            const std::uint8_t* q = d + pos + 4;
            std::size_t used = 0;
            if (p.textured) {
                p.uv[0] = {q[0], q[1]};
                p.clut = le16(q + 2);
                p.uv[1] = {q[4], q[5]};
                p.tpage = le16(q + 6);
                p.uv[2] = {q[8], q[9]};
                used = 12;
                if (p.quad) {
                    p.uv[3] = {q[12], q[13]};
                    used = 16;
                }
            }
            // Bytes the layout needs after the texture block; checked against the packet's own size first.
            std::size_t need = used;
            if (p.unlit) {
                need += p.gouraud ? corners * 4 : 4;
                need += 8;
            } else {
                need += p.textured ? 0 : 4;
                need += p.gouraud ? corners * 4 : (p.quad ? 12 : 8);
            }
            if (need != input_words * 4) {
                return Out::fail(pwhere + "size " + std::to_string(input_words * 4) + " does not match the layout of mode " +
                                 std::to_string(p.mode) + " flag " + std::to_string(p.flag) + " (" + std::to_string(need) +
                                 ")");
            }

            const auto set_rgb = [&](std::size_t corner, const std::uint8_t* c) {
                p.rgb[corner] = {c[0], c[1], c[2]};
            };
            if (p.unlit) {
                set_rgb(0, q + used);
                if (p.gouraud) {
                    for (std::size_t k = 1; k < corners; ++k) {
                        set_rgb(k, q + used + k * 4);
                    }
                    used += corners * 4;
                } else {
                    for (std::size_t k = 1; k < corners; ++k) {
                        p.rgb[k] = p.rgb[0];
                    }
                    used += 4;
                }
                p.has_rgb = true;
                for (std::size_t k = 0; k < corners; ++k) {
                    p.vertex[k] = le16(q + used + k * 2);
                }
            } else {
                if (!p.textured) {
                    set_rgb(0, q + used);
                    for (std::size_t k = 1; k < corners; ++k) {
                        p.rgb[k] = p.rgb[0];
                    }
                    p.has_rgb = true;
                    used += 4;
                }
                if (p.gouraud) {
                    for (std::size_t k = 0; k < corners; ++k) {
                        p.normal[k] = le16(q + used + k * 4);
                        p.vertex[k] = le16(q + used + k * 4 + 2);
                    }
                } else {
                    const std::uint16_t n0 = le16(q + used);
                    for (std::size_t k = 0; k < corners; ++k) {
                        p.normal[k] = n0;
                        p.vertex[k] = le16(q + used + 2 + k * 2);
                    }
                }
            }

            for (std::size_t k = 0; k < corners; ++k) {
                if (p.vertex[k] >= obj.vertices.size()) {
                    return Out::fail(pwhere + "vertex index " + std::to_string(p.vertex[k]) + " out of range");
                }
                if (!p.unlit && p.normal[k] >= obj.normals.size()) {
                    return Out::fail(pwhere + "normal index " + std::to_string(p.normal[k]) + " out of range");
                }
            }
            obj.prims.push_back(p);
            pos += packet_bytes;
        }
        tmd.objects.push_back(std::move(obj));
    }
    return Out::ok(std::move(tmd));
}

namespace {

// One upload place of an item TIM (see tmd.h). `tim` empty: the key-item slot, whose TIM depends on the file.
struct ItemSlot {
    int page;           // texture page index (tpage bits 0..4)
    int u0, v0;         // page texel of the picture's top-left corner (4-bit: 4 texels per VRAM word)
    int clut_x, clut_y;
    int rows;
    int width, height;
    const char* tim;
};

constexpr ItemSlot kCommonSlot{15, 0, 0, 176, 0, 16, 256, 256, "TIM00"};
constexpr ItemSlot kKeySlot{14, 0, 0, 160, 0, 16, 256, 256, ""};
constexpr ItemSlot kModelSlot{13, 32 * 4, 0, 240, 0, 8, 128, 256, "TIM07"};
constexpr ItemSlot kFookSlot{5, 0, 128, 240, 8, 2, 128, 128, "FOOK"};

struct KeyTexture {
    const char* model;
    const char* tim;
};

// Map-group packs: the key TIM each one is loaded with.
constexpr KeyTexture kPackKeys[] = {
    {"IT_001", "TIM02"}, {"IT_002", "TIM01"}, {"IT_003", "TIM03"},
    {"IT_004", "TIM04"}, {"IT_005", "TIM05"}, {"IT_006", "TIM06"},
};

// UNQ models whose object occurs in the packs above with the same vertices, UVs and CLUTs, and whose drawn texels
// read the same colours in every key TIM of those packs (measured on the disc image; the first such TIM is named).
// Left out: UNQ30..32 (never loaded, no pack holds them), UNQ4E/4F/51/52 (TIM05 and TIM06 differ), UNQ65/66 (the
// bottle colours differ between the groups).
constexpr KeyTexture kUniqueKeys[] = {
    {"UNQ40", "TIM01"}, {"UNQ41", "TIM02"}, {"UNQ42", "TIM02"}, {"UNQ43", "TIM02"}, {"UNQ44", "TIM02"},
    {"UNQ45", "TIM01"}, {"UNQ46", "TIM01"}, {"UNQ47", "TIM01"}, {"UNQ48", "TIM02"}, {"UNQ49", "TIM03"},
    {"UNQ4A", "TIM03"}, {"UNQ4B", "TIM03"}, {"UNQ4C", "TIM02"}, {"UNQ4D", "TIM04"}, {"UNQ50", "TIM05"},
    {"UNQ53", "TIM02"}, {"UNQ54", "TIM02"}, {"UNQ55", "TIM01"}, {"UNQ60", "TIM01"}, {"UNQ61", "TIM01"},
    {"UNQ62", "TIM01"}, {"UNQ63", "TIM01"}, {"UNQ64", "TIM02"}, {"UNQ67", "TIM03"}, {"UNQ68", "TIM03"},
    {"UNQ69", "TIM03"}, {"UNQ6A", "TIM03"}, {"UNQ6B", "TIM03"}, {"UNQ6C", "TIM03"}, {"UNQ6D", "TIM03"},
    {"UNQ6E", "TIM02"}, {"UNQ70", "TIM04"}, {"UNQ71", "TIM04"}, {"UNQ72", "TIM04"}, {"UNQ73", "TIM04"},
    {"UNQ74", "TIM04"}, {"UNQ75", "TIM05"}, {"UNQ76", "TIM05"}, {"UNQ7A", "TIM05"}, {"UNQ7B", "TIM05"},
    {"UNQ7C", "TIM05"}, {"UNQ7D", "TIM06"}, {"UNQ7E", "TIM06"}, {"UNQ7F", "TIM06"},
};

template <std::size_t N>
const char* key_texture(const KeyTexture (&table)[N], std::string_view stem) {
    for (const KeyTexture& k : table) {
        if (stem == k.model) {
            return k.tim;
        }
    }
    return nullptr;
}

std::string upper_path(std::string_view game_path) {
    std::string p(game_path);
    for (char& c : p) {
        c = c == '\\' ? '/' : static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    while (!p.empty() && p.front() == '/') {
        p.erase(p.begin());
    }
    return p;
}

}  // namespace

std::optional<ItemTexture> resolve_item_texture(std::string_view game_path, std::uint16_t tpage, std::uint16_t clut) {
    const std::string path = upper_path(game_path);
    constexpr std::string_view kDir = "ITEM/";
    constexpr std::string_view kExt = ".TMD";
    if (path.size() <= kDir.size() + kExt.size() || path.compare(0, kDir.size(), kDir) != 0 ||
        path.compare(path.size() - kExt.size(), kExt.size(), kExt) != 0) {
        return std::nullopt;
    }
    const std::string stem = path.substr(kDir.size(), path.size() - kDir.size() - kExt.size());
    if (((tpage >> 7) & 3) != 0) {
        return std::nullopt;  // every item TIM is 4-bit
    }
    const int page = tpage & 31;
    const int clut_x = (clut & 63) * 16;
    const int clut_y = clut >> 6;

    const bool fook = stem == "FOOK";
    const bool pack = stem == "IT_000" || key_texture(kPackKeys, stem) != nullptr;
    const bool unique = stem.size() == 5 && stem.compare(0, 3, "UNQ") == 0;
    std::optional<ItemSlot> slot;
    std::string tim;
    if (fook) {
        if (page == kFookSlot.page) {
            slot = kFookSlot;
            tim = kFookSlot.tim;
        }
    } else if (pack || unique) {
        for (const ItemSlot& s : {kCommonSlot, kKeySlot, kModelSlot}) {
            if (s.page == page) {
                slot = s;
            }
        }
        if (slot && slot->tim[0] != '\0') {
            tim = slot->tim;
        } else if (slot) {
            const char* key = pack ? key_texture(kPackKeys, stem) : key_texture(kUniqueKeys, stem);
            if (key != nullptr) {
                tim = key;
            }
        }
    }
    if (!slot || tim.empty() || clut_x != slot->clut_x || clut_y < slot->clut_y ||
        clut_y >= slot->clut_y + slot->rows) {
        return std::nullopt;
    }
    ItemTexture out;
    out.tim = "ITEM/" + tim + ".TIM";
    out.palette = clut_y - slot->clut_y;
    out.u0 = slot->u0;
    out.v0 = slot->v0;
    out.width = slot->width;
    out.height = slot->height;
    return out;
}

std::optional<ItemTexture> resolve_key_sheet(std::uint16_t tpage, std::uint16_t clut, std::string_view key_tim) {
    const int clut_x = (clut & 63) * 16;
    const int clut_y = clut >> 6;
    if (key_tim.empty() || ((tpage >> 7) & 3) != 0 || (tpage & 31) != kKeySlot.page || clut_x != kKeySlot.clut_x ||
        clut_y < kKeySlot.clut_y || clut_y >= kKeySlot.clut_y + kKeySlot.rows) {
        return std::nullopt;
    }
    ItemTexture out;
    out.tim = std::string(key_tim);
    out.palette = clut_y - kKeySlot.clut_y;
    out.u0 = kKeySlot.u0;
    out.v0 = kKeySlot.v0;
    out.width = kKeySlot.width;
    out.height = kKeySlot.height;
    return out;
}

}  // namespace sh::convert::model
