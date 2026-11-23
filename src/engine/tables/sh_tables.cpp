#include "engine/tables/sh_tables.h"

#include "engine/tables/table_store.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>

namespace {

std::unique_ptr<sh::tables::TableStore>& store() {
    static std::unique_ptr<sh::tables::TableStore> instance;
    return instance;
}

void log_stderr(int level, const char* message) {
    std::fprintf(stderr, "%s%s\n", level == SH_TABLES_LOG_WARN ? "warning: " : "", message);
}

}  // namespace

extern "C" void ShTables_Configure(const ShTablesConfig* c) {
    sh::tables::StoreConfig config;
    if (c->gamedataDir) {
        config.gamedata_dir = c->gamedataDir;
    }
    if (c->modsDir && *c->modsDir) {
        config.mods_dir = c->modsDir;
    }
    if (c->funcs) {
        config.funcs.assign(c->funcs, c->funcs + c->funcCount);
    }
    void (*log)(int, const char*) = c->log ? c->log : log_stderr;
    config.log = [log](int level, const std::string& message) { log(level, message.c_str()); };

    if (c->configPath && *c->configPath) {
        std::ifstream in(c->configPath, std::ios::binary);
        if (in) {
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::vector<std::string> warnings;
            sh::tables::apply_config_text(config, text, warnings);
            for (const std::string& w : warnings) {
                log(SH_TABLES_LOG_WARN, ("[TABLES] " + std::string(c->configPath) + ": " + w).c_str());
            }
        }
    }
    store() = std::make_unique<sh::tables::TableStore>(std::move(config));
}

extern "C" int ShTables_LoadAnim(const ShAnimTableInfo* info, ShAnimEntry* out) {
    return ShTables_LoadAnimForOverlay(info, nullptr, out);
}

extern "C" int ShTables_LoadAnimForOverlay(const ShAnimTableInfo* info, const char* overlay, ShAnimEntry* out) {
    if (!store()) {
        const ShTablesConfig defaults{};
        ShTables_Configure(&defaults);
    }
    const sh::tables::TableStore::AnimLoad loaded = store()->load_anim(*info, overlay ? overlay : "");
    for (std::size_t i = 0; i < info->count; ++i) {
        out[i] = loaded.entries[i];
    }
    return loaded.from_file ? 1 : 0;
}

extern "C" int ShTables_GameplayChanged(void) {
    return store() && store()->gameplay_changed() ? 1 : 0;
}
