#include "framework/sh_test.h"

#include "engine/vfs/vfs.h"
#include "formats/disc/disc_image.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using sh::vfs::Layer;
using sh::vfs::Vfs;
using sh::vfs::normalize;

namespace {

using Bytes = std::optional<std::vector<std::uint8_t>>;
using Files = std::map<std::string, std::string>;

std::string text(const Bytes& data) {
    if (!data) {
        return "<missing>";
    }
    return std::string(data->begin(), data->end());
}

class MemoryLayer final : public Layer {
public:
    MemoryLayer(std::string name, const Files& files) : name_(std::move(name)) {
        for (const auto& [path, content] : files) {
            files_[normalize(path)] = content;
        }
    }

    std::string name() const override {
        return name_;
    }

    bool exists(std::string_view path) const override {
        return files_.count(normalize(path)) != 0;
    }

    Bytes read(std::string_view path) const override {
        const auto it = files_.find(normalize(path));
        if (it == files_.end()) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>(it->second.begin(), it->second.end());
    }

private:
    std::string name_;
    std::map<std::string, std::string> files_;
};

// Reports BROKEN.DAT as present and then fails to read it.
class UnreadableLayer final : public Layer {
public:
    std::string name() const override {
        return "unreadable";
    }

    bool exists(std::string_view path) const override {
        return normalize(path) == "BROKEN.DAT";
    }

    Bytes read(std::string_view) const override {
        return std::nullopt;
    }
};

fs::path scratch_dir(const std::string& name) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "vfs" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_text(const fs::path& file, const std::string& content) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << content;
}

}  // namespace

SH_TEST(vfs_normalize_canonical_form) {
    SH_CHECK(normalize("CHARA/HERO.ILM") == "CHARA/HERO.ILM");
    SH_CHECK(normalize("chara/hero.ilm") == "CHARA/HERO.ILM");
    SH_CHECK(normalize("CHARA\\HERO.ILM") == "CHARA/HERO.ILM");
    SH_CHECK(normalize("/CHARA//HERO.ILM") == "CHARA/HERO.ILM");
    SH_CHECK(normalize("./CHARA/./HERO.ILM") == "CHARA/HERO.ILM");
    SH_CHECK(normalize("CHARA/") == "CHARA");
    SH_CHECK(normalize("..\\1ST\\BODYPROG.BIN") == "../1ST/BODYPROG.BIN");
    SH_CHECK(normalize("") == "");
    SH_CHECK(normalize("/") == "");
    SH_CHECK(normalize("./") == "");
}

SH_TEST(vfs_normalize_is_idempotent) {
    const std::string_view samples[] = {"chara\\hero.ilm", "/A//b/./C", "", "..\\x"};
    for (const std::string_view sample : samples) {
        const std::string once = normalize(sample);
        SH_CHECK(normalize(once) == once);
    }
}

SH_TEST(vfs_higher_priority_layer_wins) {
    const Files low{{"CHARA/HERO.ILM", "low"}, {"ONLY_LOW.DAT", "low"}};
    const Files high{{"chara\\hero.ilm", "high"}};

    Vfs vfs;
    vfs.add_layer(std::make_unique<MemoryLayer>("low", low), 0);
    vfs.add_layer(std::make_unique<MemoryLayer>("high", high), 10);

    SH_CHECK(text(vfs.read("CHARA/HERO.ILM")) == "high");
    SH_CHECK(text(vfs.read("only_low.dat")) == "low");
    SH_CHECK(vfs.layer_names() == (std::vector<std::string>{"high", "low"}));
}

SH_TEST(vfs_equal_priority_keeps_insertion_order) {
    Vfs vfs;
    vfs.add_layer(std::make_unique<MemoryLayer>("zero", Files{}), 4);
    vfs.add_layer(std::make_unique<MemoryLayer>("first", Files{{"A.DAT", "first"}}), 5);
    vfs.add_layer(std::make_unique<MemoryLayer>("second", Files{{"A.DAT", "second"}, {"B.DAT", "second"}}), 5);
    vfs.add_layer(std::make_unique<MemoryLayer>("higher", Files{}), 6);
    vfs.add_layer(std::make_unique<MemoryLayer>("third", Files{}), 5);

    SH_CHECK(vfs.layer_names() == (std::vector<std::string>{"higher", "first", "second", "third", "zero"}));
    SH_CHECK(text(vfs.read("A.DAT")) == "first");
    SH_CHECK(text(vfs.read("B.DAT")) == "second");
}

SH_TEST(vfs_missing_file_is_not_found) {
    Vfs vfs;
    vfs.add_layer(std::make_unique<MemoryLayer>("one", Files{{"A.DAT", "a"}}), 0);

    SH_CHECK(!vfs.exists("B.DAT"));
    SH_CHECK(!vfs.read("B.DAT").has_value());
}

SH_TEST(vfs_unreadable_file_does_not_fall_back) {
    Vfs vfs;
    vfs.add_layer(std::make_unique<MemoryLayer>("stock", Files{{"BROKEN.DAT", "stock"}}), 0);
    vfs.add_layer(std::make_unique<UnreadableLayer>(), 10);

    SH_CHECK(vfs.exists("BROKEN.DAT"));
    SH_CHECK(!vfs.read("BROKEN.DAT").has_value());
}

SH_TEST(vfs_null_layer_is_ignored) {
    Vfs vfs;
    vfs.add_layer(nullptr, 1);

    SH_CHECK(vfs.layer_names().empty());
    SH_CHECK(!vfs.exists("ANYTHING.DAT"));
}

SH_TEST(vfs_directory_layer_is_case_insensitive) {
    const fs::path root = scratch_dir("case");
    write_text(root / "Chara" / "Hero.ilm", "hero-data");
    write_text(root / "1st" / "bodyprog.bin", "bodyprog-data");

    Vfs vfs;
    vfs.add_layer(sh::vfs::make_directory_layer(root, "case"), 0);

    SH_CHECK(text(vfs.read("CHARA/HERO.ILM")) == "hero-data");
    SH_CHECK(text(vfs.read("chara/hero.ilm")) == "hero-data");
    SH_CHECK(text(vfs.read("CHARA\\hero.ILM")) == "hero-data");
    SH_CHECK(text(vfs.read("1ST/BODYPROG.BIN")) == "bodyprog-data");
    SH_CHECK(!vfs.exists("CHARA"));
    SH_CHECK(!vfs.exists("CHARA/NOPE.ILM"));

    fs::remove_all(root);
}

// The app adds mods in discover() order (by id), so with equal priority the lower id wins.
SH_TEST(vfs_equal_priority_mods_follow_add_order) {
    const fs::path alpha = scratch_dir("order-alpha");
    const fs::path beta = scratch_dir("order-beta");
    write_text(alpha / "CHARA" / "HERO.ILM", "alpha");
    write_text(beta / "CHARA" / "HERO.ILM", "beta");
    write_text(beta / "CHARA" / "ONLY_BETA.ILM", "beta");

    Vfs in_order;
    in_order.add_layer(sh::vfs::make_directory_layer(alpha, "alpha"), 5);
    in_order.add_layer(sh::vfs::make_directory_layer(beta, "beta"), 5);
    SH_CHECK(text(in_order.read("CHARA/HERO.ILM")) == "alpha");
    SH_CHECK(text(in_order.read("CHARA/ONLY_BETA.ILM")) == "beta");

    Vfs reversed;
    reversed.add_layer(sh::vfs::make_directory_layer(beta, "beta"), 5);
    reversed.add_layer(sh::vfs::make_directory_layer(alpha, "alpha"), 5);
    SH_CHECK(text(reversed.read("CHARA/HERO.ILM")) == "beta");

    fs::remove_all(alpha);
    fs::remove_all(beta);
}

SH_TEST(vfs_directory_layers_stack_by_priority) {
    const fs::path mod = scratch_dir("stack-mod");
    const fs::path unpacked = scratch_dir("stack-unpacked");
    write_text(mod / "CHARA" / "HERO.ILM", "mod");
    write_text(unpacked / "CHARA" / "HERO.ILM", "unpacked");
    write_text(unpacked / "1ST" / "X.BIN", "unpacked");

    Vfs vfs;
    vfs.add_layer(sh::vfs::make_directory_layer(unpacked, "unpacked"), 0);
    vfs.add_layer(sh::vfs::make_directory_layer(mod, "mod"), 10);

    SH_CHECK(text(vfs.read("CHARA/HERO.ILM")) == "mod");
    SH_CHECK(text(vfs.read("1ST/X.BIN")) == "unpacked");

    fs::remove_all(mod);
    fs::remove_all(unpacked);
}

SH_TEST(vfs_directory_layer_survives_symlink_loop) {
    const fs::path root = scratch_dir("loop");
    const fs::path outside = scratch_dir("loop-outside");
    write_text(root / "sub" / "inner.dat", "inner");
    write_text(outside / "x.dat", "outside");

    std::error_code ec;
    fs::create_directory_symlink(root, root / "sub" / "back", ec);
    if (ec) {
        SH_SKIP("symlinks are not available here");
    }
    fs::create_directory_symlink(outside, root / "linked", ec);

    Vfs vfs;
    vfs.add_layer(sh::vfs::make_directory_layer(root, "loop"), 0);

    SH_CHECK(text(vfs.read("SUB/INNER.DAT")) == "inner");
    SH_CHECK(text(vfs.read("LINKED/X.DAT")) == "outside");

    fs::remove_all(root);
    fs::remove_all(outside);
}

SH_TEST(vfs_directory_layer_missing_root_is_empty) {
    const fs::path root = fs::path(SH_PROJECT_ROOT) / ".tmp" / "vfs" / "does-not-exist";
    fs::remove_all(root);

    const std::unique_ptr<Layer> layer = sh::vfs::make_directory_layer(root, "missing");
    SH_CHECK(layer->name() == "missing");
    SH_CHECK(!layer->exists("ANY.DAT"));
    SH_CHECK(!layer->read("ANY.DAT").has_value());
}

SH_TEST(vfs_disc_layer_without_image_is_empty) {
    const std::unique_ptr<Layer> layer = sh::vfs::make_disc_layer(nullptr);
    SH_CHECK(!layer->exists("SLUS_007.07"));
    SH_CHECK(!layer->read("SYSTEM.CNF").has_value());
}

SH_TEST(vfs_disc_image_root_files_and_archive_entries) {
    const fs::path image_path = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
    if (!fs::exists(image_path)) {
        SH_SKIP("disc image is not in data/");
    }

    const std::shared_ptr<const sh::disc::Image> image = sh::disc::Image::open(image_path);
    SH_CHECK(image != nullptr);
    if (!image) {
        return;
    }

    Vfs vfs;
    vfs.add_layer(sh::vfs::make_disc_layer(image), 0);

    // Root files of the disc; SLUS_007.07 is 81 920 bytes (docs/FORMATS.md).
    const Bytes executable = vfs.read("SLUS_007.07");
    SH_CHECK(executable.has_value());
    SH_CHECK(executable.has_value() && executable->size() == 81920);
    SH_CHECK(vfs.exists("SYSTEM.CNF"));

    SH_CHECK(vfs.exists("chara\\ar.ilm"));
    SH_CHECK(vfs.exists("CHARA/AR.TIM"));
    SH_CHECK(!vfs.read("CHARA/AR.ILM").value_or(std::vector<std::uint8_t>{}).empty());

    // XA tracks live in HILL., which is read as 2336-byte sectors.
    const Bytes xa_track = vfs.read("XA/ZZ_14239");
    SH_CHECK(xa_track.has_value() && !xa_track->empty() && xa_track->size() % 2336 == 0);

    Vfs with_mod;
    with_mod.add_layer(std::make_unique<MemoryLayer>("mod", Files{{"SYSTEM.CNF", "override"}}), 10);
    with_mod.add_layer(sh::vfs::make_disc_layer(image), 0);
    SH_CHECK(text(with_mod.read("SYSTEM.CNF")) == "override");
}
