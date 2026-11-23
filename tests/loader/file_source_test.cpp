// Where one queued read takes its bytes (engine/loader/queue/file_source): cutscene, mod file, disc,
// and the pictures remembered for the image step.

#include <map>
#include <string>
#include <vector>

#include "engine/loader/queue/file_source.h"

#include "framework/sh_test.h"

namespace ld = sh::loader;

namespace {

class Host final : public ld::FileSourceHost {
public:
    bool cutscene = false, mods = true;
    std::map<std::string, std::int64_t> files;  // mod files and their sizes
    std::map<std::string, bool> pictures;
    std::size_t capacity = 0;
    std::vector<std::string> log;
    std::int64_t open_size = 0;

    bool serve_cutscene(const ld::SourceRequest&) override { return cutscene; }
    bool mods_active() const override { return mods; }
    bool mod_has_picture(const std::string& p) const override { return pictures.count(p) != 0; }
    bool mod_open(const std::string& p, std::int64_t* size) override {
        auto it = files.find(p);
        if (it == files.end()) return false;
        *size = open_size = it->second;
        return true;
    }
    std::size_t mod_read(void*, std::size_t max) override {
        log.push_back("mod_read");
        return static_cast<std::size_t>(open_size) < max ? static_cast<std::size_t>(open_size) : max;
    }
    void mod_close() override { log.push_back("mod_close"); }
    std::size_t dest_capacity(const void*) const override { return capacity; }
    void mod_bytes_read(const ld::SourceRequest&, std::int64_t) override { log.push_back("mod_bytes"); }
    void read_sector(std::uint32_t s, void*) override { log.push_back("sector " + std::to_string(s)); }
    void trace(const ld::SourceRequest&, const char* from) override { log.push_back(std::string("trace ") + (from ? from : "disc")); }
    void note(const std::string&) override {}
};

ld::SourceRequest request(const char* path, std::uint32_t bytes, bool image) {
    ld::SourceRequest r;
    r.file = 7;
    r.path = path;
    r.table_bytes = bytes;
    r.first_sector = 100;
    r.image = image;
    r.dest = &r;  // never written by the test host
    return r;
}

}  // namespace

SH_TEST(file_source_disc_sectors) {
    Host h;
    ld::FileSource s;
    SH_CHECK(s.read(h, request("CHARA/HERO.TIM", 4097, true)) == ld::Source::Disc);
    SH_CHECK(h.log == (std::vector<std::string>{"trace disc", "sector 100", "sector 101", "sector 102"}));
}

SH_TEST(file_source_cutscene_first) {
    Host h;
    h.cutscene = true;
    ld::FileSource s;
    SH_CHECK(s.read(h, request("VIN/X.BIN", 2048, false)) == ld::Source::Cutscene);
    SH_CHECK(h.log == (std::vector<std::string>{"trace convert/cutscenes"}));
}

SH_TEST(file_source_mod_file_replaces_bytes) {
    Host h;
    h.files["ITEM/A.TMD"] = 1000;
    ld::FileSource s;
    SH_CHECK(s.read(h, request("ITEM/A.TMD", 2048, false)) == ld::Source::Mod);
    SH_CHECK(h.log == (std::vector<std::string>{"mod_read", "mod_bytes", "trace ITEM/A.TMD"}));
    h.mods = false;
    h.log.clear();
    SH_CHECK(s.read(h, request("ITEM/A.TMD", 2048, false)) == ld::Source::Disc);
}

SH_TEST(file_source_oversized_and_pictures_wait_for_the_image_step) {
    Host h;
    h.files["TIM/BIG.TIM"] = 9000;
    ld::FileSource s;
    SH_CHECK(s.read(h, request("TIM/BIG.TIM", 2048, true)) == ld::Source::Disc);  // too big: disc, picture later
    SH_CHECK(h.log.front() == "mod_close");
    SH_CHECK(s.pending().pop(7) == std::optional<std::string>("TIM/BIG.TIM"));
    SH_CHECK(!s.pending().pop(7));

    h.capacity = 10000;  // a registered big destination takes it
    h.log.clear();
    SH_CHECK(s.read(h, request("TIM/BIG.TIM", 2048, true)) == ld::Source::Mod);

    h.pictures["TIM/BIG.TIM"] = true;  // a picture wins over the same-path TIM
    h.log.clear();
    SH_CHECK(s.read(h, request("TIM/BIG.TIM", 2048, true)) == ld::Source::Disc);
    SH_CHECK(h.log.front() == "trace disc");
    SH_CHECK(s.pending().pop(7) == std::optional<std::string>("TIM/BIG.TIM"));
}

SH_TEST(file_source_pending_table_is_bounded) {
    ld::PendingPictures p;
    for (int i = 0; i < 32; i++) SH_CHECK(p.stash(i, "x"));
    SH_CHECK(!p.stash(40, "y"));
    SH_CHECK(p.stash(3, "z"));  // the same file overwrites its slot
    SH_CHECK(p.pop(3) == std::optional<std::string>("z"));
    SH_CHECK(!p.stash(-1, "w"));
}
