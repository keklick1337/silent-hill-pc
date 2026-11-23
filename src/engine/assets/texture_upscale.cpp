#include "engine/assets/texture_upscale.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>

#include "engine/assets/upscale_cache.h"

namespace sh::upscale {

namespace {

namespace fs = std::filesystem;

// A picture find() was asked for: its source, the scale it was given (0: the original) and when it was asked last.
struct Asked {
    std::uint64_t hash = 0;
    int given = 0;
    std::uint64_t serial = 0;
};

std::atomic<int> g_scale{0};
std::mutex g_mutex;
fs::path g_root;
fs::path g_textures;                                    // <gamedata>/convert/textures
std::unordered_map<std::string, std::uint64_t> g_base;  // source hash of the imported picture of a key, 0: none
std::unordered_map<std::string, Asked> g_asked;
std::uint64_t g_serial = 0;
std::string g_progress_seen;  // the progress files at the last look for improvements
std::chrono::steady_clock::time_point g_next_look{};
bool g_backlog = false;  // the last look stopped at its limit
std::string g_wanted_written;

// The scales whose files serve `scale`, best first: itself, then the smaller ones.
std::vector<int> candidates(int scale) {
    std::vector<int> out;
    for (int s : {4, 2})
        if (s <= scale) out.push_back(s);
    return out;
}

std::string read_text(const fs::path& file) {
    std::ifstream f(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

void set_scale(int s) {
    s = valid_scale(s) ? s : 0;
    if (g_scale.exchange(s) == s) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    // The readers load everything again on a change of scale: what was given before says nothing now.
    g_asked.clear();
    g_progress_seen.clear();
}

int scale() { return g_scale.load(); }

void set_gamedata(const std::filesystem::path& gamedata) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_root = gamedata.empty() ? fs::path() : cache_root(gamedata);
    g_textures = gamedata.empty() ? fs::path() : gamedata / "convert" / "textures";
    g_base.clear();
}

std::optional<std::vector<std::uint8_t>> find(std::string_view path, std::span<const std::uint8_t> source) {
    const int s = g_scale.load();
    if (s == 0) return std::nullopt;
    const std::optional<std::string> key = texture_key(path);
    if (!key) return std::nullopt;
    const std::uint64_t hash = source_hash(source);
    fs::path root, textures;
    std::optional<std::uint64_t> base;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_root.empty()) return std::nullopt;
        root = g_root;
        textures = g_textures;
        if (const auto it = g_base.find(*key); it != g_base.end()) base = it->second;
    }
    if (!base) {
        std::ifstream f(textures / (*key + ".png"), std::ios::binary);
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        base = bytes.empty() ? 0 : source_hash(bytes);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_base[*key] = *base;
    }
    // Not the imported picture: a mod's (any size), drawn as the mod made it. The tool only upscales the imported
    // ones, so it is neither looked up nor named as wanted.
    if (hash != *base) return std::nullopt;
    std::optional<std::vector<std::uint8_t>> out;
    int given = 0;
    for (const int c : candidates(s)) {
        std::ifstream f(cache_file(root, kModel, *key, c, hash), std::ios::binary);
        if (!f) continue;
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (bytes.empty()) continue;
        out = std::move(bytes);
        given = c;
        break;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_asked[*key] = Asked{hash, given, ++g_serial};
    return out;
}

std::vector<std::string> improved(std::size_t limit) {
    std::vector<std::string> out;
    const int s = g_scale.load();
    if (s == 0 || limit == 0) return out;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_root.empty() || g_asked.empty()) return out;
    // The tool rewrites a progress file after every picture; a run for checks (--only) writes none, so a slow look
    // still comes now and then.
    std::string seen;
    for (const int c : candidates(s)) seen += read_text(model_dir(g_root, kModel, c) / kProgressFile) + ";";
    const auto now = std::chrono::steady_clock::now();
    if (seen == g_progress_seen && !g_backlog && now < g_next_look) return out;
    g_progress_seen = std::move(seen);
    g_next_look = now + std::chrono::seconds(5);
    std::vector<std::pair<std::uint64_t, const std::string*>> waiting;
    for (const auto& [key, a] : g_asked)
        if (a.given < s) waiting.emplace_back(a.serial, &key);
    std::sort(waiting.begin(), waiting.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    g_backlog = false;
    std::error_code ec;
    for (const auto& [serial, key] : waiting) {
        Asked& a = g_asked[*key];
        for (const int c : candidates(s)) {
            if (c <= a.given) break;
            if (!fs::exists(cache_file(g_root, kModel, *key, c, a.hash), ec)) continue;
            if (out.size() >= limit) {
                g_backlog = true;
                return out;
            }
            out.push_back(*key);
            a.given = c;
            break;
        }
    }
    return out;
}

void publish_wanted() {
    const int s = g_scale.load();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_root.empty() || s == 0) return;
    std::vector<std::pair<std::uint64_t, const std::string*>> waiting;
    for (const auto& [key, a] : g_asked)
        if (a.given < s) waiting.emplace_back(a.serial, &key);
    std::sort(waiting.begin(), waiting.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
    std::string text;
    for (const auto& [serial, key] : waiting) text += *key + "\n";
    if (text == g_wanted_written) return;
    std::error_code ec;
    fs::create_directories(g_root, ec);
    // Renamed into place: the tool reads either the old list or the new one.
    const fs::path file = g_root / kWantedFile;
    const fs::path tmp = file.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << text;
        if (!f) return;
    }
    fs::rename(tmp, file, ec);
    if (!ec) g_wanted_written = std::move(text);
}

Progress progress(int s) {
    Progress p;
    fs::path dir;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_root.empty() || !valid_scale(s)) return p;
        dir = model_dir(g_root, kModel, s);
    }
    std::error_code ec;
    p.complete = fs::exists(dir / kCompleteFile, ec);
    std::ifstream f(dir / kProgressFile);
    f >> p.done >> p.total;
    return p;
}

}  // namespace sh::upscale
