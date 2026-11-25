#include "engine/mods/mod_import.h"

#include "engine/mods/manifest.h"
#include "engine/mods/mods_admin.h"
#include "engine/mods/registry.h"

#include "miniz/include/miniz.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace sh::mods {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kManifestMaxBytes = 1u << 20;

std::string utf8(const fs::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

fs::path from_utf8(std::string_view text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

bool ends_with_zip(const fs::path& path) {
    std::string ext = utf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".zip";
}

// Parts of a path the import leaves out: hidden files and folders, and the resource forks macOS adds to archives.
bool skipped_name(std::string_view name) { return name.empty() || name.front() == '.' || name == "__MACOSX"; }

std::vector<std::string> split(std::string_view path) {
    std::vector<std::string> out;
    std::string part;
    for (const char c : path) {
        if (c == '/') {
            if (!part.empty()) out.push_back(std::move(part));
            part.clear();
        } else {
            part += c;
        }
    }
    if (!part.empty()) out.push_back(std::move(part));
    return out;
}

// A member name of an archive as a relative path ('\' read as '/'), or nothing when it could leave the folder it is
// unpacked into: absolute, a drive or stream (':'), "..", control characters.
std::optional<std::string> safe_member(std::string_view raw) {
    std::string name(raw);
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.empty() || name.front() == '/') return std::nullopt;
    for (const char c : name) {
        if (static_cast<unsigned char>(c) < 0x20 || c == ':') return std::nullopt;
    }
    std::string out;
    for (const std::string& part : split(name)) {
        if (part == "..") return std::nullopt;
        if (part == ".") continue;
        out += (out.empty() ? "" : "/") + part;
    }
    return out;
}

bool hidden_path(const std::vector<std::string>& parts) {
    return std::any_of(parts.begin(), parts.end(), [](const std::string& p) { return skipped_name(p); });
}

std::optional<std::string> read_text(const fs::path& file) {
    std::error_code ec;
    if (fs::file_size(file, ec) > kManifestMaxBytes || ec) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// ---- archives: miniz over a std::ifstream, so a UTF-8 path opens on every platform

struct Zip {
    mz_zip_archive archive{};
    std::ifstream file;
    bool open = false;

    static size_t read(void* opaque, mz_uint64 offset, void* buffer, size_t n) {
        auto* self = static_cast<Zip*>(opaque);
        self->file.clear();
        self->file.seekg(static_cast<std::streamoff>(offset));
        self->file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(n));
        return static_cast<size_t>(self->file.gcount());
    }

    bool init(const fs::path& path) {
        file.open(path, std::ios::binary);
        if (!file) return false;
        file.seekg(0, std::ios::end);
        const auto size = static_cast<mz_uint64>(file.tellg());
        archive.m_pRead = &Zip::read;
        archive.m_pIO_opaque = this;
        open = mz_zip_reader_init(&archive, size, 0) != 0;
        return open;
    }
    ~Zip() {
        if (open) mz_zip_reader_end(&archive);
    }
};

struct Member {
    mz_uint index = 0;
    std::string name;  // safe_member's form
    bool directory = false;
    bool link = false;
    bool readable = true;
    bool unsafe = false;
    std::string raw;
    std::uint64_t size = 0;
};

std::vector<Member> list_members(Zip& zip) {
    std::vector<Member> out;
    const mz_uint count = mz_zip_reader_get_num_files(&zip.archive);
    out.reserve(count);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip.archive, i, &st)) continue;
        Member m;
        m.index = i;
        m.raw = st.m_filename;
        m.directory = st.m_is_directory != 0;
        m.readable = st.m_is_supported != 0 && st.m_is_encrypted == 0;
        m.size = st.m_uncomp_size;
        // Unix hosts keep the file type in the high half of the external attributes: 0120000 is a symbolic link.
        constexpr mz_uint32 kTypeMask = 0170000;
        constexpr mz_uint32 kLink = 0120000;
        m.link = (st.m_version_made_by >> 8) == 3 && ((st.m_external_attr >> 16) & kTypeMask) == kLink;
        if (const std::optional<std::string> safe = safe_member(m.raw)) {
            m.name = *safe;
        } else {
            m.unsafe = true;
        }
        out.push_back(std::move(m));
    }
    return out;
}

bool under(std::string_view name, std::string_view prefix) {
    if (prefix.empty()) return true;
    return name.size() > prefix.size() && name.starts_with(prefix) && name[prefix.size()] == '/';
}

std::string relative_to(std::string_view name, std::string_view prefix) {
    return prefix.empty() ? std::string(name) : std::string(name.substr(prefix.size() + 1));
}

// The outermost mod.json folders up to kImportMaxDepth down; one inside another found mod is part of that mod.
std::vector<std::string> outermost(std::vector<std::string> roots) {
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    std::vector<std::string> out;
    for (const std::string& r : roots) {
        if (std::none_of(out.begin(), out.end(), [&](const std::string& o) { return under(r, o); })) out.push_back(r);
    }
    return out;
}

void set_invalid(FoundMod& m, InvalidReason reason, std::string detail) {
    if (m.reason != InvalidReason::None) return;  // the first reason is kept
    m.status = FoundStatus::Invalid;
    m.reason = reason;
    m.detail = std::move(detail);
}

void add_size(FoundMod& m, std::uint64_t size, std::string_view path) {
    ++m.files;
    m.bytes += size;
    if (size > kImportMaxFileBytes || m.bytes > kImportMaxBytes || m.files > kImportMaxFiles) {
        set_invalid(m, InvalidReason::TooBig, std::string(path));
    }
}

void read_manifest(FoundMod& m, const std::optional<std::string>& text, std::string_view folder,
                   std::optional<Manifest>& out) {
    if (!text) {
        set_invalid(m, InvalidReason::Unreadable, "mod.json");
        return;
    }
    ManifestResult parsed = parse_manifest_json(*text, folder);
    if (!parsed.manifest) {
        std::string why;
        for (const std::string& e : parsed.errors) why += (why.empty() ? "" : "; ") + e;
        set_invalid(m, InvalidReason::Manifest, why.empty() ? "mod.json" : why);
        return;
    }
    out = std::move(parsed.manifest);
    m.id = out->id;
    m.name = out->name.empty() ? out->id : out->name;
    m.version = out->version_text.empty() ? out->version.to_string() : out->version_text;
}

void find_folders(const fs::path& dir, const std::string& rel, int depth, std::vector<std::string>& out) {
    std::error_code ec;
    if (fs::is_regular_file(dir / kManifestFile, ec)) {
        out.push_back(rel);
        return;
    }
    if (depth >= kImportMaxDepth) return;
    std::vector<fs::path> children;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_symlink(e2) || !it->is_directory(e2)) continue;
        if (skipped_name(utf8(it->path().filename()))) continue;
        children.push_back(it->path());
    }
    std::sort(children.begin(), children.end());
    for (const fs::path& c : children) {
        find_folders(c, rel.empty() ? utf8(c.filename()) : rel + "/" + utf8(c.filename()), depth + 1, out);
    }
}

// Every file of a mod folder, relative with '/', hidden ones left out; links make the mod invalid.
std::vector<std::string> folder_files(const fs::path& root, FoundMod& m) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::none, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string name = utf8(it->path().filename());
        std::error_code e2;
        if (skipped_name(name)) {
            if (it->is_directory(e2) && !it->is_symlink(e2)) it.disable_recursion_pending();
            continue;
        }
        const std::string rel = utf8(it->path().lexically_relative(root));
        if (it->is_symlink(e2)) {
            set_invalid(m, InvalidReason::Link, rel);
            continue;
        }
        if (it->is_directory(e2)) continue;
        if (!it->is_regular_file(e2)) {
            set_invalid(m, InvalidReason::Unreadable, rel);
            continue;
        }
        add_size(m, it->file_size(e2), rel);
        out.push_back(rel);
    }
    if (ec) set_invalid(m, InvalidReason::Unreadable, ec.message());
    return out;
}

std::string folder_name_of(const ImportScan& scan, const std::string& location) {
    if (!location.empty()) return split(location).back();
    return utf8(scan.zip ? scan.source.stem() : scan.source.filename());
}

// Status against the installed mods, duplicates and the notes, once every mod of the pick was read.
void judge(ImportScan& scan, std::vector<std::optional<Manifest>>& manifests, const std::vector<InstalledMod>& installed,
           const Environment& env) {
    std::map<std::string, std::size_t> first;
    for (std::size_t i = 0; i < scan.mods.size(); ++i) {
        FoundMod& m = scan.mods[i];
        if (m.status == FoundStatus::Invalid || !manifests[i]) continue;
        if (const auto [it, fresh] = first.emplace(m.id, i); !fresh) {
            set_invalid(m, InvalidReason::Duplicate, scan.mods[it->second].location);
            continue;
        }
        m.status = FoundStatus::New;
        const auto inst = std::find_if(installed.begin(), installed.end(),
                                       [&](const InstalledMod& x) { return x.id == m.id; });
        if (inst != installed.end()) {
            m.installed_version = inst->version_text.empty() ? inst->version.to_string() : inst->version_text;
            m.installed_folder = inst->folder;
            const auto order = manifests[i]->version <=> inst->version;
            m.status = order > 0 ? FoundStatus::Update : order == 0 ? FoundStatus::Same : FoundStatus::Older;
        }
        const Manifest& man = *manifests[i];
        if (!engine_problems(man, env.engine).empty()) {
            std::string need = man.min_engine_version ? ">= " + man.min_engine_version->to_string()
                               : man.max_engine_version ? "<= " + man.max_engine_version->to_string()
                               : man.engine ? man.engine->text() : std::string();
            m.notes.push_back({"engine", need + " (" + env.engine.to_string() + ")", need, env.engine.to_string()});
        }
    }
    for (std::size_t i = 0; i < scan.mods.size(); ++i) {
        FoundMod& m = scan.mods[i];
        if (m.status == FoundStatus::Invalid || !manifests[i]) continue;
        for (const ModRef& dep : manifests[i]->dependencies) {
            bool present = false;
            for (const InstalledMod& x : installed) present |= x.id == dep.id && dep.versions.matches(x.version);
            for (std::size_t j = 0; j < scan.mods.size(); ++j) {
                present |= j != i && scan.mods[j].status != FoundStatus::Invalid && manifests[j] &&
                           manifests[j]->id == dep.id && dep.versions.matches(manifests[j]->version);
            }
            if (!present) {
                const std::string need = dep.versions.is_any() ? dep.id : dep.id + " " + dep.versions.text();
                m.notes.push_back({"dependency", need, need, std::string()});
            }
        }
        m.selected = m.status == FoundStatus::New || m.status == FoundStatus::Update;
    }
}

void scan_folder(ImportScan& scan, std::vector<std::optional<Manifest>>& manifests) {
    std::vector<std::string> roots;
    find_folders(scan.source, "", 0, roots);
    for (const std::string& loc : outermost(std::move(roots))) {
        FoundMod m;
        m.location = loc;
        const fs::path root = loc.empty() ? scan.source : scan.source / from_utf8(loc);
        std::optional<Manifest> man;
        read_manifest(m, read_text(root / kManifestFile), folder_name_of(scan, loc), man);
        folder_files(root, m);
        scan.mods.push_back(std::move(m));
        manifests.push_back(std::move(man));
    }
}

std::optional<std::string> zip_text(Zip& zip, const Member& member) {
    if (!member.readable || member.size > kManifestMaxBytes) return std::nullopt;
    size_t size = 0;
    void* data = mz_zip_reader_extract_to_heap(&zip.archive, member.index, &size, 0);
    if (data == nullptr) return std::nullopt;
    std::string text(static_cast<const char*>(data), size);
    mz_free(data);
    return text;
}

void scan_zip(ImportScan& scan, std::vector<std::optional<Manifest>>& manifests) {
    Zip zip;
    if (!zip.init(scan.source)) {
        scan.error = ImportError::BrokenZip;
        scan.detail = mz_zip_get_error_string(mz_zip_get_last_error(&zip.archive));
        return;
    }
    const std::vector<Member> members = list_members(zip);
    std::vector<std::string> roots;
    std::map<std::string, const Member*> manifest_of;
    for (const Member& m : members) {
        if (m.unsafe || m.directory) continue;
        std::vector<std::string> parts = split(m.name);
        if (parts.empty() || parts.back() != kManifestFile) continue;
        parts.pop_back();
        if (static_cast<int>(parts.size()) > kImportMaxDepth || hidden_path(parts)) continue;
        std::string loc;
        for (const std::string& p : parts) loc += (loc.empty() ? "" : "/") + p;
        roots.push_back(loc);
        manifest_of[loc] = &m;
    }
    for (const std::string& loc : outermost(std::move(roots))) {
        FoundMod m;
        m.location = loc;
        std::optional<Manifest> man;
        read_manifest(m, zip_text(zip, *manifest_of[loc]), folder_name_of(scan, loc), man);
        for (const Member& member : members) {
            if (member.unsafe) {
                // Unsafe names are not inside any folder; they spoil every mod of the archive.
                set_invalid(m, InvalidReason::UnsafePath, member.raw);
                continue;
            }
            if (!under(member.name, loc) || hidden_path(split(relative_to(member.name, loc)))) continue;
            if (member.link) {
                set_invalid(m, InvalidReason::Link, member.name);
            } else if (!member.readable) {
                set_invalid(m, InvalidReason::Unreadable, member.name);
            } else if (!member.directory) {
                add_size(m, member.size, member.name);
            }
        }
        scan.mods.push_back(std::move(m));
        manifests.push_back(std::move(man));
    }
}

// ---- writing

struct FileSink {
    std::ofstream out;
    static size_t write(void* opaque, mz_uint64, const void* data, size_t n) {
        auto* self = static_cast<FileSink*>(opaque);
        self->out.write(static_cast<const char*>(data), static_cast<std::streamsize>(n));
        return self->out ? n : 0;
    }
};

Result<void> stage_from_folder(const fs::path& root, const fs::path& dest) {
    FoundMod probe;
    const std::vector<std::string> files = folder_files(root, probe);
    if (probe.reason != InvalidReason::None) {
        return Result<void>::fail("cannot read " + utf8(root) + ": " + probe.detail);
    }
    for (const std::string& rel : files) {
        const fs::path target = dest / from_utf8(rel);
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        if (!ec) fs::copy_file(root / from_utf8(rel), target, fs::copy_options::overwrite_existing, ec);
        if (ec) return Result<void>::fail("cannot copy " + rel + ": " + ec.message());
    }
    return Result<void>::ok();
}

Result<void> stage_from_zip(Zip& zip, const std::vector<Member>& members, const std::string& loc, const fs::path& dest) {
    for (const Member& member : members) {
        if (member.unsafe) return Result<void>::fail("unsafe path in the archive: " + member.raw);
        if (!under(member.name, loc)) continue;
        const std::string rel = relative_to(member.name, loc);
        if (rel.empty() || hidden_path(split(rel))) continue;
        if (member.link || !member.readable) return Result<void>::fail("cannot unpack " + member.name);
        const fs::path target = dest / from_utf8(rel);
        std::error_code ec;
        if (member.directory) {
            fs::create_directories(target, ec);
            if (ec) return Result<void>::fail("cannot make " + rel + ": " + ec.message());
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        if (ec) return Result<void>::fail("cannot make " + rel + ": " + ec.message());
        FileSink sink;
        sink.out.open(target, std::ios::binary | std::ios::trunc);
        if (!sink.out) return Result<void>::fail("cannot write " + rel);
        if (!mz_zip_reader_extract_to_callback(&zip.archive, member.index, &FileSink::write, &sink, 0)) {
            return Result<void>::fail("cannot unpack " + member.name + ": " +
                                      mz_zip_get_error_string(mz_zip_get_last_error(&zip.archive)));
        }
        sink.out.close();
        if (!sink.out) return Result<void>::fail("cannot write " + rel);
    }
    return Result<void>::ok();
}

std::string random_suffix() {
    std::random_device rd;
    std::mt19937_64 gen((static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^
                        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (std::uint64_t v = gen(), i = 0; i < 12; ++i, v >>= 4) out += kHex[v & 15];
    return out;
}

Picker& picker() {
    static Picker p;
    return p;
}

}  // namespace

const char* found_status_name(FoundStatus status) {
    switch (status) {
    case FoundStatus::New: return "new";
    case FoundStatus::Update: return "update";
    case FoundStatus::Same: return "same";
    case FoundStatus::Older: return "older";
    case FoundStatus::Invalid: return "invalid";
    }
    return "?";
}

const char* invalid_reason_name(InvalidReason reason) {
    switch (reason) {
    case InvalidReason::None: return "";
    case InvalidReason::Manifest: return "manifest";
    case InvalidReason::Duplicate: return "duplicate";
    case InvalidReason::Link: return "link";
    case InvalidReason::UnsafePath: return "unsafe_path";
    case InvalidReason::TooBig: return "too_big";
    case InvalidReason::Unreadable: return "unreadable";
    }
    return "?";
}

const char* import_error_name(ImportError error) {
    switch (error) {
    case ImportError::None: return "";
    case ImportError::NotFound: return "not_found";
    case ImportError::BrokenZip: return "broken_zip";
    case ImportError::NoMods: return "no_mods";
    case ImportError::Unreadable: return "unreadable";
    }
    return "?";
}

const char* import_state_name(ImportState state) {
    switch (state) {
    case ImportState::Idle: return "idle";
    case ImportState::Picking: return "picking";
    case ImportState::Ready: return "ready";
    }
    return "?";
}

std::vector<InstalledMod> installed_mods(const Registry& registry) {
    std::vector<InstalledMod> out;
    for (const ResolvedMod& m : registry.pending().mods) {
        const Manifest& man = m.candidate.manifest;
        out.push_back({man.id, man.version, man.version_text, m.candidate.folder});
    }
    return out;
}

ImportScan scan_import(const fs::path& picked, const std::vector<InstalledMod>& installed, const Environment& env) {
    ImportScan scan;
    // A picked mod.json stands for its folder.
    const fs::path source = picked.filename() == fs::path(kManifestFile) ? picked.parent_path() : picked;
    scan.source = source;
    std::error_code ec;
    const fs::file_status st = fs::status(source, ec);
    if (ec || !fs::exists(st)) {
        scan.error = ImportError::NotFound;
        scan.detail = utf8(source);
        return scan;
    }
    std::vector<std::optional<Manifest>> manifests;
    if (fs::is_directory(st)) {
        scan_folder(scan, manifests);
    } else if (fs::is_regular_file(st) || ends_with_zip(source)) {
        scan.zip = true;
        scan_zip(scan, manifests);
    } else {
        scan.error = ImportError::Unreadable;
        scan.detail = utf8(source);
        return scan;
    }
    if (scan.error == ImportError::None && scan.mods.empty()) {
        scan.error = ImportError::NoMods;
        scan.detail = utf8(source);
    }
    judge(scan, manifests, installed, env);
    return scan;
}

Result<ImportReport> install_import(const ImportScan& scan, const fs::path& mods_dir) {
    std::vector<const FoundMod*> chosen;
    for (const FoundMod& m : scan.mods) {
        if (m.selected && m.status != FoundStatus::Invalid && !m.id.empty()) chosen.push_back(&m);
    }
    if (chosen.empty()) return Result<ImportReport>::fail("no mod chosen");
    std::error_code ec;
    fs::create_directories(mods_dir, ec);
    if (ec) return Result<ImportReport>::fail("cannot make " + utf8(mods_dir) + ": " + ec.message());

    // A hidden folder inside the mods folder: the registry skips it, and the final moves stay on one file system.
    const fs::path staging = mods_dir / (".import-" + random_suffix());
    fs::create_directories(staging / "new", ec);
    if (ec) return Result<ImportReport>::fail("cannot make " + utf8(staging) + ": " + ec.message());
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } cleanup{staging};

    std::unique_ptr<Zip> zip;
    std::vector<Member> members;
    if (scan.zip) {
        zip = std::make_unique<Zip>();
        if (!zip->init(scan.source)) return Result<ImportReport>::fail("cannot open " + utf8(scan.source));
        members = list_members(*zip);
    }

    // Each mod's folder: the installed copy's, else its id; an unrelated folder of that name is not touched.
    std::vector<std::string> folders;
    std::set<std::string> taken;
    for (const FoundMod* m : chosen) {
        std::string folder = m->installed_folder;
        if (folder.empty()) {
            folder = m->id;
            for (int n = 2; fs::exists(mods_dir / from_utf8(folder), ec) || taken.count(folder) != 0; ++n) {
                folder = m->id + "-" + std::to_string(n);
            }
        }
        taken.insert(folder);
        folders.push_back(folder);
        const fs::path dest = staging / "new" / from_utf8(folder);
        fs::create_directories(dest, ec);
        if (ec) return Result<ImportReport>::fail("cannot make " + utf8(dest) + ": " + ec.message());
        const Result<void> staged =
            scan.zip ? stage_from_zip(*zip, members, m->location, dest)
                     : stage_from_folder(m->location.empty() ? scan.source : scan.source / from_utf8(m->location), dest);
        if (!staged) return Result<ImportReport>::fail(m->id + ": " + staged.error());
        if (!fs::is_regular_file(dest / kManifestFile, ec)) {
            return Result<ImportReport>::fail(m->id + ": mod.json was not copied");
        }
    }

    // Everything is on disk: move it in, an installed copy aside first; a failed move puts back what moved.
    fs::create_directories(staging / "old", ec);
    struct Moved {
        fs::path target;
        fs::path old;  // empty: there was none
    };
    std::vector<Moved> moved;
    auto undo = [&]() {
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            std::error_code e;
            fs::remove_all(it->target, e);
            if (!it->old.empty()) fs::rename(it->old, it->target, e);
        }
    };
    ImportReport report;
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        const fs::path target = mods_dir / from_utf8(folders[i]);
        Moved mv{target, {}};
        if (fs::exists(target, ec)) {
            mv.old = staging / "old" / from_utf8(folders[i]);
            fs::rename(target, mv.old, ec);
            if (ec) {
                undo();
                return Result<ImportReport>::fail("cannot replace " + utf8(target) + ": " + ec.message());
            }
        }
        fs::rename(staging / "new" / from_utf8(folders[i]), target, ec);
        if (ec) {
            if (!mv.old.empty()) {
                std::error_code e;
                fs::rename(mv.old, target, e);
            }
            undo();
            return Result<ImportReport>::fail("cannot move " + utf8(target) + " in place: " + ec.message());
        }
        moved.push_back(mv);
        (mv.old.empty() ? report.added : report.replaced).push_back(chosen[i]->id);
    }
    return Result<ImportReport>::ok(std::move(report));
}

// ---- the session

void set_import_picker(Picker p) { picker() = std::move(p); }

bool ImportSession::can_pick(PickKind kind) const {
    const Picker& p = picker();
    return p.open && (!p.available || p.available(kind));
}

bool ImportSession::pick(PickKind kind) {
    if (!can_pick(kind)) return false;
    {
        std::lock_guard lock(mutex_);
        if (waiting_) return false;
        waiting_ = true;
        arrived_.reset();
    }
    state_ = ImportState::Picking;
    changed_ = true;
    if (!picker().open(kind, [this](std::string path) { deliver(std::move(path)); })) {
        std::lock_guard lock(mutex_);
        waiting_ = false;
        state_ = ImportState::Idle;
        return false;
    }
    return true;
}

void ImportSession::deliver(std::string path) {
    std::lock_guard lock(mutex_);
    waiting_ = false;
    arrived_ = std::move(path);
}

bool ImportSession::offer_drop(const std::string& path) {
    std::lock_guard lock(mutex_);
    if (std::chrono::steady_clock::now() - polled_ > std::chrono::seconds(1)) return false;
    arrived_ = path;
    return true;
}

bool ImportSession::poll() {
    if (picker().poll) {
        bool waiting = false;
        {
            std::lock_guard lock(mutex_);
            waiting = waiting_;
        }
        if (waiting) picker().poll();
    }
    std::optional<std::string> path;
    {
        std::lock_guard lock(mutex_);
        polled_ = std::chrono::steady_clock::now();
        path.swap(arrived_);
    }
    if (path) {
        if (path->empty()) {
            if (state_ == ImportState::Picking) state_ = ImportState::Idle;  // cancelled
            changed_ = true;
        } else {
            scan(from_utf8(*path));
        }
    }
    const bool out = changed_;
    changed_ = false;
    return out;
}

ImportState ImportSession::state() const { return state_; }

void ImportSession::scan(const fs::path& path) {
    Registry& reg = admin_registry();
    scan_ = scan_import(path, installed_mods(reg), reg.config().env);
    state_ = ImportState::Ready;
    last_report_.reset();
    last_error_.clear();
    changed_ = true;
}

bool ImportSession::select(std::size_t index, bool on) {
    if (index >= scan_.mods.size() || scan_.mods[index].status == FoundStatus::Invalid) return false;
    scan_.mods[index].selected = on;
    return true;
}

Result<ImportReport> ImportSession::run(Registry& reg) {
    Result<ImportReport> done = reg.config().dirs.empty() ? Result<ImportReport>::fail("no mods folder")
                                                          : install_import(scan_, reg.config().dirs.front());
    changed_ = true;
    if (picker().release && !scan_.source.empty()) picker().release(utf8(scan_.source));
    if (!done) {
        last_error_ = done.error();
        return done;
    }
    reg.rescan_pending();
    last_report_ = done.value();
    last_error_.clear();
    state_ = ImportState::Idle;
    scan_ = ImportScan{};
    return done;
}

void ImportSession::clear() {
    std::lock_guard lock(mutex_);
    arrived_.reset();
    state_ = waiting_ ? ImportState::Picking : ImportState::Idle;
    scan_ = ImportScan{};
    last_report_.reset();
    last_error_.clear();
    changed_ = true;
}

ImportSession& import_session() {
    static ImportSession s;
    return s;
}

}  // namespace sh::mods
