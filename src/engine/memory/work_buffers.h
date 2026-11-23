// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The work buffers: the memory the game loads its files into and keeps its large work data in. Each buffer has a
// name and a capacity of its own (`sh::memory::work_buffers().data(WorkBuffer::FileStaging)`); the table in
// work_buffers.cpp says how big each one is.
//
// A few names share memory on purpose, because the game relates them by address (work_buffers.cpp says why for each):
// the map's code-side data, the demo's recording and the demo's setup; and the character animation area, where
// Harry's base, weapon and map animations, the map's group animations packed behind them and the scenes' animation
// slots live together. Every other buffer is independent.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace sh::memory {

// What each buffer holds. "Cutscene", "MapWork" and "CharaAnim"/"CharaModel" buffers carry the number the scripts
// address them by (the numbered file buffers, sh::scripting::file_buffer).
enum class WorkBuffer : std::uint8_t {
    MapOverlayData,     // the map's code-side data (records, tables) of the loaded map
    DemoPlayBuffer,     // the demo's recorded pad stream
    DemoSetup,          // the demo's setup file: settings, then the save it starts from
    PlayerModel,        // the player's model file (HERO.ILM)
    GroupModels,        // the map's character models, packed one behind another
    CharaModel31,       // a character model slot (model slot 0)
    CharaModel32,       // a character model slot (model slot 2)
    CharaModel34,       // a character model slot (model slot 1)
    SceneModel,         // a scene's second character model
    PlayerBaseAnim,     // the player's base animation file (HB_BASE.ANM), resident for the whole game
    WeaponAnim,         // the player's weapon animation, inside the character animation area
    PlayerMapAnim,      // the player's map animation, inside the character animation area
    CharaAnim29,        // a character animation slot (first bone set)
    CharaAnim33,        // a character animation slot (second bone set)
    CharaAnim30,        // a character animation slot (second bone set) and a map work table
    LibraryModels,      // the stream's shared model library (the global LM)
    MapWork22,          // the map's work buffer (the lake's projection scratch and ripples)
    MapWork25,          // a map's work table
    MapWork26,          // a map's work table; the ending's third animation
    MapWork27,          // a map's work table
    MapWork28,          // a map's work table
    MapWork35,          // a map's work table
    Cutscene11,         // a cutscene track (.DMS)
    Cutscene13,         // a cutscene track (.DMS)
    Cutscene14,         // a cutscene track (.DMS)
    Cutscene15,         // a cutscene track (.DMS)
    Cutscene16,         // a cutscene track (.DMS)
    Cutscene17,         // a cutscene track (.DMS)
    Cutscene18,         // a cutscene track (.DMS)
    Cutscene19,         // a cutscene track (.DMS)
    Cutscene20,         // a cutscene track (.DMS)
    Cutscene24,         // a cutscene track (.DMS)
    PacketArea,         // the boot screens' frame packets, one block per display buffer
    BackgroundKeep,     // video memory kept aside under an event background
    PickupKeep,         // video memory kept aside under the paper map pickup sheet
    StaffRollKeep,      // video memory kept aside while the staff roll runs (the column strip)
    MovieKeep,          // video memory kept aside under a movie or the staff roll (the right half)
    FileStaging,        // where most disc reads land before upload or parsing (pictures, menus, sound data)
    TitleImage,         // the title picture file
    SaveLoadImage,      // the save/load screen picture file
    PaperMapSheet,      // the paper map's sheet file, kept while the sheet is shown
    PlateDoorImage,     // the colour plate door's picture file
    LoadingImage,       // the loading screen picture file
    EventImage,         // an event picture file
    HeldItemImage,      // the held item's picture file
    ScreenImage,        // a menu picture file before its upload
    MapTexture,         // a map texture file before its upload
    SheetImage,         // an effect sheet or the staff roll's font file before its upload
    InventoryModels,    // the inventory's item models
    CloseUpModel,       // the inventory close-up model; the save screen's packed data
    SaveScreenData,     // the save screen's unpacked data
    EventModel,         // an event's model file
    SongFile,           // the loaded song
    SoundBankHeader0,   // a loaded sound bank's header, bank slot 0
    SoundBankHeader1,   // bank slot 1
    SoundBankHeader2,   // bank slot 2
    SoundBankHeader3,   // bank slot 3
    Count
};

class WorkBuffers {
public:
    WorkBuffers();
    ~WorkBuffers();
    WorkBuffers(const WorkBuffers&) = delete;
    WorkBuffers& operator=(const WorkBuffers&) = delete;

    // The buffer's byte `offset`.
    void* data(WorkBuffer b, std::size_t offset = 0);
    template <class T>
    T* as(WorkBuffer b, std::size_t offset = 0) {
        return static_cast<T*>(data(b, offset));
    }
    // The bytes a buffer can hold (for a name inside shared memory: from its first byte to that memory's end).
    static std::size_t capacity(WorkBuffer b);
    std::span<std::byte> span(WorkBuffer b);

    // The bytes from `p` to the end of the buffer memory it points into: what a read landing at `p` may write.
    // 0 for the address right behind a buffer; nullopt when `p` is not in or right behind any work buffer.
    std::optional<std::size_t> room(const void* p) const;

    // Zero every buffer.
    void clear();
    // The bytes of all buffers together.
    static std::size_t total_bytes();

private:
    struct Memory;
    Memory* memory_;
};

// The game's work buffers.
WorkBuffers& work_buffers();

}  // namespace sh::memory
