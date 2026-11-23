// sh-upscale: upscales the imported textures (gamedata/convert/textures/**/*.png) with Real-ESRGAN on ncnn's Vulkan
// compute into the cache the engine reads when texture_upscale is on (docs/RENDER.md section 12). The
// game starts it in the background; a run without a Vulkan device, or one that fails, writes nothing and the
// originals stay in use.
//
//   sh-upscale --gamedata <dir> [--scale 2|4] [--model <name>] [--models <dir>] [--limit <n>] [--only <text>]
//              [--gpu <n>] [--tile <n>] [--log <file>] [--lettering <file>] [--skip <file>]
//
// The model runs at x4: x2 is its result averaged down; no side passes kMaxSide (upscale_cache.h). Lettering
// (--lettering, by default gamedata/config/texture_upscale.json; the game passes the copy with its mods' patches)
// is scaled with a Lanczos filter instead: the whole picture, or its rectangles over the network's result. The keys
// in the --skip file (one per line: the pictures active mods replace, which the game draws as the mod made them)
// are left out. The pictures the game names in the wanted file go first; the file is read again whenever it
// changes. Every picture is written the moment it is done and the game draws it from then on; a full run writes
// "<done> <total>" to the progress file and the complete file at the end, and removes the caches of scales no longer
// made (x8, x16); --only and --limit runs (for checks) do neither. Pictures go to disk one at a time; nothing is
// kept in memory.
//
// Exit codes: 0 done, 2 usage, 3 no Vulkan device, no model or a picture failed, 4 another run holds the cache.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "engine/assets/upscale_cache.h"
#include "formats/png/sh_png.h"
#include "gpu.h"
#include "net.h"
#include "sh-upscale/upscaler.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

struct Args {
    fs::path gamedata;
    fs::path models;
    int scale = 2;
    std::string model{sh::upscale::kModel};
    int limit = 0;
    int gpu = -1;
    int tile = 0;
    std::string only;
    fs::path log;
    fs::path lettering;
    fs::path skip;
};

std::optional<std::vector<std::uint8_t>> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool write_file_atomic(const fs::path& p, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    // The engine may open the file at any moment: it sees the whole picture or none.
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    if (ec) fs::remove(tmp, ec);
    return !ec;
}

fs::path exe_dir(const char* argv0) {
#ifdef _WIN32
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n > 0 && n < 32768) return fs::path(buf).parent_path();
#else
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path();
#endif
    return fs::absolute(argv0).parent_path();
}

// One run at a time per cache; the lock goes with the process.
bool take_lock(const fs::path& file) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
#ifdef _WIN32
    const HANDLE h = CreateFileW(file.wstring().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    return h != INVALID_HANDLE_VALUE;
#else
    const int fd = open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0;
#endif
}

void lower_priority() {
#ifdef _WIN32
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#else
    (void)!nice(10);
#endif
}

std::optional<Args> parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        const bool has_value = i + 1 < argc;
        if (k == "--gamedata" && has_value) {
            a.gamedata = argv[++i];
        } else if (k == "--models" && has_value) {
            a.models = argv[++i];
        } else if (k == "--model" && has_value) {
            a.model = argv[++i];
        } else if (k == "--scale" && has_value) {
            a.scale = std::atoi(argv[++i]);
        } else if (k == "--limit" && has_value) {
            a.limit = std::atoi(argv[++i]);
        } else if (k == "--gpu" && has_value) {
            a.gpu = std::atoi(argv[++i]);
        } else if (k == "--tile" && has_value) {
            a.tile = std::atoi(argv[++i]);
        } else if (k == "--log" && has_value) {
            a.log = argv[++i];
        } else if (k == "--only" && has_value) {
            a.only = argv[++i];
        } else if (k == "--lettering" && has_value) {
            a.lettering = argv[++i];
        } else if (k == "--skip" && has_value) {
            a.skip = argv[++i];
        } else {
            return std::nullopt;
        }
    }
    if (a.gamedata.empty() || !sh::upscale::valid_scale(a.scale)) return std::nullopt;
    if (a.models.empty()) a.models = exe_dir(argv[0]) / "upscale-models";
    if (a.lettering.empty()) a.lettering = a.gamedata / "config" / "texture_upscale.json";
    return a;
}

// Old entries of a picture (another source hash) go when the new one is written.
void drop_stale(const fs::path& keep) {
    const std::string stem = keep.filename().string();
    const std::size_t cut = stem.size() - (16 + 4);  // ".<hash>.png" keeps the name before it
    const std::string prefix = stem.substr(0, cut);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(keep.parent_path(), ec)) {
        const std::string n = e.path().filename().string();
        if (n != stem && n.size() == stem.size() && n.compare(0, prefix.size(), prefix) == 0) fs::remove(e.path(), ec);
    }
}

// The wanted file of the game (upscale_cache.h): the rank of a key, read again whenever the file changes.
class Wanted {
public:
    explicit Wanted(fs::path file) : file_(std::move(file)) {}

    void refresh() {
        std::error_code ec;
        const auto time = fs::last_write_time(file_, ec);
        const std::uintmax_t size = ec ? 0 : fs::file_size(file_, ec);
        if (!ec && seen_ && time == time_ && size == size_) return;
        seen_ = !ec;
        time_ = time;
        size_ = size;
        rank_.clear();
        std::ifstream f(file_);
        std::string line;
        for (int n = 0; std::getline(f, line); ++n)
            if (!line.empty()) rank_.emplace(line, n);
    }
    int rank(const std::string& key) const {
        const auto it = rank_.find(key);
        return it == rank_.end() ? INT_MAX : it->second;
    }

private:
    fs::path file_;
    bool seen_ = false;
    fs::file_time_type time_{};
    std::uintmax_t size_ = 0;
    std::unordered_map<std::string, int> rank_;
};

// The lettering each picture of a model directory was made with (its Lettering::signature(), none for the network
// alone): a picture whose entry changed since is made again.
class Ledger {
public:
    explicit Ledger(fs::path file) : file_(std::move(file)) {
        std::ifstream f(file_);
        std::string line;
        while (std::getline(f, line)) {
            const std::size_t tab = line.find('\t');
            if (tab != std::string::npos) made_[line.substr(0, tab)] = line.substr(tab + 1);
        }
    }
    bool matches(const std::string& key, const std::string& signature) const {
        const auto it = made_.find(key);
        return (it == made_.end() ? std::string() : it->second) == signature;
    }
    void note(const std::string& key, const std::string& signature) {
        if (matches(key, signature)) return;
        if (signature.empty())
            made_.erase(key);
        else
            made_[key] = signature;
        std::string text;
        for (const auto& [k, v] : made_) text += k + "\t" + v + "\n";
        write_file_atomic(file_, std::vector<std::uint8_t>(text.begin(), text.end()));
    }

private:
    fs::path file_;
    std::map<std::string, std::string> made_;
};

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// The model directories of scales no longer made (x8, x16 of earlier versions): gigabytes nothing reads.
void drop_old_scales(const fs::path& root, const std::string& model) {
    std::error_code ec;
    const std::string prefix = model + "-x";
    for (const auto& e : fs::directory_iterator(root, ec)) {
        const std::string n = e.path().filename().string();
        if (!e.is_directory(ec) || n.compare(0, prefix.size(), prefix) != 0) continue;
        const int scale = std::atoi(n.c_str() + prefix.size());
        if (!sh::upscale::valid_scale(scale)) {
            std::printf("upscale: removing %s (a scale no longer made)\n", n.c_str());
            fs::remove_all(e.path(), ec);
        }
    }
}

std::optional<sh::upscale::Picture> read_picture(const fs::path& file) {
    const std::optional<std::vector<std::uint8_t>> bytes = read_file(file);
    if (!bytes) return std::nullopt;
    auto rgba = sh::png::decode_rgba8(*bytes);
    if (!rgba) return std::nullopt;
    return sh::upscale::Picture{static_cast<int>(rgba.value().width), static_cast<int>(rgba.value().height),
                                std::move(rgba.value().pixels)};
}

bool write_picture(const fs::path& file, sh::upscale::Picture&& p) {
    sh::png::Image im;
    im.width = static_cast<std::uint32_t>(p.width);
    im.height = static_cast<std::uint32_t>(p.height);
    im.color_type = sh::png::ColorType::Rgba;
    im.pixels = std::move(p.rgba);
    // A cache file is written once and read at map loads: speed over size.
    auto png = sh::png::encode(im, 2);
    if (!png || !write_file_atomic(file, png.value())) return false;
    drop_stale(file);
    return true;
}

// Tiles as large as the device's memory takes: the context around each tile is work done twice, so fewer tiles are
// faster. Real-ESRGAN's own sizes for small cards, larger ones above.
int tile_for(int gpu) {
    const ncnn::VulkanDevice* device = ncnn::get_gpu_device(gpu);
    const std::uint32_t budget = device != nullptr ? device->get_heap_budget() : 0;
    if (budget > 6000) return 400;
    if (budget > 3000) return 256;
    if (budget > 1900) return 200;
    if (budget > 550) return 100;
    if (budget > 190) return 64;
    return 32;
}

}  // namespace

int main(int argc, char** argv) {
    const std::optional<Args> args = parse(argc, argv);
    if (!args) {
        std::fprintf(stderr,
                     "usage: sh-upscale --gamedata <dir> [--scale 2|4|8|16] [--model <name>] [--models <dir>] "
                     "[--limit <n>] [--only <text>] [--gpu <n>] [--tile <n>] [--log <file>]\n");
        return 2;
    }
    const fs::path textures = args->gamedata / "convert" / "textures";
    const fs::path root = sh::upscale::cache_root(args->gamedata);
    if (!take_lock(root / ".lock")) {
        std::fprintf(stderr, "upscale: another run is working on %s\n", root.string().c_str());
        return 4;
    }
    lower_priority();
    if (!args->log.empty()) {
        std::error_code dir_ec;
        fs::create_directories(args->log.parent_path(), dir_ec);
        // Both streams in the one file the game names in its log.
        if (std::freopen(args->log.string().c_str(), "w", stdout) != nullptr) {
#ifdef _WIN32
            _dup2(_fileno(stdout), _fileno(stderr));
#else
            dup2(fileno(stdout), fileno(stderr));
#endif
        }
    }
    const bool full = args->only.empty() && args->limit == 0;
    if (full) drop_old_scales(root, args->model);

    sh::upscale::LetteringList lettering;
    if (const std::optional<std::vector<std::uint8_t>> text = read_file(args->lettering)) {
        std::string error;
        std::optional<sh::upscale::LetteringList> list = sh::upscale::parse_lettering(
            std::string_view(reinterpret_cast<const char*>(text->data()), text->size()), error);
        if (!list) {
            // Without the list lettering would go through the network: better nothing than garbled signs.
            std::fprintf(stderr, "upscale: %s: %s\n", args->lettering.string().c_str(), error.c_str());
            return 2;
        }
        lettering = std::move(*list);
    }
    std::set<std::string> skip;
    if (!args->skip.empty()) {
        std::ifstream f(args->skip);
        for (std::string line; std::getline(f, line);)
            if (!line.empty()) skip.insert(lower(line));
    }

    // The list first: a run with nothing to do never starts the GPU.
    struct Job {
        fs::path source;
        std::string key;
        std::string signature;  // of its lettering, empty without
        fs::path target;
        bool finished = false;
        bool failed = false;
        bool in_flight = false;  // its picture is being written
    };
    const fs::path dir = sh::upscale::model_dir(root, args->model, args->scale);
    Ledger ledger(dir / sh::upscale::kLetteringFile);
    std::vector<Job> jobs;
    std::size_t cached = 0, skipped = 0, replaced = 0, pictures = 0;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(textures, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        const std::string rel = fs::relative(it->path(), args->gamedata, ec).generic_string();
        const std::optional<std::string> key = sh::upscale::texture_key(rel);
        if (!key) {
            if (it->path().extension() == ".png") ++skipped;
            continue;
        }
        if (skip.count(lower(*key)) != 0) {
            ++replaced;
            continue;
        }
        if (!args->only.empty() && key->find(args->only) == std::string::npos) continue;
        const std::optional<std::vector<std::uint8_t>> bytes = read_file(it->path());
        if (!bytes) continue;
        ++pictures;
        const std::uint64_t hash = sh::upscale::source_hash(*bytes);
        Job job;
        job.source = it->path();
        job.key = *key;
        if (const sh::upscale::Lettering* l = sh::upscale::lettering_for(lettering, *key))
            job.signature = l->signature();
        job.target = sh::upscale::cache_file(root, args->model, *key, args->scale, hash);
        if (fs::exists(job.target, ec)) {
            if (ledger.matches(job.key, job.signature)) {
                ++cached;
                continue;
            }
            // Made with other lettering: the game draws the original until the new picture is there.
            fs::remove(job.target, ec);
        }
        // Only the path is kept: the picture is read again when its turn comes, so memory holds one at a time.
        jobs.push_back(std::move(job));
        if (args->limit > 0 && static_cast<int>(jobs.size()) >= args->limit) break;
    }
    std::printf(
        "upscale: %zu to do, %zu cached, %zu skipped (fonts, masks), %zu replaced by mods, %zu with lettering, "
        "%s x%d\n",
        jobs.size(), cached, skipped, replaced, lettering.size(), args->model.c_str(), args->scale);
    const auto write_text = [](const fs::path& file, const std::string& text) {
        const std::vector<std::uint8_t> bytes(text.begin(), text.end());
        write_file_atomic(file, bytes);
    };
    const auto counts = [](std::size_t done, std::size_t total) {
        return std::to_string(done) + " " + std::to_string(total);
    };
    if (jobs.empty()) {
        if (full) write_text(dir / sh::upscale::kCompleteFile, "");
        return 0;
    }
    if (full) {
        fs::remove(dir / sh::upscale::kCompleteFile, ec);
        write_text(dir / sh::upscale::kProgressFile, counts(cached, pictures));
    }

    fs::path param = args->models / (args->model + ".param");
    fs::path weights = args->models / (args->model + ".bin");
    if (!fs::exists(param, ec)) {
        param = args->models / (args->model + "-x4.param");
        weights = args->models / (args->model + "-x4.bin");
    }
    if (!fs::exists(param, ec) || !fs::exists(weights, ec)) {
        std::fprintf(stderr, "upscale: no x4 model %s in %s\n", args->model.c_str(), args->models.string().c_str());
        return 3;
    }
    if (ncnn::create_gpu_instance() != 0 || ncnn::get_gpu_count() == 0) {
        std::fprintf(stderr, "upscale: no Vulkan device; the original textures stay\n");
        ncnn::destroy_gpu_instance();
        return 3;
    }
    const int gpu = args->gpu >= 0 && args->gpu < ncnn::get_gpu_count() ? args->gpu : ncnn::get_default_gpu_index();
    const int tile = args->tile > 0 ? args->tile : tile_for(gpu);
    int status = 0;
    {
        ncnn::Net net;
        net.opt.use_vulkan_compute = true;
        net.opt.use_fp16_packed = true;
        net.opt.use_fp16_storage = true;
        net.opt.use_fp16_arithmetic = false;
        net.set_vulkan_device(gpu);
        if (net.load_param(param.string().c_str()) != 0 || net.load_model(weights.string().c_str()) != 0) {
            std::fprintf(stderr, "upscale: cannot load %s\n", param.string().c_str());
            status = 3;
        } else {
            std::printf("upscale: %s on %s, tiles of %d\n", param.filename().string().c_str(),
                        ncnn::get_gpu_info(gpu).device_name(), tile);
            constexpr int kNet = sh::upscale::kNetScale;
            const sh::upscale::TileInfer infer = [&](const std::vector<std::uint8_t>& rgb, int w, int h,
                                                     std::vector<std::uint8_t>& out) {
                ncnn::Mat in = ncnn::Mat::from_pixels(rgb.data(), ncnn::Mat::PIXEL_RGB, w, h);
                const float norm[3] = {1 / 255.f, 1 / 255.f, 1 / 255.f};
                in.substract_mean_normalize(nullptr, norm);
                ncnn::Extractor ex = net.create_extractor();
                ncnn::Mat res;
                if (ex.input("data", in) != 0 || ex.extract("output", res) != 0) return false;
                if (res.w != w * kNet || res.h != h * kNet || res.c != 3) return false;
                const float denorm[3] = {255.f, 255.f, 255.f};
                res.substract_mean_normalize(nullptr, denorm);
                out.resize(static_cast<std::size_t>(res.w) * res.h * 3);
                res.to_pixels(out.data(), ncnn::Mat::PIXEL_RGB);
                return true;
            };
            // One run of the network, with 10 texels of context around each tile (Real-ESRGAN's prepadding).
            const auto network = [&](const sh::upscale::Picture& in, sh::upscale::Picture& out) {
                return sh::upscale::upscale_tiled(in, kNet, tile, 10, infer, out);
            };
            // The source at `scale` (at most x4): one run, averaged down below x4. Lettering is scaled with the
            // filter: the whole picture, or its rectangles over the network's result.
            const auto make = [&](sh::upscale::Picture src, int scale, const sh::upscale::Lettering* letters,
                                  sh::upscale::Picture& out) {
                if (scale <= 1) {
                    out = std::move(src);
                    return true;
                }
                sh::upscale::bleed_colour(src);
                if (letters != nullptr && letters->rects.empty()) {
                    sh::upscale::scale_lanczos(src, scale, out);
                    return true;
                }
                if (!network(src, out)) return false;
                if (scale < kNet) sh::upscale::shrink_colour(out, kNet / scale);
                if (letters != nullptr) {
                    sh::upscale::Picture sharp;
                    sh::upscale::scale_lanczos(src, scale, sharp);
                    sh::upscale::paste_rects(sharp, letters->rects, scale, out);
                }
                return true;
            };

            std::size_t steps = 0, failed = 0, finished = cached;
            using Clock = std::chrono::steady_clock;
            Clock::duration t_network{}, t_write{};
            const Clock::time_point t_start = Clock::now();
            Wanted wanted(root / sh::upscale::kWantedFile);
            const auto done_with = [&](std::size_t index, bool ok) {
                Job& job = jobs[index];
                job.in_flight = false;
                ++steps;
                if (!ok) {
                    ++failed;
                    job.failed = true;
                    std::fprintf(stderr, "upscale: %s failed\n", job.source.string().c_str());
                } else {
                    job.finished = true;
                    ++finished;
                    ledger.note(job.key, job.signature);
                    if (full) write_text(dir / sh::upscale::kProgressFile, counts(finished, pictures));
                }
                if (steps % 50 == 0) std::printf("upscale: %zu steps, %zu/%zu pictures\n", steps, finished, pictures);
                std::fflush(stdout);
            };
            struct Writing {
                std::size_t job = 0;
                std::future<bool> result;
            };
            std::optional<Writing> writing;
            const auto settle = [&] {
                if (!writing) return;
                done_with(writing->job, writing->result.get());
                writing.reset();
            };
            for (;;) {
                // Next: what the game waits for, then file order.
                wanted.refresh();
                std::size_t pick = jobs.size();
                std::pair<int, std::size_t> best{};
                for (std::size_t i = 0; i < jobs.size(); ++i) {
                    const Job& j = jobs[i];
                    if (j.finished || j.failed || j.in_flight) continue;
                    const int rank = wanted.rank(j.key);
                    const std::pair<int, std::size_t> order{rank == INT_MAX ? 1 : 0,
                                                            rank == INT_MAX ? i : static_cast<std::size_t>(rank)};
                    if (pick == jobs.size() || order < best) {
                        best = order;
                        pick = i;
                    }
                }
                if (pick == jobs.size() && writing) {
                    settle();
                    continue;
                }
                if (pick == jobs.size()) break;
                Job& job = jobs[pick];
                const Clock::time_point t0 = Clock::now();
                std::optional<sh::upscale::Picture> src = read_picture(job.source);
                bool ok = false;
                if (src) {
                    const int scale = sh::upscale::fitted_scale(src->width, src->height, args->scale);
                    sh::upscale::Picture dst;
                    ok = make(*src, scale, sh::upscale::lettering_for(lettering, job.key), dst);
                    t_network += Clock::now() - t0;
                    if (ok) {
                        sh::upscale::scale_alpha_nearest(*src, scale, dst);
                        // The PNG is written while the network runs the next picture.
                        settle();
                        job.in_flight = true;
                        writing = Writing{pick, std::async(std::launch::async, [out_file = job.target, &t_write,
                                                                                pic = std::move(dst)]() mutable {
                                              const Clock::time_point t1 = Clock::now();
                                              const bool written = write_picture(out_file, std::move(pic));
                                              t_write += Clock::now() - t1;
                                              return written;
                                          })};
                        continue;
                    }
                }
                settle();
                done_with(pick, false);
            }
            settle();
            const auto secs = [](Clock::duration d) { return std::chrono::duration<double>(d).count(); };
            std::printf("upscale: %zu steps in %.1f s (network %.1f s, PNG %.1f s), %zu failed\n", steps,
                        secs(Clock::now() - t_start), secs(t_network), secs(t_write), failed);
            if (failed > 0) status = 3;
            if (full && failed == 0) write_text(dir / sh::upscale::kCompleteFile, "");
        }
    }
    ncnn::destroy_gpu_instance();
    return status;
}
