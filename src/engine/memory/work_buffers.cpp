// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/memory/work_buffers.h"

#include <array>
#include <cstring>
#include <memory>

namespace sh::memory {

namespace {

using B = WorkBuffer;

// Sizes of the files the buffers take, rounded up to whole 2 KB sectors (the reads write whole sectors).
constexpr std::size_t kSector = 0x800;
constexpr std::size_t kLargestPicture = 0x26000;      // TIM/TITLE_E.TIM, TIM/MP_*.TIM (152 KB)
constexpr std::size_t kLargestCharaModel = 0x4000;    // CHARA/*.ILM (SIBYL.ILM, 16 KB)
constexpr std::size_t kLargestCharaAnim = 0x2A000;    // ANIM/*.ANM of a character (WRM.ANM, 168 KB)
constexpr std::size_t kLargestItemModel = 0x2C000;    // ITEM/*.TMD (IT_005.TMD, 176 KB)
constexpr std::size_t kLargestCutscene = 0x6800;      // ANIM/*.DMS (LAST4.DMS, 26 KB)
constexpr std::size_t kLargestMapOverlay = 0x2C800;   // VIN/MAP*.BIN (MAP2_S00.BIN, 178 KB)
constexpr std::size_t kLargestMenuOverlay = 0x13000;  // VIN/STREAM.BIN, the largest file the menus stage (76 KB)
constexpr std::size_t kLargestLibrary = 0xA000;       // BG/*.PLM (THR_GLB.PLM, 40 KB)

// The map's code-side data, the demo's recording and the demo's setup share one block. The demo player puts the
// recording right below the setup file and plays a demo only when the recording does not reach into the map's data
// (DemoPlayer::place_play_buffer): the rule is "the map's data and the recording must fit in this block together",
// so the three live in one block of the size the rule was made for.
constexpr std::size_t kMapCodeBlock = 0x35088;
constexpr std::size_t kDemoSetupBytes = kSector;
constexpr std::size_t kDemoSetupAt = kMapCodeBlock - kDemoSetupBytes;
constexpr std::size_t kDemoPlayDefault = 0x8000;  // where the recording sits until a demo places it
static_assert(kLargestMapOverlay + kDemoPlayDefault <= kDemoSetupAt);

// The character animation area. Harry's base file comes first, and the game reads his weapon and map animation files
// at fixed places behind it: a clip keyframe past the end of the base file is read from those files by its address
// (AnimBank::keyframe_at), so the clip tables depend on these distances. The map's group animations are packed behind
// Harry's slot (AnimSlots::alloc), and the scenes' animation slots 29, 33 and 30 lie inside the same area: loading
// into a slot that covers a group's animation drops that group's slot (AnimSlots::overlaps) and the animation bank
// forgets what the read covered, so the slots must keep their places relative to the packed groups.
constexpr std::size_t kWeaponAnimAt = 0x15BB4;
constexpr std::size_t kPlayerMapAnimAt = 0x19D84;
constexpr std::size_t kCharaAnim29At = 0x38400;
constexpr std::size_t kCharaAnim33At = 0x49400;
constexpr std::size_t kCharaAnim30At = 0x4C400;
constexpr std::size_t kCharaAnimArea = kCharaAnim30At + kLargestCharaAnim;

// The ripples of the lake (9 KB) follow its projection scratch (2.3 KB) in the map's work buffer.
constexpr std::size_t kMapWork22 = 0x3000;
// The boot screens' frame packets: one 32 KB block per display buffer.
constexpr std::size_t kPacketBlocks = 2 * 0x8000;

struct Place {
    WorkBuffer buffer;
    WorkBuffer home;     // the buffer whose memory it lives in: itself unless it shares
    std::size_t at;      // where it starts in its home
    std::size_t bytes;   // a home's size; 0 for a buffer inside another's memory
};

constexpr Place own(WorkBuffer b, std::size_t bytes) { return {b, b, 0, bytes}; }
constexpr Place inside(WorkBuffer b, WorkBuffer home, std::size_t at) { return {b, home, at, 0}; }

constexpr std::array<Place, static_cast<std::size_t>(B::Count)> kPlaces = {{
    own(B::MapOverlayData, kMapCodeBlock),
    inside(B::DemoPlayBuffer, B::MapOverlayData, kDemoSetupAt - kDemoPlayDefault),
    inside(B::DemoSetup, B::MapOverlayData, kDemoSetupAt),
    own(B::PlayerModel, kLargestCharaModel),
    own(B::GroupModels, 3 * kLargestCharaModel),  // the map's three groups
    own(B::CharaModel31, kLargestCharaModel),
    own(B::CharaModel32, kLargestCharaModel),
    own(B::CharaModel34, kLargestCharaModel),
    own(B::SceneModel, kLargestCharaModel),
    own(B::PlayerBaseAnim, kCharaAnimArea),
    inside(B::WeaponAnim, B::PlayerBaseAnim, kWeaponAnimAt),
    inside(B::PlayerMapAnim, B::PlayerBaseAnim, kPlayerMapAnimAt),
    inside(B::CharaAnim29, B::PlayerBaseAnim, kCharaAnim29At),
    inside(B::CharaAnim33, B::PlayerBaseAnim, kCharaAnim33At),
    inside(B::CharaAnim30, B::PlayerBaseAnim, kCharaAnim30At),
    own(B::LibraryModels, kLargestLibrary),
    own(B::MapWork22, kMapWork22),
    own(B::MapWork25, kLargestCharaAnim),
    own(B::MapWork26, kLargestCharaAnim),
    own(B::MapWork27, kLargestCharaAnim),
    own(B::MapWork28, kLargestCharaAnim),
    own(B::MapWork35, kLargestCharaAnim),
    own(B::Cutscene11, kLargestCutscene),
    own(B::Cutscene13, kLargestCutscene),
    own(B::Cutscene14, kLargestCutscene),
    own(B::Cutscene15, kLargestCutscene),
    own(B::Cutscene16, kLargestCutscene),
    own(B::Cutscene17, kLargestCutscene),
    own(B::Cutscene18, kLargestCutscene),
    own(B::Cutscene19, kLargestCutscene),
    own(B::Cutscene20, kLargestCutscene),
    own(B::Cutscene24, kLargestCutscene),
    own(B::PacketArea, kPacketBlocks),
    own(B::BackgroundKeep, 192 * 240 * 2),  // 16-bit pixels of the kept rectangles
    own(B::PickupKeep, 160 * 240 * 2),
    own(B::StaffRollKeep, 32 * 480 * 2),
    own(B::MovieKeep, 160 * 240 * 2),
    own(B::FileStaging, kLargestPicture),
    own(B::TitleImage, kLargestPicture),
    own(B::SaveLoadImage, kLargestPicture),
    own(B::PaperMapSheet, kLargestPicture),
    own(B::PlateDoorImage, kLargestPicture),
    own(B::LoadingImage, kLargestPicture),
    own(B::EventImage, kLargestPicture),
    own(B::HeldItemImage, kLargestPicture),
    own(B::ScreenImage, kLargestPicture),
    own(B::MapTexture, kLargestPicture),
    own(B::SheetImage, kLargestPicture),
    own(B::InventoryModels, kLargestItemModel),
    own(B::CloseUpModel, kLargestItemModel),
    own(B::SaveScreenData, kSector * 2),
    own(B::EventModel, kLargestItemModel),
    own(B::SongFile, 0x5000),
    own(B::SoundBankHeader0, 0x1C20),
    own(B::SoundBankHeader1, 0x1C20),
    own(B::SoundBankHeader2, 0x1C20),
    own(B::SoundBankHeader3, 0x1C20),
}};
static_assert(kLargestMenuOverlay <= kLargestPicture);

constexpr const Place& place(WorkBuffer b) { return kPlaces[static_cast<std::size_t>(b)]; }

constexpr bool places_are_sound() {
    for (std::size_t i = 0; i < kPlaces.size(); ++i) {
        const Place& p = kPlaces[i];
        if (static_cast<std::size_t>(p.buffer) != i) return false;
        const Place& home = place(p.home);
        if (home.home != home.buffer || home.bytes == 0) return false;  // a home owns its memory
        if (p.home != p.buffer && (p.bytes != 0 || p.at >= home.bytes)) return false;
    }
    return true;
}
static_assert(places_are_sound(), "every buffer listed once, in enum order; shared names start inside their home");

constexpr std::size_t kCount = kPlaces.size();

}  // namespace

struct WorkBuffers::Memory {
    std::array<std::unique_ptr<std::byte[]>, kCount> homes;
};

WorkBuffers::WorkBuffers() : memory_(new Memory) {
    for (const Place& p : kPlaces) {
        if (p.home == p.buffer) {
            memory_->homes[static_cast<std::size_t>(p.buffer)] = std::make_unique<std::byte[]>(p.bytes);
        }
    }
}

WorkBuffers::~WorkBuffers() { delete memory_; }

void* WorkBuffers::data(WorkBuffer b, std::size_t offset) {
    const Place& p = place(b);
    return memory_->homes[static_cast<std::size_t>(p.home)].get() + p.at + offset;
}

std::size_t WorkBuffers::capacity(WorkBuffer b) {
    const Place& p = place(b);
    return place(p.home).bytes - p.at;
}

std::span<std::byte> WorkBuffers::span(WorkBuffer b) {
    return {static_cast<std::byte*>(data(b)), capacity(b)};
}

std::optional<std::size_t> WorkBuffers::room(const void* p) const {
    const auto at = reinterpret_cast<std::uintptr_t>(p);
    for (const Place& h : kPlaces) {
        if (h.home != h.buffer) continue;
        const auto begin = reinterpret_cast<std::uintptr_t>(memory_->homes[static_cast<std::size_t>(h.buffer)].get());
        if (at >= begin && at < begin + h.bytes) {
            return begin + h.bytes - at;
        }
    }
    // The address right behind a buffer still belongs to it, with no room left: a read that has filled the buffer
    // arrives there with its next sector, and must be told so rather than taken for memory of someone else.
    for (const Place& h : kPlaces) {
        if (h.home != h.buffer) continue;
        const auto begin = reinterpret_cast<std::uintptr_t>(memory_->homes[static_cast<std::size_t>(h.buffer)].get());
        if (at == begin + h.bytes) {
            return std::size_t{0};
        }
    }
    return std::nullopt;
}

void WorkBuffers::clear() {
    for (const Place& p : kPlaces) {
        if (p.home == p.buffer) {
            std::memset(memory_->homes[static_cast<std::size_t>(p.buffer)].get(), 0, p.bytes);
        }
    }
}

std::size_t WorkBuffers::total_bytes() {
    std::size_t total = 0;
    for (const Place& p : kPlaces) {
        total += p.home == p.buffer ? p.bytes : 0;
    }
    return total;
}

WorkBuffers& work_buffers() {
    // Built on first use: other files' statics take buffer addresses during start-up.
    static WorkBuffers s_buffers;
    return s_buffers;
}

}  // namespace sh::memory
