/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sh_log.h - categorized log for Silent Hill PC (C facade).
 *
 * The journal itself is C++20 (engine/log/log.cpp, sh::log::Logger); this
 * header is what C code, map overlays and C++ callers include. A message has
 * a category (texture, spawn, fmv, ...) and a level (error .. trace) and goes
 * to the terminal, the log file under <exe>/logs/ and the in-game console.
 *
 * Cost: a disabled message is one byte compare, the arguments are never
 * evaluated and nothing is formatted.
 *
 *   SH_LOG_INFO(SPAWN, "chara %d spawned", id);
 *   SH_LOG_TRACE(TEXTURE, "uploaded %s", name);
 *
 * The legacy macros (SH_LOG, SH_WARN, SH_ERR, SH_DBG, SH_DBG_ECHO) stay: their
 * category comes from the message's leading "[TAG]" ("[POOL] ..." -> spawn),
 * resolved once per call site.
 */
#pragma once

#include <stdarg.h>
#include <stdio.h>


#ifdef __cplusplus
extern "C" {
#endif

typedef enum ShLogCategory
{
    SH_LOGCAT_GAME = 0, /* anything without a narrower category */
    SH_LOGCAT_FS,       /* file reads, fs queue, VFS, loose/mod files */
    SH_LOGCAT_CD,       /* CD sector reads */
    SH_LOGCAT_TEXTURE,  /* TIM uploads, replacement pictures of mods */
    SH_LOGCAT_MODEL,    /* character models, LM/IPD/TMD */
    SH_LOGCAT_ANIM,     /* animation files and playback */
    SH_LOGCAT_SPAWN,    /* characters spawned and removed */
    SH_LOGCAT_EVENT,    /* story/event flags, game and system states */
    SH_LOGCAT_MAP,      /* map changes, overlays, rooms */
    SH_LOGCAT_FMV,      /* movies */
    SH_LOGCAT_AUDIO,    /* XA voice, BGM, SFX */
    SH_LOGCAT_INPUT,    /* pads, binds, mouse */
    SH_LOGCAT_RENDER,   /* GL, effects, frame timing */
    SH_LOGCAT_SAVE,     /* memory card, quick save */
    SH_LOGCAT_IMPORT,   /* disc image import */
    SH_LOGCAT_CONFIG,   /* config.cfg */
    SH_LOGCAT_CONSOLE,  /* console commands, cheats */
    SH_LOGCAT_COUNT
} ShLogCategory;

typedef enum ShLogLevel
{
    SH_LOGLV_OFF   = 0,
    SH_LOGLV_ERROR = 1,
    SH_LOGLV_WARN  = 2,
    SH_LOGLV_INFO  = 3,
    SH_LOGLV_DEBUG = 4,
    SH_LOGLV_TRACE = 5
} ShLogLevel;

/* Highest level that reaches any sink, per category; and the highest over all
 * categories. Written only by the journal when its settings change. */
extern unsigned char g_ShLogGate[SH_LOGCAT_COUNT];
extern unsigned char g_ShLogGateAny;

/* The log file while the file sink is open, else NULL: for flushing it at a
 * point of no return, and for telling whether there is one. */
extern FILE* g_ShDebugLog;

#if defined(__MINGW32__)
/* mingw: plain "printf" checks against msvcrt (no %zu, %lld); the log goes
 * through the C++ vsnprintf, which is mingw's C99 one (__USE_MINGW_ANSI_STDIO). */
#define SH_LOG_PRINTF_ATTR(fmtIdx, argIdx) __attribute__((format(gnu_printf, fmtIdx, argIdx)))
#elif defined(__GNUC__) || defined(__clang__)
#define SH_LOG_PRINTF_ATTR(fmtIdx, argIdx) __attribute__((format(printf, fmtIdx, argIdx)))
#else
#define SH_LOG_PRINTF_ATTR(fmtIdx, argIdx)
#endif

/* --- writing --- */
void ShLog_Write(int category, int level, const char* fmt, ...) SH_LOG_PRINTF_ATTR(3, 4);
void ShLog_VWrite(int category, int level, const char* fmt, va_list ap);
/* User feedback (setting toggles, debug keys): always shown in the console and
 * as a toast, plus terminal/file when the category's info level is on. */
void ShLog_Echo(const char* fmt, ...) SH_LOG_PRINTF_ATTR(1, 2);
/* Category of a message from its leading "[TAG]"; SH_LOGCAT_GAME if none. */
int ShLog_CategoryFromText(const char* text);

/* --- setup (engine/app/game_main.cpp) --- */
void ShLog_Init(void);                                  /* first thing in main() */
int  ShLog_ConfigKey(const char* key, const char* value); /* 1 = a log_* key it owns */
void ShLog_SetLegacyDebugLog(int enabled);              /* enable_debug_log = 1 */
int  ShLog_FileWanted(void);
int  ShLog_StartFile(const char* exeDir);               /* <exeDir>/logs/silenthill-<time>.log */
const char* ShLog_FilePath(void);                       /* "" when no file */
void ShLog_SetTerminalStream(FILE* stream);             /* NULL = no terminal output */
void ShLog_SetFrame(unsigned frame);
void ShLog_Flush(void);
void ShLog_Shutdown(void);
void Sh_LogPeriodicFlush(void);                         /* at most one fflush per second */

/* --- runtime control (console `log`) --- */
int         ShLog_ParseLevel(const char* text);          /* -1 if unknown */
int         ShLog_ParseCategory(const char* text);       /* name or alias; -1 if unknown */
const char* ShLog_LevelName(int level);
const char* ShLog_CategoryName(int category);
const char* ShLog_CategoryHelp(int category);
int         ShLog_CategoryLevel(int category);
/* "texture=trace,spawn=debug,*=info"; returns the number of entries rejected. */
int  ShLog_ApplySpec(const char* spec);
int  ShLog_SetLevel(int category, int level);            /* category -1 = all */
int  ShLog_ConsoleLevel(void);
void ShLog_SetConsoleLevel(int level);

/* In-game console sink: lines queued by any thread, drained by the overlay on
 * the main thread. toast = 1 for ShLog_Echo lines. */
typedef void (*ShLogConsoleFn)(const char* text, int level, int toast);
void ShLog_ConsoleDrain(ShLogConsoleFn fn);

/* Test hook: every delivered line (no colours) with its category and level. */
typedef void (*ShLogListenerFn)(int category, int level, const char* line);
void ShLog_SetListener(ShLogListenerFn fn);

#ifdef __cplusplus
}
#endif

/* --- categorized macros: category is a bare name (TEXTURE, SPAWN, ...) --- */
#define SH_LOG_ON(cat, lvl) ((lvl) <= g_ShLogGate[SH_LOGCAT_##cat])
#define SH_LOGF(cat, lvl, ...) do { \
    if (SH_LOG_ON(cat, lvl)) ShLog_Write(SH_LOGCAT_##cat, (lvl), __VA_ARGS__); \
} while (0)
#define SH_LOG_ERROR(cat, ...) SH_LOGF(cat, SH_LOGLV_ERROR, __VA_ARGS__)
#define SH_LOG_WARN(cat, ...)  SH_LOGF(cat, SH_LOGLV_WARN, __VA_ARGS__)
#define SH_LOG_INFO(cat, ...)  SH_LOGF(cat, SH_LOGLV_INFO, __VA_ARGS__)
#define SH_LOG_DEBUG(cat, ...) SH_LOGF(cat, SH_LOGLV_DEBUG, __VA_ARGS__)
#define SH_LOG_TRACE(cat, ...) SH_LOGF(cat, SH_LOGLV_TRACE, __VA_ARGS__)

/* --- legacy macros: category from the "[TAG]" prefix, cached per call site.
 * The first gate (any category) keeps a disabled call at one compare; the
 * call site's category is looked up only once something at that level is on. */
/* The format string is the first argument; the macros pass all of them on as they are, so a call without
 * arguments after the format needs no comma pasting (a GNU extension clang warns about under -Wpedantic). */
#define SH_LOG_FIRST_(...) SH_LOG_FIRST_I_(__VA_ARGS__, 0)
#define SH_LOG_FIRST_I_(first, ...) first

#define SH_LOG_TAGGED_(lvl, ...) do { \
    static signed char sh_log_cat_ = -1; \
    if ((lvl) <= g_ShLogGateAny) { \
        if (sh_log_cat_ < 0) sh_log_cat_ = (signed char)ShLog_CategoryFromText(SH_LOG_FIRST_(__VA_ARGS__)); \
        if ((lvl) <= g_ShLogGate[sh_log_cat_]) ShLog_Write(sh_log_cat_, (lvl), __VA_ARGS__); \
    } \
} while (0)

#define SH_LOG(...)  SH_LOG_TAGGED_(SH_LOGLV_INFO, __VA_ARGS__)
#define SH_WARN(...) SH_LOG_TAGGED_(SH_LOGLV_WARN, __VA_ARGS__)
#define SH_ERR(...)  SH_LOG_TAGGED_(SH_LOGLV_ERROR, __VA_ARGS__)
#define SH_DBG(...)  SH_LOG_TAGGED_(SH_LOGLV_DEBUG, __VA_ARGS__)
#define SH_DBG_ECHO(...) ShLog_Echo(__VA_ARGS__)

