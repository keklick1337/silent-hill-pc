#include "import/import.h"

#include "import/crc32_stream.h"
#include "import/index.h"
#include "import/input.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace sh::import {

namespace {

namespace fs = std::filesystem;

// 2: palette variants of a TIM are not written as NAME.pNN.png files.
// 3: 4/8-bit TIMs keep their indices in convert/indexed/; convert/textures/ holds true-colour pictures baked in the
//    CLUT rows the geometry draws (texture_bake.h), and the glbs name those pictures.
// 4: convert/fonts/game.png + game.font.json, the game font.
// 5: maps/areas/*.glb list every model of each cell's LM ("<CELL>/models") and of the global PLM by name, which the
//    scene draws world objects from (docs/RENDER.md section 6).
// 6: each cell root of maps/areas/*.glb lists its lamp glows (extras "glows": [x, y, z, kind] in cell space, Q8), which
//    the engine draws instead of reading them from the cell file.
// 7: each cell root and library root of maps/areas/*.glb lists its texture names (extras "materials", file order),
//    from which the engine streams the cells without reading the cell files.
// 8: convert/models/ITEM/*.model.json map each TMD packet to its mesh primitive and first triangle and keep the
//    packet bytes, which the item models are drawn from.
// 9: maps/<map>/points.json and map.json name a point's two shape bytes by its use (heading_q8 / half_width_q4, or
//    half_x_q2 / half_z_q2 for a box trigger's point) instead of param0 / param1; the readers accept the old keys.
//    The imported files explain themselves with "description" instead of "_comment" (points.json, convert/
//    materials.json, convert/texture_bake.json), and events.json names the event flags of gamedata/flags.
// 10: semi-transparent glb materials name their PS1 blend mode in extras ("abr", "blend_mode").
// 11: maps/areas/*.glb mark a map primitive semi-transparent only in models whose draw chain gives it the GPU's
//     semi-transparency bit (lit or far-drawn models); the flat chain's STP texels are solid (cut-out grates).
// 12: nothing new in the files; a game whose import is 11 imports again, because the first run of 11 kept the
//     maps/ derivatives (maps/areas/*.glb) of the previous import instead of rewriting them (first_run.cpp).
// 13: convert/fonts/game.png draws the Latin-1 accents from the disc's own letters and the project's marks and
//     signs (assets/font/glyphs/) instead of cells cut from the PAL disc's font; the cell numbers past the disc's
//     84 change with it.
constexpr int kImporterVersion = 13;
constexpr std::size_t kChunkBytes = 1u << 20;
// The converters see whole files; this caps how many bytes of them are in memory at once across the workers.
constexpr std::uint64_t kConvertMemoryBudget = 256ull << 20;
// HILL. holds 9 voice tracks (ids 2044..2052), then 21 movies.
constexpr std::uint16_t kFirstMovieId = 2053;
constexpr std::size_t kFailuresInMessage = 5;

// ---- progress ---------------------------------------------------------------------------------------------------

enum class Stage { Read, Raw, Convert, Verify, Commit };
constexpr std::array<const char*, 5> kStageNames = {"read", "raw", "convert", "verify", "commit"};

// Splits 0..1 between the stages that will run. Stages that are off get no share. Never reports a lower value.
class Progress {
public:
    Progress(const ProgressFn& fn, bool convert, bool verify) : fn_(fn) {
        const std::array<double, 5> weight = {8, 30, convert ? 30.0 : 0.0, verify ? 20.0 : 0.0, 2};
        double total = 0;
        for (const double w : weight) {
            total += w;
        }
        double at = 0;
        for (std::size_t i = 0; i < weight.size(); ++i) {
            begin_[i] = at / total;
            at += weight[i];
            end_[i] = at / total;
        }
    }

    void report(Stage stage, double local) {
        if (!fn_) {
            return;
        }
        const auto i = static_cast<std::size_t>(stage);
        local = std::clamp(local, 0.0, 1.0);
        last_ = std::max(last_, begin_[i] + (end_[i] - begin_[i]) * local);
        fn_(kStageNames[i], last_);
    }

private:
    const ProgressFn& fn_;
    std::array<double, 5> begin_{};
    std::array<double, 5> end_{};
    double last_ = 0;
};

bool is_cancelled(const std::atomic<bool>* cancel) {
    return cancel != nullptr && cancel->load();
}

// ---- helpers ----------------------------------------------------------------------------------------------------

std::string process_id() {
#ifdef _WIN32
    return std::to_string(_getpid());
#else
    return std::to_string(getpid());
#endif
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    return text;
}

// Runs job(0..count-1) on `threads` workers. A job that returns false stops the pending ones.
// `tick` runs on the calling thread only, between jobs when single-threaded and about every 25 ms otherwise.
void run_jobs(std::size_t count, unsigned threads, const std::function<bool(std::size_t)>& job,
              const std::function<void()>& tick) {
    const std::size_t workers = std::min<std::size_t>(std::max(threads, 1u), count);
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> running{0};

    const auto work = [&] {
        for (;;) {
            const std::size_t i = next.fetch_add(1);
            if (stop.load() || i >= count) {
                break;
            }
            if (!job(i)) {
                stop.store(true);
            }
        }
        running.fetch_sub(1);
    };

    std::vector<std::thread> pool;
    if (workers > 1) {
        running.store(workers);
        try {
            for (std::size_t w = 0; w < workers; ++w) {
                pool.emplace_back(work);
            }
        } catch (const std::system_error&) {
            // Fewer threads than asked for: the ones that started drain the queue.
            running.store(pool.size());
        }
    }
    if (pool.empty()) {
        running.store(1);
        for (std::size_t i = 0; i < count; ++i) {
            if (!job(i)) {
                break;
            }
            tick();
        }
        tick();
        return;
    }
    while (running.load() > 0) {
        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    for (std::thread& t : pool) {
        t.join();
    }
    tick();
}

// Bounds the bytes of whole files held by the converting workers. A single file larger than the limit still goes
// through when nothing else is in flight.
class ByteBudget {
public:
    explicit ByteBudget(std::uint64_t limit) : limit_(limit) {}

    void acquire(std::uint64_t bytes) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return used_ == 0 || used_ + bytes <= limit_; });
        used_ += bytes;
    }

    void release(std::uint64_t bytes) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            used_ -= bytes;
        }
        ready_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::uint64_t limit_;
    std::uint64_t used_ = 0;
};

Result<void> write_file(const fs::path& path, std::span<const std::uint8_t> bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        return Result<void>::fail("cannot create " + path.parent_path().string() + ": " + ec.message());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        return Result<void>::fail("cannot write " + path.string());
    }
    return Result<void>::ok();
}

// A converter output path goes under convert/ and into a tab/semicolon separated index, so it is held to a strict form.
bool valid_output_path(const std::string& path) {
    if (path.empty() || path.size() > 200 || path.front() == '/' || path.back() == '/') {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) {
            end = path.size();
        }
        const std::string_view segment(path.data() + start, end - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        start = end + 1;
    }
    return std::none_of(path.begin(), path.end(), [](unsigned char c) {
        return c < 0x20 || c == 0x7f || c == '\\' || c == ':' || c == ';' || c == '*' || c == '?' || c == '"' ||
               c == '<' || c == '>' || c == '|';
    });
}

// ---- the file list ----------------------------------------------------------------------------------------------

struct Item {
    std::string path;    // game path
    std::string source;
    std::string format;
    const ByteSource* container = nullptr;
    std::uint64_t offset = 0;
    std::uint64_t stored = 0;   // bytes written to raw/
    std::uint64_t logical = 0;  // bytes the game gets for the path
    std::uint32_t crc = 0;      // of the stored bytes
};

std::string format_of(const archive::Entry& entry) {
    if (entry.region == archive::Region::Hill) {
        return entry.id < kFirstMovieId ? "xa" : "fmv";
    }
    const std::size_t dot = entry.name.rfind('.');
    return dot == std::string::npos ? "raw" : lower(entry.name.substr(dot + 1));
}

// SYSTEM.CNF and SLUS_007.07 first, then the table by id. The same order on every run is what makes index.tsv repeat.
std::vector<Item> build_items(const GameDisc& game) {
    std::vector<Item> items;
    items.reserve(game.entries.size() + 2);

    Item cnf;
    cnf.path = "SYSTEM.CNF";
    cnf.source = "ISO:file";
    cnf.format = "cnf";
    cnf.container = game.system_cnf.get();
    cnf.stored = cnf.logical = game.system_cnf->size();
    items.push_back(cnf);

    Item exe;
    exe.path = "SLUS_007.07";
    exe.source = "SLUS:file";
    exe.format = "exe";
    exe.container = game.slus.get();
    exe.stored = exe.logical = game.slus->size();
    items.push_back(exe);

    for (const archive::Entry& e : game.entries) {
        Item item;
        item.path = e.name;
        item.format = format_of(e);
        item.offset = e.offset;
        item.logical = e.size;
        if (e.region == archive::Region::Silent) {
            item.source = "SILENT:id=" + std::to_string(e.id);
            item.container = game.silent.get();
            item.stored = std::uint64_t{e.sectors} * 2048;
        } else {
            item.source = "HILL:lba=" + std::to_string(e.lba) + "+" + std::to_string(e.sectors);
            item.container = game.hill.get();
            item.stored = e.size;
        }
        items.push_back(std::move(item));
    }
    return items;
}

// ---- stage: raw -------------------------------------------------------------------------------------------------

Result<void> extract_item(Item& item, const fs::path& destination, std::atomic<std::uint64_t>& bytes_done,
                          const std::atomic<bool>* cancel) {
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        return Result<void>::fail("cannot create " + destination.parent_path().string() + ": " + ec.message());
    }
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Result<void>::fail("cannot create " + destination.string());
    }

    Crc32Stream crc;
    std::vector<std::uint8_t> chunk(static_cast<std::size_t>(std::min<std::uint64_t>(item.stored, kChunkBytes)));
    for (std::uint64_t done = 0; done < item.stored;) {
        if (is_cancelled(cancel)) {
            return Result<void>::fail("cancelled");
        }
        const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(item.stored - done, chunk.size()));
        if (!item.container->read_at(item.offset + done, chunk.data(), n)) {
            return Result<void>::fail("cannot read " + item.path + " (" + item.source + ") from the image at byte " +
                                      std::to_string(done));
        }
        crc.update(chunk.data(), n);
        out.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(n));
        done += n;
        bytes_done.fetch_add(n);
    }
    out.close();
    if (!out) {
        return Result<void>::fail("cannot write " + destination.string());
    }
    item.crc = crc.value();
    return Result<void>::ok();
}

// ---- stage: convert ---------------------------------------------------------------------------------------------

struct ConvertedFile {
    std::string path;  // relative to convert/
    std::uint64_t size;
};

struct ConverterCount {
    std::uint64_t accepted = 0;
    std::uint64_t outputs = 0;
    std::uint64_t round_trips = 0;
};

struct ItemResult {
    std::vector<ConvertedFile> files;
    std::vector<ConversionFailure> failures;
    std::vector<ConverterCount> counts;  // per converter
    std::string fatal;                   // an I/O error: the import stops
};

// Each convert/ path may be produced once. Compared in lower case, since the target may be a case-folding file system.
class OutputRegistry {
public:
    // The game path that already owns `path`, or an empty string when it was free.
    std::string claim(const std::string& path, const std::string& owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto [it, inserted] = owners_.emplace(lower(path), owner);
        return inserted ? std::string() : it->second;
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::string, std::string> owners_;
};

std::string first_difference(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    if (a.size() != b.size()) {
        return "size " + std::to_string(b.size()) + " instead of " + std::to_string(a.size());
    }
    const auto mismatch = std::mismatch(a.begin(), a.end(), b.begin());
    return "first difference at byte " + std::to_string(mismatch.first - a.begin());
}

struct ConvertContext {
    ConvertContext(const std::vector<std::unique_ptr<convert::Converter>>& list, bool verify_roundtrip,
                   fs::path raw, fs::path converted)
        : converters(list), verify(verify_roundtrip), raw_dir(std::move(raw)), convert_dir(std::move(converted)) {}

    const std::vector<std::unique_ptr<convert::Converter>>& converters;
    bool verify;
    const std::atomic<bool>* cancel = nullptr;
    fs::path raw_dir;
    fs::path convert_dir;
    ByteBudget budget{kConvertMemoryBudget};
    OutputRegistry registry;
    std::atomic<std::uint64_t> bytes_done{0};
};

// Runs every converter on one raw file. Conversion errors go to `result.failures` and the import goes on with the
// other files, so one run lists everything that is wrong; I/O errors go to `result.fatal`.
void convert_item(const Item& item, ConvertContext& ctx, ItemResult& result) {
    result.counts.assign(ctx.converters.size(), ConverterCount{});
    if (item.logical == 0) {
        return;
    }
    if (is_cancelled(ctx.cancel)) {
        result.fatal = "cancelled";
        return;
    }

    ctx.budget.acquire(item.logical);
    struct Release {
        ConvertContext& ctx;
        std::uint64_t bytes;
        ~Release() { ctx.budget.release(bytes); }
    } release{ctx, item.logical};

    const fs::path file = ctx.raw_dir / fs::path(item.path);
    const std::unique_ptr<ByteSource> source = open_file_source(file);
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(item.logical));
    if (!source || !source->read_at(0, raw.data(), raw.size())) {
        result.fatal = "cannot read back " + file.string();
        return;
    }

    for (std::size_t ci = 0; ci < ctx.converters.size(); ++ci) {
        if (is_cancelled(ctx.cancel)) {
            result.fatal = "cancelled";
            return;
        }
        const convert::Converter& converter = *ctx.converters[ci];
        const std::string name(converter.name());
        const auto fail = [&](std::string message) {
            result.failures.push_back({item.path, name, std::move(message)});
        };

        std::vector<convert::Output> outputs;
        try {
            if (!converter.accepts(raw, item.path)) {
                continue;
            }
            ++result.counts[ci].accepted;
            auto decoded = converter.decode(raw, item.path);
            if (!decoded) {
                fail("decode failed: " + decoded.error());
                continue;
            }
            outputs = std::move(decoded.value());

            if (std::any_of(outputs.begin(), outputs.end(),
                            [](const convert::Output& o) { return !valid_output_path(o.relative_path); })) {
                const auto bad = std::find_if(outputs.begin(), outputs.end(),
                                              [](const convert::Output& o) { return !valid_output_path(o.relative_path); });
                fail("decode gave an unusable output path \"" + bad->relative_path + "\"");
                continue;
            }

            if (ctx.verify && converter.lossless()) {
                auto encoded = converter.encode(outputs);
                if (!encoded) {
                    fail("encode failed: " + encoded.error());
                    continue;
                }
                if (encoded.value().size() != raw.size() ||
                    !std::equal(raw.begin(), raw.end(), encoded.value().begin())) {
                    fail("round trip differs from the original: " + first_difference(raw, encoded.value()));
                    continue;
                }
                ++result.counts[ci].round_trips;
            }
        } catch (const std::exception& e) {
            fail(std::string("converter threw: ") + e.what());
            continue;
        }

        for (const convert::Output& output : outputs) {
            const std::string owner = ctx.registry.claim(output.relative_path, item.path);
            if (!owner.empty()) {
                fail("output " + output.relative_path + " is already produced from " + owner);
                break;
            }
            const Result<void> written = write_file(ctx.convert_dir / fs::path(output.relative_path), output.bytes);
            if (!written) {
                result.fatal = written.error();
                return;
            }
            result.files.push_back({output.relative_path, output.bytes.size()});
            ++result.counts[ci].outputs;
        }
    }
    ctx.bytes_done.fetch_add(item.logical);
}

// ---- stage: verify ----------------------------------------------------------------------------------------------

Result<void> check_file_crc(const fs::path& file, std::uint64_t size, std::uint32_t crc,
                            std::atomic<std::uint64_t>& bytes_done, const std::atomic<bool>* cancel) {
    std::error_code ec;
    const std::uintmax_t on_disk = fs::file_size(file, ec);
    if (ec || on_disk != size) {
        return Result<void>::fail(file.string() + " has " + (ec ? std::string("no size") : std::to_string(on_disk) + " bytes") +
                                  ", the index says " + std::to_string(size));
    }
    std::ifstream in(file, std::ios::binary);
    Crc32Stream actual;
    std::vector<char> chunk(static_cast<std::size_t>(std::min<std::uint64_t>(std::max<std::uint64_t>(size, 1), kChunkBytes)));
    while (in.read(chunk.data(), static_cast<std::streamsize>(chunk.size())) || in.gcount() > 0) {
        if (is_cancelled(cancel)) {
            return Result<void>::fail("cancelled");
        }
        actual.update(chunk.data(), static_cast<std::size_t>(in.gcount()));
        bytes_done.fetch_add(static_cast<std::uint64_t>(in.gcount()));
    }
    if (actual.value() != crc) {
        return Result<void>::fail(file.string() + " has CRC32 " + crc32_hex(actual.value()) + ", the index says " +
                                  crc32_hex(crc));
    }
    return Result<void>::ok();
}

// ---- version.cfg and the final swap ------------------------------------------------------------------------------

std::string version_cfg(const std::string& image_name, const std::string& image_sha256, bool verify,
                        const std::vector<std::unique_ptr<convert::Converter>>& converters) {
    std::string names;
    for (const auto& c : converters) {
        names += (names.empty() ? "" : ",") + std::string(c->name());
    }
    std::string text;
    text += "importer = " + std::to_string(kImporterVersion) + "\n";
    text += "image_sha256 = " + image_sha256 + "\n";
    text += "image_name = " + image_name + "\n";
    text += std::string("game = ") + kGameId + "\n";
    text += std::string("verify = ") + (verify ? "1" : "0") + "\n";
    text += "converters = " + (names.empty() ? std::string("none") : names) + "\n";
    return text;
}

// Deletes the temporary directories and the .iso alias unless the import succeeded.
struct Scratch {
    fs::path staging;
    fs::path alias;
    bool keep_staging = false;

    ~Scratch() {
        std::error_code ec;
        if (!keep_staging) {
            fs::remove_all(staging, ec);
        }
        fs::remove(alias, ec);
    }
};

Result<void> run_import(const Request& request, const std::vector<std::unique_ptr<convert::Converter>>& converters,
                        const ProgressFn& progress_fn, Report& report) {
    using R = Result<void>;
    std::error_code ec;

    if (request.image.empty()) {
        return R::fail("no image given (import_image)");
    }
    if (request.gamedata_dir.empty()) {
        return R::fail("no target directory given (gamedata_dir)");
    }
    for (const auto& c : converters) {
        if (!c) {
            return R::fail("the converter list has a null entry");
        }
    }

    fs::path target = fs::absolute(request.gamedata_dir, ec).lexically_normal();
    if (!target.has_filename()) {
        target = target.parent_path();
    }
    if (ec || target.filename().empty() || target == target.root_path()) {
        return R::fail("bad target directory: " + request.gamedata_dir.string());
    }

    // An empty directory counts as absent: nothing in it can be lost.
    const bool exists = fs::exists(target, ec);
    if (exists && !fs::is_directory(target, ec)) {
        return R::fail(target.string() + " exists and is not a directory");
    }
    const bool replace = exists && (request.overwrite || fs::is_empty(target, ec));
    if (exists && !replace) {
        return R::fail(target.string() +
                       " already exists. Set import_overwrite = 1 to replace it (this also resets tables/)");
    }

    const std::string stem = target.filename().string();
    const fs::path parent = target.parent_path();
    const std::string pid = process_id();
    Scratch scratch;
    scratch.staging = parent / (stem + ".tmp-" + pid);
    scratch.alias = parent / (stem + ".src-" + pid + ".bin");
    const fs::path old_dir = parent / (stem + ".old-" + pid);

    fs::create_directories(parent, ec);
    if (ec) {
        return R::fail("cannot create " + parent.string() + ": " + ec.message());
    }
    fs::remove_all(scratch.staging, ec);
    const fs::path raw_dir = scratch.staging / "raw";
    const fs::path convert_dir = scratch.staging / "convert";
    fs::create_directories(raw_dir, ec);
    if (ec) {
        return R::fail("cannot create " + raw_dir.string() + ": " + ec.message());
    }

    if (is_cancelled(request.cancel)) {
        return R::fail("cancelled");
    }
    const unsigned threads = request.threads != 0 ? request.threads : std::max(1u, std::thread::hardware_concurrency());
    Progress progress(progress_fn, !converters.empty(), request.verify);

    // ---- read
    progress.report(Stage::Read, 0);
    auto image = open_image(request.image, scratch.alias);
    if (!image) {
        return R::fail(image.error());
    }
    auto opened = open_game(image.value(), request.cancel, [&](double f) { progress.report(Stage::Read, f); });
    if (!opened) {
        return R::fail(opened.error());
    }
    const GameDisc& game = opened.value();
    report.image_sha256 = game.image_sha256;

    std::vector<Item> items = build_items(game);
    std::uint64_t stored_total = 0;
    for (const Item& item : items) {
        stored_total += item.stored;
    }

    // ---- raw
    progress.report(Stage::Raw, 0);
    {
        std::atomic<std::uint64_t> bytes_done{0};
        std::vector<std::string> errors(items.size());
        run_jobs(
            items.size(), threads,
            [&](std::size_t i) {
                const Result<void> done = extract_item(items[i], raw_dir / fs::path(items[i].path), bytes_done, request.cancel);
                if (!done) {
                    errors[i] = done.error();
                }
                return static_cast<bool>(done);
            },
            [&] { progress.report(Stage::Raw, stored_total ? static_cast<double>(bytes_done.load()) / static_cast<double>(stored_total) : 1.0); });
        if (is_cancelled(request.cancel)) {
            return R::fail("cancelled");
        }
        for (const std::string& error : errors) {
            if (!error.empty()) {
                return R::fail(error);
            }
        }
    }
    report.raw_files = items.size();
    report.raw_bytes = stored_total;

    // ---- convert
    std::vector<ItemResult> results(items.size());
    if (!converters.empty()) {
        progress.report(Stage::Convert, 0);
        fs::create_directories(convert_dir, ec);
        ConvertContext ctx(converters, request.verify, raw_dir, convert_dir);
        ctx.cancel = request.cancel;
        std::uint64_t logical_total = 0;
        for (const Item& item : items) {
            logical_total += item.logical;
        }
        run_jobs(
            items.size(), threads,
            [&](std::size_t i) {
                convert_item(items[i], ctx, results[i]);
                return results[i].fatal.empty();
            },
            [&] { progress.report(Stage::Convert, logical_total ? static_cast<double>(ctx.bytes_done.load()) / static_cast<double>(logical_total) : 1.0); });

        if (is_cancelled(request.cancel)) {
            return R::fail("cancelled");
        }
        report.converters.resize(converters.size());
        for (std::size_t ci = 0; ci < converters.size(); ++ci) {
            report.converters[ci].name = std::string(converters[ci]->name());
            report.converters[ci].lossless = converters[ci]->lossless();
        }
        for (const ItemResult& r : results) {
            for (std::size_t ci = 0; ci < r.counts.size(); ++ci) {
                report.converters[ci].accepted += r.counts[ci].accepted;
                report.converters[ci].outputs += r.counts[ci].outputs;
                report.converters[ci].round_trips += r.counts[ci].round_trips;
            }
            for (const ConvertedFile& f : r.files) {
                ++report.converted_files;
                report.converted_bytes += f.size;
            }
            report.failures.insert(report.failures.end(), r.failures.begin(), r.failures.end());
        }
        for (const ItemResult& r : results) {
            if (!r.fatal.empty()) {
                return R::fail(r.fatal);
            }
        }
        if (!report.failures.empty()) {
            std::string message = std::to_string(report.failures.size()) + " conversion error(s):";
            for (std::size_t i = 0; i < report.failures.size() && i < kFailuresInMessage; ++i) {
                const ConversionFailure& f = report.failures[i];
                message += "\n  " + f.path + " [" + f.converter + "]: " + f.message;
            }
            if (report.failures.size() > kFailuresInMessage) {
                message += "\n  ... and " + std::to_string(report.failures.size() - kFailuresInMessage) + " more";
            }
            return R::fail(message);
        }
    }

    // ---- index.tsv
    std::vector<IndexRow> rows;
    rows.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        IndexRow row;
        row.path = items[i].path;
        row.size = items[i].logical;
        row.crc32 = items[i].crc;
        row.source = items[i].source;
        row.format = items[i].format;
        for (const ConvertedFile& f : results[i].files) {
            row.converted.push_back(f.path);
        }
        rows.push_back(std::move(row));
    }
    const std::string index_text = format_index(rows);
    const fs::path index_path = scratch.staging / "index.tsv";
    {
        const Result<void> written = write_file(
            index_path, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(index_text.data()), index_text.size()));
        if (!written) {
            return written;
        }
    }

    // ---- verify
    if (request.verify) {
        progress.report(Stage::Verify, 0);
        std::ifstream in(index_path, std::ios::binary);
        const std::string on_disk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto parsed = parse_index(on_disk);
        if (!parsed) {
            return R::fail("verify: " + parsed.error());
        }
        if (parsed.value().size() != rows.size()) {
            return R::fail("verify: index.tsv has " + std::to_string(parsed.value().size()) + " rows, expected " +
                           std::to_string(rows.size()));
        }

        std::uint64_t total = 0;
        for (const Item& item : items) {
            total += item.stored;
        }
        std::atomic<std::uint64_t> bytes_done{0};
        std::vector<std::string> errors(items.size());
        run_jobs(
            items.size(), threads,
            [&](std::size_t i) {
                const IndexRow& row = parsed.value()[i];
                if (row.path != rows[i].path || row.size != rows[i].size || row.crc32 != rows[i].crc32 ||
                    row.converted != rows[i].converted) {
                    errors[i] = "index.tsv row " + std::to_string(i + 1) + " (" + row.path + ") does not match the import";
                    return false;
                }
                const Result<void> raw_ok = check_file_crc(raw_dir / fs::path(row.path), items[i].stored, row.crc32, bytes_done,
                                                           request.cancel);
                if (!raw_ok) {
                    errors[i] = raw_ok.error();
                    return false;
                }
                for (std::size_t k = 0; k < row.converted.size(); ++k) {
                    std::error_code size_ec;
                    const std::uintmax_t size = fs::file_size(convert_dir / fs::path(row.converted[k]), size_ec);
                    if (size_ec || size != results[i].files[k].size) {
                        errors[i] = "converted file " + row.converted[k] + " is missing or has the wrong size";
                        return false;
                    }
                }
                return true;
            },
            [&] { progress.report(Stage::Verify, total ? static_cast<double>(bytes_done.load()) / static_cast<double>(total) : 1.0); });
        if (is_cancelled(request.cancel)) {
            return R::fail("cancelled");
        }
        for (const std::string& error : errors) {
            if (!error.empty()) {
                return R::fail("verify: " + error);
            }
        }
        report.crc_checked = items.size();
    }

    // ---- commit
    if (is_cancelled(request.cancel)) {
        return R::fail("cancelled");
    }
    progress.report(Stage::Commit, 0);
    const std::string cfg = version_cfg(request.image.filename().string(), game.image_sha256, request.verify, converters);
    {
        const Result<void> written = write_file(
            scratch.staging / "version.cfg", std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(cfg.data()), cfg.size()));
        if (!written) {
            return written;
        }
    }
    progress.report(Stage::Commit, 0.5);

    if (replace) {
        fs::remove_all(old_dir, ec);
        fs::rename(target, old_dir, ec);
        if (ec) {
            return R::fail("cannot move the existing " + target.string() + " aside: " + ec.message());
        }
    }
    fs::rename(scratch.staging, target, ec);
    if (ec) {
        const std::string message = ec.message();
        if (replace) {
            fs::rename(old_dir, target, ec);
        }
        return R::fail("cannot rename " + scratch.staging.string() + " to " + target.string() + ": " + message);
    }
    scratch.keep_staging = true;
    if (replace) {
        fs::remove_all(old_dir, ec);
    }
    progress.report(Stage::Commit, 1);
    return R::ok();
}

}  // namespace

int importer_version() { return kImporterVersion; }

Result<void> import_image(const Request& request, const std::vector<std::unique_ptr<convert::Converter>>& converters,
                          const ProgressFn& progress) {
    Report local;
    Report& report = request.report ? *request.report : local;
    report = Report{};
    try {
        return run_import(request, converters, progress, report);
    } catch (const std::exception& e) {
        return Result<void>::fail(std::string("import failed: ") + e.what());
    }
}

}  // namespace sh::import
