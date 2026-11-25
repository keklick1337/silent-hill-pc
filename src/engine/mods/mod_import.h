// Importing mods the player picked: a folder or a .zip holding one mod or several. scan_import() finds every mod in
// the pick and says what importing it would do; install_import() copies (or unpacks) the chosen ones into the mods
// folder, all or nothing. ImportSession is the process-wide state the Mods screen (sh.mods.import_*) and the
// `modimport` console command share: the system's file dialog answers later, from any thread.
#pragma once

#include "engine/mods/load_order.h"
#include "formats/common/status.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sh::mods {

class Registry;

// Limits of one pick: deeper folders are not looked into, bigger mods are refused (a broken or hostile archive
// must not fill the disk).
inline constexpr int kImportMaxDepth = 4;  // folders between the pick and a mod.json
inline constexpr std::uint64_t kImportMaxFiles = 200000;
inline constexpr std::uint64_t kImportMaxFileBytes = 4ull << 30;
inline constexpr std::uint64_t kImportMaxBytes = 16ull << 30;

enum class FoundStatus {
    New,      // no mod with this id is installed
    Update,   // replaces an older installed version
    Same,     // the installed mod has this version
    Older,    // the installed mod is newer
    Invalid,  // cannot be imported: `reason`
};
const char* found_status_name(FoundStatus status);

// Why a found mod cannot be imported; the text keys of the Mods screen are "ui.mods.import.invalid.<name>".
enum class InvalidReason { None, Manifest, Duplicate, Link, UnsafePath, TooBig, Unreadable };
const char* invalid_reason_name(InvalidReason reason);

// Something about a mod that still imports but will not load as it is ("ui.mods.import.note.<kind>").
struct ImportNote {
    std::string kind;    // "engine" (needs another engine version), "dependency" (needs a mod that is not there)
    std::string detail;  // the version range or the missing mod, as written
    // The parts of the note's text ("{need}", "{have}" of ui.mods.import.note.<kind>): engine, the version range the
    // mod needs and the running engine's version; dependency, the missing mod with the versions it must have.
    std::string need;
    std::string have;
};

struct FoundMod {
    std::string location;  // the mod's folder inside the pick, '/'-separated; "" = the pick itself
    std::string id;        // "" when the manifest is unusable
    std::string name;
    std::string version;
    FoundStatus status = FoundStatus::New;
    InvalidReason reason = InvalidReason::None;
    std::string detail;             // the manifest's error or the offending path, in English
    std::string installed_version;  // Update / Same / Older
    std::string installed_folder;   // the folder the installed copy is in (replaced on import)
    std::vector<ImportNote> notes;
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
    bool selected = false;  // by default: new mods and updates
};

// Why the pick as a whole gives nothing to import ("ui.mods.import.error.<name>").
enum class ImportError { None, NotFound, BrokenZip, NoMods, Unreadable };
const char* import_error_name(ImportError error);

struct ImportScan {
    std::filesystem::path source;
    bool zip = false;
    ImportError error = ImportError::None;
    std::string detail;
    std::vector<FoundMod> mods;
};

// A mod already in the mods folder, as the scan compares with it.
struct InstalledMod {
    std::string id;
    Version version;
    std::string version_text;
    std::string folder;
};
std::vector<InstalledMod> installed_mods(const Registry& registry);

// Finds the mods in `source`: a folder (a mod itself, or mods up to kImportMaxDepth folders down) or a .zip (the
// same layouts inside it). Folders and members whose name starts with '.' are left out, so is __MACOSX.
ImportScan scan_import(const std::filesystem::path& source, const std::vector<InstalledMod>& installed,
                       const Environment& env);

struct ImportReport {
    std::vector<std::string> added;     // ids
    std::vector<std::string> replaced;  // ids
};

// Copies every selected, valid mod of `scan` into `mods_dir` (made when missing) as `mods_dir/<id>` (or the folder
// the installed copy is in). Everything is written into a hidden folder of `mods_dir` first and moved in place at
// the end; a failure leaves the mods folder as it was.
Result<ImportReport> install_import(const ImportScan& scan, const std::filesystem::path& mods_dir);

// ---- the session of the Mods screen

enum class PickKind { Folder, Zip };

// Opens the system's dialog for `kind` and returns at once; `done` gets the picked path ("" = cancelled), possibly
// from another thread. False when this platform has no such dialog.
using PickDone = std::function<void(std::string path)>;
struct Picker {
    std::function<bool(PickKind kind, PickDone done)> open;
    std::function<bool(PickKind kind)> available;
    std::function<void()> poll;  // main thread, every ImportSession::poll() while a dialog is open
    // Main thread: the import is done with `path` (a run finished, well or not); a copy the dialog made of the picked
    // file (Android) can go.
    std::function<void(const std::string& path)> release;
};
void set_import_picker(Picker picker);

enum class ImportState { Idle, Picking, Ready };
const char* import_state_name(ImportState state);

class ImportSession {
public:
    bool can_pick(PickKind kind) const;
    bool pick(PickKind kind);
    // A path dropped on the window: taken only while the import page polls (it is open), else ignored.
    bool offer_drop(const std::string& path);

    // Main thread, every frame of the import page: a path the dialog gave is scanned here. True when the state or
    // the scan changed since the last call.
    bool poll();
    ImportState state() const;
    // Scans `path` now (the console, tests, a drop).
    void scan(const std::filesystem::path& path);
    const ImportScan& result() const { return scan_; }
    bool select(std::size_t index, bool on);
    // Installs the selection into the registry's first mods folder and reads the folders again, so the new mods are
    // listed (switched off, as every new mod) at once. The session goes back to Idle. Either way the picked file is
    // released to the picker (Picker::release).
    Result<ImportReport> run(Registry& registry);
    void clear();
    // The outcome of the last run, for the page shown after it.
    const std::optional<ImportReport>& last_report() const { return last_report_; }
    const std::string& last_error() const { return last_error_; }

private:
    void deliver(std::string path);

    mutable std::mutex mutex_;  // guards `arrived_`, `waiting_`, `polled_`
    std::optional<std::string> arrived_;
    bool waiting_ = false;
    std::chrono::steady_clock::time_point polled_{};
    ImportState state_ = ImportState::Idle;
    ImportScan scan_;
    std::optional<ImportReport> last_report_;
    std::string last_error_;
    bool changed_ = false;
};

ImportSession& import_session();

}  // namespace sh::mods
