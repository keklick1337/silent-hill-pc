#include "engine/chara/chara_library.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/game_state/game_state.h"
#include "engine/ids/game_numbers.h"
#include "engine/loader/queue/disc_files.h"
namespace gs = sh::game_state;
#include <SDL3/SDL_timer.h>
/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdio.h>
#include <string.h>

#include "engine/log/sh_log.h"
#include "engine/debug/dbg_trace.h"


static const char* const CHARA_NAMES[gs::chara::kCount] = {
    "none", "Harry", "AirScreamer", "NightFlutter", "Groaner", "Wormhead",
    "LarvalStalker", "Stalker", "GreyChild", "Mumbler", "HangedScratcher",
    "Creeper", "Romper", "Chicken", "SplitHead", "Floatstinger", "PuppetNurse",
    "DummyNurse", "PuppetDoctor", "DummyDoctor", "Twinfeeler", "Bloodsucker",
    "Incubus", "Unknown23", "MonsterCybil", "LockerDeadBody", "Cybil",
    "EndingCybil", "Cheryl", "Cat", "Dahlia", "EndingDahlia", "Lisa",
    "BloodyLisa", "Alessa", "GhostChildAlessa", "Incubator", "BloodyIncubator",
    "Kaufmann", "EndingKaufmann", "Flauros", "LittleIncubus", "GhostDoctor",
    "Parasite", "Padlock",
};

static const char* const GAME_STATE_NAMES[] = {
    "Init", "KonamiLogo", "KcetLogo", "MovieIntroFadeIn", "AutoLoadSavegame",
    "MovieIntroAlternate", "MovieIntro", "MainMenu", "LoadSavegameScreen",
    "MovieOpening", "MainLoadScreen", "InGame", "MapEvent", "ExitMovie",
    "InventoryScreen", "PaperMapScreen", "SaveScreen", "DebugMoviePlayer",
    "OptionScreen", "LoadStatusScreen", "LoadMapScreen", "Unk15", "Unk16",
};

static const char* const SYS_STATE_NAMES[] = {
    "Gameplay", "OptionsMenu", "StatusMenu", "MapScreen", "Fmv", "LoadOverlay",
    "LoadRoom", "ReadMessage", "SaveMenu0", "SaveMenu1", "EventCallback",
    "EventSetFlag", "EventPlaySound", "GameOver", "GamePaused", "Invalid",
};

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

const char* DbgTrace_CharaName(int charaId)
{
    if (charaId >= 0 && charaId < gs::chara::kCount)
        return CHARA_NAMES[charaId];
    /* A mod's new character: its id ("<mod>:<Name>"). */
    if (const sh::chara::CharacterDef* def = sh::chara::type_def(charaId))
        return def->id.c_str();
    return "?";
}

const char* DbgTrace_GameStateName(int state)
{
    return (state >= 0 && state < ARRAY_LEN(GAME_STATE_NAMES)) ? GAME_STATE_NAMES[state] : "?";
}

const char* DbgTrace_SysStateName(int state)
{
    return (state >= 0 && state < ARRAY_LEN(SYS_STATE_NAMES)) ? SYS_STATE_NAMES[state] : "?";
}

const char* DbgTrace_EventFlagName(int flag)
{
    // The name without its namespace; the registry's placeholders (Flag<N>) carry no name worth printing.
    const char* full = sh::ids::flag_name(flag);
    if (full == nullptr)
        return nullptr;
    const char* colon = strchr(full, ':');
    const char* name = colon != nullptr ? colon + 1 : full;
    if (strncmp(name, "Flag", 4) == 0 && name[4] >= '0' && name[4] <= '9')
        return nullptr;
    return name;
}

/* One sector of the disc. */
#define DISC_SECTOR_SIZE 2048

const char* DbgTrace_FileName(int file, char* buf, int bufSize)
{
    if (bufSize <= 0)
        return "";
    const sh::loader::DiscFiles& files = sh::loader::disc_files();
    snprintf(buf, bufSize, "%s", files.valid(file) ? files.path(file).c_str() : "?");
    return buf;
}

void DbgTrace_FileRead(int file, const char* source)
{
    char path[48];

    if (!SH_LOG_ON(FS, SH_LOGLV_DEBUG))
        return;
    const sh::loader::DiscFiles& files = sh::loader::disc_files();
    DbgTrace_FileName(file, path, sizeof(path));
    if (files.valid(file))
        SH_LOG_DEBUG(FS, "read %s (%u bytes, #%d) <- %s", path, (unsigned)files.bytes(file), file,
                     (source != NULL) ? source : "cd");
    else
        SH_LOG_DEBUG(FS, "read %s <- %s", path, (source != NULL) ? source : "cd");
    if (source == NULL && files.valid(file))
        SH_LOG_TRACE(CD, "%s: lba %u, %u sectors", path, (unsigned)files.first_sector(file),
                     (unsigned)((files.bytes(file) + DISC_SECTOR_SIZE - 1) / DISC_SECTOR_SIZE));
}

/* ---- TIMs in VRAM, for `textures`: a new upload drops the ones it covers. */

#define TIM_TRACK_MAX 256

typedef struct
{
    char     name[32];
    short      x, y, w, h; /* VRAM halfword units */
    short      cx, cy, cw, ch;
    unsigned char       bpp;
    unsigned char       virtualSlot;
    unsigned frame;
} s_DbgTim;

static s_DbgTim s_tims[TIM_TRACK_MAX];
static int      s_timCount = 0;
static unsigned s_frame    = 0;

static int RectOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

void DbgTrace_TimLoaded(int file, int x, int y, int w, int h, int bpp,
                        int clutX, int clutY, int clutW, int clutH, int virtualSlot)
{
    char      name[32];
    s_DbgTim* t;
    int       i, out;

    DbgTrace_FileName(file, name, sizeof(name));

    /* Pool slots alias real pages, so they never evict a VRAM resident. */
    if (!virtualSlot)
    {
        for (i = 0, out = 0; i < s_timCount; i++)
        {
            if (!s_tims[i].virtualSlot &&
                RectOverlap(s_tims[i].x, s_tims[i].y, s_tims[i].w, s_tims[i].h, x, y, w, h))
                continue;
            if (out != i)
                s_tims[out] = s_tims[i];
            out++;
        }
        s_timCount = out;
    }
    for (i = 0; i < s_timCount; i++)
    {
        if (strcmp(s_tims[i].name, name) == 0 && s_tims[i].virtualSlot == (unsigned char)(virtualSlot != 0))
            break;
    }
    if (i == s_timCount)
    {
        if (s_timCount == TIM_TRACK_MAX)
        {
            memmove(&s_tims[0], &s_tims[1], sizeof(s_tims[0]) * (TIM_TRACK_MAX - 1));
            s_timCount--;
        }
        i = s_timCount++;
    }
    t = &s_tims[i];
    snprintf(t->name, sizeof(t->name), "%s", name);
    t->x = (short)x; t->y = (short)y; t->w = (short)w; t->h = (short)h;
    t->cx = (short)clutX; t->cy = (short)clutY; t->cw = (short)clutW; t->ch = (short)clutH;
    t->bpp         = (unsigned char)bpp;
    t->virtualSlot = (unsigned char)(virtualSlot != 0);
    t->frame       = s_frame;

    if (clutW > 0)
        SH_LOG_DEBUG(TEXTURE, "TIM %s %dbpp -> VRAM (%d,%d) %dx%d, CLUT (%d,%d) %dx%d%s", name, bpp, x, y, w, h,
                     clutX, clutY, clutW, clutH, virtualSlot ? " [pool slot]" : "");
    else
        SH_LOG_DEBUG(TEXTURE, "TIM %s %dbpp -> VRAM (%d,%d) %dx%d, no CLUT%s", name, bpp, x, y, w, h,
                     virtualSlot ? " [pool slot]" : "");
}

int DbgTrace_TimCount(void)
{
    return s_timCount;
}

int DbgTrace_TimGet(int idx, char* line, int lineSize)
{
    const s_DbgTim* t;

    if (idx < 0 || idx >= s_timCount)
        return 0;
    t = &s_tims[idx];
    if (t->cw > 0)
        snprintf(line, lineSize, "%-18s %2dbpp vram %4d,%3d %3dx%-3d clut %4d,%3d %3dx%d%s f%u", t->name, t->bpp,
                 t->x, t->y, t->w, t->h, t->cx, t->cy, t->cw, t->ch, t->virtualSlot ? " pool" : "", t->frame);
    else
        snprintf(line, lineSize, "%-18s %2dbpp vram %4d,%3d %3dx%-3d no clut%s f%u", t->name, t->bpp, t->x, t->y,
                 t->w, t->h, t->virtualSlot ? " pool" : "", t->frame);
    return 1;
}

/* ---- movies ---- */

static char   s_fmvName[32];
static char   s_fmvLast[32];
static int    s_fmvPlaying = 0;
static int    s_fmvFrames  = 0;
static int    s_fmvLastFrames = 0;
static Uint32 s_fmvStartMs = 0;

void DbgTrace_FmvBegin(const char* name, int fileIdx, int maxFrames)
{
    snprintf(s_fmvName, sizeof(s_fmvName), "%s", name ? name : "?");
    s_fmvPlaying = 1;
    s_fmvFrames  = 0;
    s_fmvStartMs = SDL_GetTicks();
    SH_LOG_INFO(FMV, "start %s (file #%d%s%d)", s_fmvName, fileIdx, maxFrames > 0 ? ", max frames " : "",
                maxFrames > 0 ? maxFrames : 0);
}

void DbgTrace_FmvFrame(void)
{
    s_fmvFrames++;
    if (s_fmvFrames % 150 == 0)
        SH_LOG_TRACE(FMV, "%s frame %d", s_fmvName, s_fmvFrames);
}

void DbgTrace_FmvEnd(int result)
{
    Uint32 ms = SDL_GetTicks() - s_fmvStartMs;

    SH_LOG_INFO(FMV, "end %s: %d frames shown, %.1f s%s", s_fmvName, s_fmvFrames, ms / 1000.0f,
                result != 0 ? " (failed)" : "");
    snprintf(s_fmvLast, sizeof(s_fmvLast), "%s", s_fmvName);
    s_fmvLastFrames = s_fmvFrames;
    s_fmvPlaying    = 0;
}

int DbgTrace_FmvStatus(char* line, int lineSize)
{
    if (s_fmvPlaying)
        snprintf(line, lineSize, "playing %s, frame %d", s_fmvName, s_fmvFrames);
    else if (s_fmvLast[0])
        snprintf(line, lineSize, "no movie playing; last %s (%d frames)", s_fmvLast, s_fmvLastFrames);
    else
        snprintf(line, lineSize, "no movie played yet");
    return s_fmvPlaying;
}

/* ---- frame time, for `fps` ---- */

#define FRAME_HIST 240

static float  s_frameMs[FRAME_HIST];
static int    s_frameHead = 0;
static int    s_frameFill = 0;
static Uint64 s_framePrev = 0;

static void FrameTimeTick(void)
{
    Uint64 now = SDL_GetPerformanceCounter();

    if (s_framePrev != 0)
    {
        s_frameMs[s_frameHead] = (float)((double)(now - s_framePrev) * 1000.0 / (double)SDL_GetPerformanceFrequency());
        s_frameHead = (s_frameHead + 1) % FRAME_HIST;
        if (s_frameFill < FRAME_HIST)
            s_frameFill++;
    }
    s_framePrev = now;
}

void DbgTrace_FrameStats(float* avgMs, float* minMs, float* maxMs, int* samples)
{
    float sum = 0.0f, lo = 0.0f, hi = 0.0f;
    int   i;

    for (i = 0; i < s_frameFill; i++)
    {
        float v = s_frameMs[i];
        sum += v;
        if (i == 0 || v < lo) lo = v;
        if (i == 0 || v > hi) hi = v;
    }
    *avgMs   = s_frameFill ? sum / (float)s_frameFill : 0.0f;
    *minMs   = lo;
    *maxMs   = hi;
    *samples = s_frameFill;
}

/* ---- watchers ---- */

typedef struct
{
    signed char  charaId;
    unsigned char  dead;
} s_DbgNpcSeen;

static s_DbgNpcSeen s_npcSeen[gs::kNpcSlots];
static int          s_mapSeen       = -1;
static int          s_roomSeen      = -1;
static int          s_gameStateSeen = -1;
static int          s_sysStateSeen  = -1;
static unsigned int          s_flagsSeen[gs::kEventFlagWords];
static int          s_flagsValid    = 0;

static void WatchNpcs(void)
{
    int i;

    for (i = 0; i < gs::kNpcSlots; i++)
    {
        const int                id   = gs::chara_id(i + 1);
        const int                dead = gs::chara_health(i + 1) <= 0;
        const sh::entities::Vec3 pos  = gs::chara_position(i + 1);

        if (id == s_npcSeen[i].charaId)
        {
            if (id != gs::chara::kNone && dead && !s_npcSeen[i].dead)
                SH_LOG_INFO(SPAWN, "slot %d %s died at (%.2f, %.2f, %.2f)", i, DbgTrace_CharaName(id),
                            pos.x / 4096.0f, pos.y / 4096.0f, pos.z / 4096.0f);
            s_npcSeen[i].dead = (unsigned char)dead;
            continue;
        }
        if (s_npcSeen[i].charaId != gs::chara::kNone)
            SH_LOG_INFO(SPAWN, "slot %d %s removed", i, DbgTrace_CharaName(s_npcSeen[i].charaId));
        if (id != gs::chara::kNone)
            SH_LOG_INFO(SPAWN, "slot %d %s (id %d) spawned at (%.2f, %.2f, %.2f) hp %.1f, map %s room %d", i,
                        DbgTrace_CharaName(id), id, pos.x / 4096.0f, pos.y / 4096.0f,
                        pos.z / 4096.0f, gs::chara_health(i + 1) / 4096.0f, gs::map_name(gs::current_map()),
                        gs::save_room());
        s_npcSeen[i].charaId = (signed char)id;
        s_npcSeen[i].dead    = (unsigned char)dead;
    }
}

static void WatchFlags(void)
{
    const unsigned int* now   = gs::save_event_flags();
    int        words = ARRAY_LEN(s_flagsSeen);
    int        set = 0, cleared = 0, w, b;

    if (!s_flagsValid)
    {
        memcpy(s_flagsSeen, now, sizeof(s_flagsSeen));
        s_flagsValid = 1;
        return;
    }
    if (memcmp(s_flagsSeen, now, sizeof(s_flagsSeen)) == 0)
        return;

    for (w = 0; w < words; w++)
    {
        unsigned int diff = s_flagsSeen[w] ^ now[w];
        for (b = 0; diff != 0 && b < 32; b++)
        {
            if (diff & (1u << b))
            {
                if (now[w] & (1u << b)) set++;
                else                    cleared++;
            }
        }
    }

    /* A savegame load or a new game rewrites the whole block. */
    if (set + cleared > 24)
    {
        SH_LOG_INFO(EVENT, "event flags replaced: %d set, %d cleared (savegame load or new game)", set, cleared);
    }
    else if (SH_LOG_ON(EVENT, SH_LOGLV_INFO))
    {
        for (w = 0; w < words; w++)
        {
            unsigned int diff = s_flagsSeen[w] ^ now[w];
            for (b = 0; diff != 0 && b < 32; b++)
            {
                const char* name;
                int         flag;

                if (!(diff & (1u << b)))
                    continue;
                flag = w * 32 + b;
                name = DbgTrace_EventFlagName(flag);
                SH_LOG_INFO(EVENT, "flag %d %s%s%s", flag, (now[w] & (1u << b)) ? "set" : "cleared",
                            name ? " " : "", name ? name : "");
            }
        }
    }
    memcpy(s_flagsSeen, now, sizeof(s_flagsSeen));
}

void DbgTrace_FramePresented(void)
{
    s_frame++;
    ShLog_SetFrame(s_frame);
    FrameTimeTick();
}

void DbgTrace_Frame(void)
{
    if (SH_LOG_ON(SPAWN, SH_LOGLV_INFO))
    {
        WatchNpcs();
    }
    else
    {
        /* Keep the baseline current so turning the category on later does not
         * report every resident as a fresh spawn. */
        int i;
        for (i = 0; i < gs::kNpcSlots; i++)
        {
            s_npcSeen[i].charaId = (signed char)gs::chara_id(i + 1);
            s_npcSeen[i].dead    = (unsigned char)(gs::chara_health(i + 1) <= 0);
        }
    }

    if (SH_LOG_ON(EVENT, SH_LOGLV_INFO))
        WatchFlags();
    else
        s_flagsValid = 0;

    if (gs::game_state_raw() != s_gameStateSeen)
    {
        if (s_gameStateSeen >= 0)
            SH_LOG_DEBUG(EVENT, "game state %s -> %s", DbgTrace_GameStateName(s_gameStateSeen),
                         DbgTrace_GameStateName(gs::game_state_raw()));
        s_gameStateSeen = gs::game_state_raw();
    }
    if (gs::sys_state_raw() != s_sysStateSeen)
    {
        if (s_sysStateSeen >= 0)
            SH_LOG_DEBUG(EVENT, "system state %s -> %s", DbgTrace_SysStateName(s_sysStateSeen),
                         DbgTrace_SysStateName(gs::sys_state_raw()));
        s_sysStateSeen = gs::sys_state_raw();
    }

    if (gs::current_map() != s_mapSeen || gs::save_room() != s_roomSeen)
    {
        if (s_mapSeen >= 0 && gs::current_map() == s_mapSeen)
            SH_LOG_DEBUG(MAP, "room %d -> %d (%s)", s_roomSeen, gs::save_room(),
                        gs::map_name(gs::current_map()));
        s_mapSeen  = gs::current_map();
        s_roomSeen = gs::save_room();
    }
}

