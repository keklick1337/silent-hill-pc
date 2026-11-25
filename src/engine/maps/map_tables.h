#pragma once

// The arrays of a map's disc overlay that the game reads: gamedata/maps/<map>/tables.json names
// each one, where it lies in the overlay (its address in PS1 memory, the overlay loaded at kOverlayBase), its
// element type and count. Loading a map makes zeroed storage for every array and fills it from the overlay file
// (the game file <bin>, formats/convert/gamefiles/game_files.h), as the PS1 did when it read the overlay; the storage
// lives until the next map load.
//
// tables.json:
//   "bin": the game path of the overlay file
//   "symbols": [{"symbol": name, "va": "0x...", "type": element type, "count": n, "bytes": prefix to copy,
//               "port_word": true (no place on the disc: zero storage for the visit)}]
//   "functions", "variables": [{"symbol": name, "va": "0x..."}]: game functions and live game variables the
//               overlay's pointers name
//
// A flat element type is copied as it is; a relocating one is copied field by field, its pointer fields (PS1
// addresses) translated to the storage they point into. The element types and the game's own addresses come from
// the game (MapTableTypes): their layouts are its structures'.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::maps {

inline constexpr std::uint32_t kOverlayBase = 0x800C9578u;
// The overlay's header (the PS1 layout) at the start of the file: the counts and pointer words the map's own
// data does not carry elsewhere.
inline constexpr std::size_t kOverlayHeaderBytes = 0x200;

// How one field of a relocating element is filled.
enum class FieldFill : std::uint8_t {
    Copy,            // `size` bytes as they are
    Pointer,         // a 4-byte PS1 address, translated to a pointer
    PointerOrValue,  // a word inside PS1 memory translates like a pointer, any other is a 4-byte value
};

struct TableField {
    std::uint16_t disc_offset;
    std::uint16_t offset;  // in the element of the storage
    std::uint16_t size;
    FieldFill fill;
};

struct TableType {
    std::uint16_t disc_size;
    std::uint16_t size;
    std::span<const TableField> fields;
};

// The element types and the game's addresses a tables.json may name (the game's structures and functions).
class MapTableTypes {
public:
    virtual ~MapTableTypes() = default;
    virtual const TableType* relocating(std::string_view type) const = 0;  // null: not one
    virtual unsigned flat_size(std::string_view type) const = 0;           // 0: not one
    virtual unsigned port_size(std::string_view type) const = 0;           // 0: not one (port words only)
    virtual void* function(std::string_view name) const = 0;
    virtual void* variable(std::string_view name) const = 0;
};

// The game's types, registered by the game at start; null before.
const MapTableTypes* map_table_types();
void set_map_table_types(const MapTableTypes* types);

class MapTables {
public:
    // Drops the previous map's arrays and reads the registry of `map` from its tables.json text. False (logged
    // under `relative`) when the text is broken.
    bool load_registry(std::string_view map, std::string_view relative, std::string_view text,
                       const MapTableTypes& types);
    // Fills every array from the overlay file's bytes and keeps its header. False (logged) when the overlay is
    // too short for the header; arrays outside the file stay zero (logged).
    bool fill(std::span<const std::uint8_t> overlay);
    void clear();

    bool loaded() const { return !map_.empty(); }
    const std::string& map() const { return map_; }
    const std::string& bin() const { return bin_; }

    // The storage of an array by its registry name and how many bytes the disc gives it; null for none.
    void* array(std::string_view name, std::uint32_t* bytes = nullptr);
    // A 16-bit number of the overlay header (PS1 layout offset); false before fill().
    bool header_s16(unsigned offset, std::int16_t* out) const;
    // The pointer word at `offset` of the overlay header, translated like a relocating field: null for 0, the
    // array that holds the address otherwise; an address no array holds is logged and gives null.
    void* header_pointer(unsigned offset) const;

private:
    struct Array {
        std::string name;
        std::uint32_t va = 0;
        std::uint32_t bytes = 0;  // flat: the bytes to copy; relocating: count * disc size
        std::uint32_t count = 0;
        const TableType* type = nullptr;  // relocating
        bool port_word = false;
        std::unique_ptr<std::max_align_t[]> storage;
    };
    struct External {
        std::uint32_t va;
        void* address;
    };

    // Where a PS1 address of the overlay points; the reason it cannot be translated, null when *out is set.
    const char* translate(std::uint32_t address, void** out) const;

    std::string map_;
    std::string bin_;
    std::vector<Array> arrays_;
    std::vector<External> externals_;
    std::vector<std::uint8_t> header_;
};

MapTables& map_tables();

}  // namespace sh::maps

// C faces: an array of the loaded map (null when `map` is not the loaded one or has no such
// array), any array of the loaded map by name, and a game function a table may name.
extern "C" const void* MapTables_Array(const char* map, const char* name, std::uint32_t* bytes);
extern "C" void* MapTables_DataSymbol(const char* name);
extern "C" void* MapTables_GameFunction(const char* name);
