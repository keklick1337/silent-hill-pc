#include "engine/maps/map_tables.h"

#include "engine/maps/map_table_data.h"

#include "engine/log/sh_log.h"
#include "formats/common/json.h"

#include <cstdlib>
#include <cstring>

namespace sh::maps {

namespace {

const MapTableTypes*& types_slot() {
    static const MapTableTypes* types = nullptr;
    return types;
}

bool hex_of(const sh::json::Value* v, std::uint32_t* out) {
    if (v == nullptr || !v->is_string()) {
        return false;
    }
    char* end = nullptr;
    const unsigned long n = std::strtoul(v->string().c_str(), &end, 16);
    *out = static_cast<std::uint32_t>(n);
    return end != nullptr && *end == '\0';
}

// Inside PS1 main memory (2 MB at 0x80000000).
bool console_ram_address(std::uint32_t word) { return word >= 0x80000000u && word < 0x80200000u; }

}  // namespace

const MapTableTypes* map_table_types() { return types_slot(); }
void set_map_table_types(const MapTableTypes* types) { types_slot() = types; }

void MapTables::clear() {
    map_.clear();
    bin_.clear();
    arrays_.clear();
    externals_.clear();
    header_.clear();
}

bool MapTables::load_registry(std::string_view map, std::string_view relative_view, std::string_view text,
                              const MapTableTypes& types) {
    clear();
    const std::string relative(relative_view);
    auto parsed = sh::json::parse(text);
    if (!parsed) {
        SH_ERR("[MAPTABLES] %s: %s", relative.c_str(), parsed.error().c_str());
        return false;
    }
    const sh::json::Value& root = parsed.value();
    const sh::json::Value* bin = root.find("bin");
    const sh::json::Value* symbols = root.find("symbols");
    if (bin == nullptr || !bin->is_string() || symbols == nullptr || !symbols->is_array()) {
        SH_ERR("[MAPTABLES] %s: needs \"bin\" and \"symbols\"", relative.c_str());
        return false;
    }
    for (const auto& [key, value] : root.members()) {
        if (key != "description" && key != "format" && key != "bin" && key != "base" && key != "symbols" &&
            key != "functions" && key != "variables") {
            SH_ERR("[MAPTABLES] %s: unknown key \"%s\" (ignored)", relative.c_str(), key.c_str());
        }
    }
    std::vector<Array> arrays;
    arrays.reserve(symbols->array().size());
    for (const sh::json::Value& s : symbols->array()) {
        const sh::json::Value* name = s.find("symbol");
        const sh::json::Value* type = s.find("type");
        const sh::json::Value* n = s.find("count");
        const sh::json::Value* port = s.find("port_word");
        Array a;
        a.port_word = port != nullptr && port->is_bool() && port->boolean();
        bool ok = name != nullptr && name->is_string() && type != nullptr && type->is_string() && n != nullptr &&
                  n->is_integer() && n->integer() > 0;
        if (ok && !a.port_word) {
            ok = hex_of(s.find("va"), &a.va);
        }
        if (!ok) {
            SH_ERR("[MAPTABLES] %s: a symbol needs \"symbol\", \"va\" (hex), \"type\" and \"count\"", relative.c_str());
            return false;
        }
        a.name = name->string();
        for (const auto& [key, value] : s.members()) {
            bool known = key == "symbol" || key == "va" || key == "type" || key == "count" || key == "bytes" ||
                         key == "port_word" || key == "description";
            for (std::string_view k : kTableDataKeys) {
                known = known || key == k;
            }
            if (!known) {
                SH_ERR("[MAPTABLES] %s: %s has the unknown key \"%s\" (ignored)", relative.c_str(), a.name.c_str(),
                       key.c_str());
            }
        }
        a.count = static_cast<std::uint32_t>(n->integer());
        std::size_t storage_bytes = 0;
        if (const TableType* t = types.relocating(type->string())) {
            a.type = t;
            a.bytes = a.count * t->disc_size;
            storage_bytes = static_cast<std::size_t>(a.count) * t->size;
        } else if (const unsigned size = types.flat_size(type->string())) {
            const sh::json::Value* b = s.find("bytes");
            a.bytes = a.port_word ? 0
                      : b != nullptr && b->is_integer() ? static_cast<std::uint32_t>(b->integer())
                                                        : a.count * size;
            storage_bytes = static_cast<std::size_t>(a.count) * size;
            if (a.bytes > storage_bytes) {
                SH_ERR("[MAPTABLES] %s: %s copies %u bytes into %zu", relative.c_str(), a.name.c_str(),
                       static_cast<unsigned>(a.bytes), storage_bytes);
                return false;
            }
        } else if (const unsigned port_size = a.port_word ? types.port_size(type->string()) : 0) {
            storage_bytes = static_cast<std::size_t>(a.count) * port_size;
        } else {
            SH_ERR("[MAPTABLES] %s: %s has the unknown type %s", relative.c_str(), a.name.c_str(),
                   type->string().c_str());
            return false;
        }
        const std::size_t words = (storage_bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
        a.storage.reset(new std::max_align_t[words > 0 ? words : 1]());
        arrays.push_back(std::move(a));
    }
    std::vector<External> externals;
    const auto add_externals = [&](const char* key, bool function) {
        const sh::json::Value* list = root.find(key);
        if (list == nullptr) {
            return true;
        }
        for (const sh::json::Value& f : list->array()) {
            std::uint32_t va = 0;
            const sh::json::Value* name = f.find("symbol");
            void* address = nullptr;
            if (hex_of(f.find("va"), &va) && name != nullptr && name->is_string()) {
                address = function ? types.function(name->string()) : types.variable(name->string());
            }
            if (address == nullptr) {
                SH_ERR("[MAPTABLES] %s: a %s needs \"va\" (hex) and a known game %s \"symbol\"", relative.c_str(),
                       function ? "function" : "variable", function ? "function" : "variable");
                return false;
            }
            externals.push_back({va, address});
        }
        return true;
    };
    if (!add_externals("functions", true) || !add_externals("variables", false)) {
        return false;
    }
    map_ = std::string(map);
    bin_ = bin->string();
    arrays_ = std::move(arrays);
    externals_ = std::move(externals);
    return true;
}

const char* MapTables::translate(std::uint32_t p, void** out) const {
    if (p == 0) {
        *out = nullptr;
        return nullptr;
    }
    for (const External& e : externals_) {
        if (e.va == p) {
            *out = e.address;
            return nullptr;
        }
    }
    // An array that starts exactly at the address wins over one whose range only covers it (map1_s03's drip
    // table lies inside the water table's 256-byte capacity).
    for (const Array& a : arrays_) {
        if (a.va == p) {
            *out = a.storage.get();
            return nullptr;
        }
    }
    for (const Array& a : arrays_) {
        if (a.type == nullptr) {
            if (p >= a.va && p < a.va + a.bytes) {
                *out = reinterpret_cast<char*>(a.storage.get()) + (p - a.va);
                return nullptr;
            }
        } else if (p >= a.va && p < a.va + a.count * a.type->disc_size) {
            const std::uint32_t off = p - a.va;
            if (off % a.type->disc_size != 0) {
                return "points mid-element";
            }
            *out = reinterpret_cast<char*>(a.storage.get()) + (off / a.type->disc_size) * a.type->size;
            return nullptr;
        }
    }
    return "points outside the overlay data";
}

bool MapTables::fill(std::span<const std::uint8_t> overlay) {
    header_.clear();
    if (overlay.size() >= kOverlayHeaderBytes) {
        header_.assign(overlay.begin(), overlay.begin() + kOverlayHeaderBytes);
    }
    unsigned filled = 0;
    std::uint64_t copied = 0;
    unsigned reloc_errors = 0;
    for (Array& a : arrays_) {
        if (a.port_word && a.type == nullptr) {
            continue;  // zero for the visit
        }
        const std::uint64_t bytes = a.bytes;
        if (a.va < kOverlayBase || static_cast<std::uint64_t>(a.va - kOverlayBase) + bytes > overlay.size()) {
            SH_ERR("[MAPTABLES] %s: %s at %08X is outside %s (%llu bytes)", map_.c_str(), a.name.c_str(),
                   static_cast<unsigned>(a.va), bin_.c_str(), static_cast<unsigned long long>(overlay.size()));
            continue;
        }
        const std::uint8_t* src = overlay.data() + (a.va - kOverlayBase);
        auto* dst = reinterpret_cast<std::uint8_t*>(a.storage.get());
        if (a.type == nullptr) {
            std::memcpy(dst, src, a.bytes);
            ++filled;
            copied += a.bytes;
            continue;
        }
        bool ok = true;
        for (std::uint32_t el = 0; el < a.count && ok; ++el) {
            std::uint8_t* el_dst = dst + static_cast<std::size_t>(el) * a.type->size;
            const std::uint8_t* el_src = src + static_cast<std::size_t>(el) * a.type->disc_size;
            for (std::size_t f = 0; f < a.type->fields.size(); ++f) {
                const TableField& fld = a.type->fields[f];
                if (fld.fill == FieldFill::Copy) {
                    std::memcpy(el_dst + fld.offset, el_src + fld.disc_offset, fld.size);
                    continue;
                }
                std::uint32_t p = 0;
                std::memcpy(&p, el_src + fld.disc_offset, sizeof(p));  // the disc is little-endian
                if (fld.fill == FieldFill::PointerOrValue && !console_ram_address(p)) {
                    // The value half of the union: the rest of the pointer-sized slot stays zero.
                    std::memset(el_dst + fld.offset, 0, sizeof(void*));
                    std::memcpy(el_dst + fld.offset, &p, sizeof(p));
                    continue;
                }
                void* pc = nullptr;
                if (const char* why = translate(p, &pc)) {
                    SH_ERR("[MAPTABLES] %s: %s[%u] field %u (psx_off %u): pointer %08X %s", map_.c_str(),
                           a.name.c_str(), static_cast<unsigned>(el), static_cast<unsigned>(f), fld.disc_offset,
                           static_cast<unsigned>(p), why);
                    ++reloc_errors;
                    ok = false;  // the element is half-built; stop rather than fill garbage
                    break;
                }
                std::memcpy(el_dst + fld.offset, &pc, sizeof(pc));
            }
        }
        if (ok) {
            ++filled;
        }
    }
    SH_LOG("[MAPTABLES] %s: %u arrays, %llu bytes from raw/%s, %u relocation errors (data-only map)", map_.c_str(),
           filled, static_cast<unsigned long long>(copied), bin_.c_str(), reloc_errors);
    return !header_.empty();
}

void* MapTables::array(std::string_view name, std::uint32_t* bytes) {
    for (Array& a : arrays_) {
        if (a.name == name) {
            if (bytes != nullptr) {
                *bytes = a.bytes;
            }
            return a.storage.get();
        }
    }
    return nullptr;
}

bool MapTables::header_s16(unsigned offset, std::int16_t* out) const {
    if (header_.empty() || offset + sizeof(*out) > header_.size()) {
        return false;
    }
    std::memcpy(out, header_.data() + offset, sizeof(*out));  // the disc is little-endian
    return true;
}

void* MapTables::header_pointer(unsigned offset) const {
    if (header_.empty() || offset + sizeof(std::uint32_t) > header_.size()) {
        return nullptr;
    }
    std::uint32_t p = 0;
    std::memcpy(&p, header_.data() + offset, sizeof(p));
    void* pc = nullptr;
    if (const char* why = translate(p, &pc)) {
        SH_ERR("[MAPTABLES] %s: header word %03X: pointer %08X %s", map_.c_str(), offset, static_cast<unsigned>(p),
               why);
        return nullptr;
    }
    return pc;
}

MapTables& map_tables() {
    static MapTables instance;
    return instance;
}

}  // namespace sh::maps

extern "C" const void* MapTables_Array(const char* map, const char* name, std::uint32_t* bytes) {
    sh::maps::MapTables& t = sh::maps::map_tables();
    if (map == nullptr || name == nullptr || t.map() != map) {
        return nullptr;
    }
    std::uint32_t n = 0;
    void* a = t.array(name, &n);
    if (a != nullptr && bytes != nullptr) {
        *bytes = n;
    }
    return a;
}

extern "C" void* MapTables_DataSymbol(const char* name) {
    return name != nullptr ? sh::maps::map_tables().array(name) : nullptr;
}

extern "C" void* MapTables_GameFunction(const char* name) {
    const sh::maps::MapTableTypes* types = sh::maps::map_table_types();
    return types != nullptr && name != nullptr ? types->function(name) : nullptr;
}
