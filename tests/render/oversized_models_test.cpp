// Model files of mods larger than the disc file they replace (render/geometry/oversized_models.h): which files get a
// buffer of their own, what stands in for what, and what a file that changes on disk does.

#include "framework/sh_test.h"
#include "render/geometry/oversized_models.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace {

using sh::models::OversizedModels;

// A model file the check accepts: a header without parts or materials, padded to `bytes`.
std::vector<std::uint8_t> model_file(std::size_t bytes) {
    std::vector<std::uint8_t> f(bytes, 0);
    f[0] = 0x30;
    f[1] = 6;
    return f;
}

class FakeHost final : public sh::models::OversizedModelHost {
public:
    bool mods_active() override { return active; }
    std::optional<std::string> mod_path(int file) override {
        if (file < 0 || file >= 100) return std::nullopt;
        return "CHARA/F" + std::to_string(file) + ".ILM";
    }
    std::int64_t disc_bytes(int) override { return 2048; }
    std::int64_t mod_file_bytes(const std::string& path) override {
        const auto it = files.find(path);
        return it == files.end() ? -1 : static_cast<std::int64_t>(it->second.size());
    }
    std::optional<std::vector<std::uint8_t>> mod_file(const std::string& path, std::int64_t bytes) override {
        ++reads;
        const auto it = files.find(path);
        if (it == files.end() || unreadable || static_cast<std::int64_t>(it->second.size()) != bytes)
            return std::nullopt;
        return it->second;
    }
    void note(const std::string& line) override { notes.push_back(line); }

    bool active = true;
    bool unreadable = false;
    int reads = 0;
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::vector<std::string> notes;
};

}  // namespace

SH_TEST(oversized_models_only_larger_sound_files_get_a_buffer) {
    FakeHost host;
    OversizedModels models(host);
    std::uint8_t game[2048] = {};

    // No file of a mod, a file that fits, a file number outside the table, no buffer: the game's buffer.
    SH_CHECK(models.redirect(1, game) == game);
    host.files["CHARA/F2.ILM"] = model_file(2048);
    SH_CHECK(models.redirect(2, game) == game);
    SH_CHECK(models.redirect(500, game) == game);
    SH_CHECK(models.redirect(3, nullptr) == nullptr);
    SH_CHECK(models.accepted_bytes(1) == -1 && models.accepted_bytes(2) == -1);
    SH_CHECK(host.reads == 0);  // nothing was read for them

    // A larger file that passes the check.
    host.files["CHARA/F3.ILM"] = model_file(3000);
    void* own = models.redirect(3, game);
    SH_CHECK(own != game && own != nullptr);
    SH_CHECK(models.capacity(own) == 3000 + OversizedModels::kTail);
    SH_CHECK(models.owns(own) && models.owns(static_cast<std::uint8_t*>(own) + 3015));
    SH_CHECK(!models.owns(static_cast<std::uint8_t*>(own) + 3016) && !models.owns(game) && !models.owns(nullptr));
    SH_CHECK(models.game_buffer_of(own) == game && models.game_buffer_of(game) == nullptr);
    SH_CHECK(models.accepted_bytes(3) == 3000);
    // The buffer starts zeroed, and the same file gets the same buffer without being read again.
    SH_CHECK(static_cast<std::uint8_t*>(own)[0] == 0 && static_cast<std::uint8_t*>(own)[3015] == 0);
    const int reads = host.reads;
    SH_CHECK(models.redirect(3, game) == own && host.reads == reads);

    // A larger file that fails the check, and one that cannot be read: the game's buffer, and a line in the log.
    host.files["CHARA/F4.ILM"] = model_file(3000);
    host.files["CHARA/F4.ILM"][1] = 5;
    const std::size_t notes = host.notes.size();
    SH_CHECK(models.redirect(4, game) == game);
    SH_CHECK(models.accepted_bytes(4) == -1);
    SH_CHECK(host.notes.size() == notes + 1 && host.notes.back().find("V1") != std::string::npos);
    host.files["CHARA/F5.ILM"] = model_file(3000);
    host.unreadable = true;
    SH_CHECK(models.redirect(5, game) == game);
    host.unreadable = false;

    // Without active mods nothing is looked at.
    host.active = false;
    SH_CHECK(models.redirect(3, game) == game && models.accepted_bytes(3) == -1);
}

SH_TEST(oversized_models_a_handed_over_buffer_resolves_to_the_games) {
    FakeHost host;
    OversizedModels models(host);
    std::uint8_t game[2048] = {};
    host.files["CHARA/F3.ILM"] = model_file(3000);
    void* own = models.redirect(3, game);
    SH_CHECK(own != game);

    // A slot reused for a character without a mod file hands in the previous character's buffer.
    SH_CHECK(models.redirect(7, own) == game);
    // For another oversized file the game's buffer is what the new buffer stands for.
    host.files["CHARA/F8.ILM"] = model_file(4000);
    void* other = models.redirect(8, own);
    SH_CHECK(other != own && other != game && models.game_buffer_of(other) == game);
    // Without mods the resolving still happens.
    host.active = false;
    SH_CHECK(models.redirect(3, own) == game);
}

SH_TEST(oversized_models_a_file_that_changes_size) {
    FakeHost host;
    OversizedModels models(host);
    std::uint8_t game[2048] = {};
    host.files["CHARA/F3.ILM"] = model_file(3000);
    auto* first = static_cast<std::uint8_t*>(models.redirect(3, game));
    first[100] = 0xAA;  // the read wrote the file here

    // Smaller but still oversized: the same buffer, cleared, with the capacity it has.
    host.files["CHARA/F3.ILM"] = model_file(2500);
    SH_CHECK(models.redirect(3, game) == first);
    SH_CHECK(first[100] == 0 && models.accepted_bytes(3) == 2500);
    SH_CHECK(models.capacity(first) == 3000 + OversizedModels::kTail);

    // Larger: a new buffer; the old address still resolves, because a loaded model may live there.
    host.files["CHARA/F3.ILM"] = model_file(5000);
    auto* second = static_cast<std::uint8_t*>(models.redirect(3, game));
    SH_CHECK(second != first && models.capacity(second) == 5000 + OversizedModels::kTail);
    SH_CHECK(models.owns(first) && models.game_buffer_of(first) == game);
    SH_CHECK(models.capacity(first) == 0);  // nothing is read into it any more

    // Broken now: refused, the game's buffer; the buffer it had still resolves.
    host.files["CHARA/F3.ILM"] = model_file(6000);
    host.files["CHARA/F3.ILM"][0] = 0;
    SH_CHECK(models.redirect(3, game) == game);
    SH_CHECK(models.capacity(second) == 0 && models.owns(second));
    // Fits the disc file again: the ordinary read.
    host.files["CHARA/F3.ILM"] = model_file(1000);
    SH_CHECK(models.redirect(3, game) == game && models.accepted_bytes(3) == -1);
}

SH_TEST(oversized_models_buffers_of_the_game) {
    FakeHost host;
    OversizedModels models(host);
    std::uint8_t pool[64] = {};
    SH_CHECK(models.capacity(pool) == 0);
    models.set_game_buffer(12, pool, 48);
    SH_CHECK(models.capacity(pool) == 48 && models.capacity(pool + 1) == 0);
    SH_CHECK(!models.owns(pool));
    models.set_game_buffer(12, pool, 64);
    SH_CHECK(models.capacity(pool) == 64);
    models.set_game_buffer(12, nullptr, 0);
    SH_CHECK(models.capacity(pool) == 0);
}
