// The scene's process-wide state: the frame the game is describing, the cells of the area files and the textures,
// and the C face of scene_c.h.

#include "render/scene/area_cells.h"
#include "render/scene/scene.h"
#include "render/scene/scene_c.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <set>
#include <thread>
#include <unordered_set>
#include <utility>

namespace sh::scene {
namespace {

// Maps whose files stay resident after they are left (begin_map): the one just left and the one before it, so a
// walk back through the door finds its cells, models and textures in memory.
constexpr std::uint32_t kMapsKept = 2;

// A loaded area file: its cells' stems and the map (Store::map) that last drew one of them.
struct AreaEntry {
    std::vector<std::string> stems;
    std::uint32_t used = 0;
};
struct TextureEntry {
    std::shared_ptr<const SceneTexture> texture;
    std::uint32_t used = 0;
};
struct ModelFile {
    std::map<std::string, std::shared_ptr<const LibraryModel>> models;  // by name
    std::uint32_t used = 0;
};

struct Store {
    Frame frame;
    FileReader reader;
    MipmapQuery mipmap_query;
    ModFileQuery mod_file_query;
    LogFn log;
    std::map<std::string, std::shared_ptr<const CellMesh>> cells;   // by stem
    std::map<std::string, AreaEntry> areas;                         // read area files, by area
    std::set<std::string> areas_tried;
    std::set<std::string> missing_logged;
    std::map<std::pair<std::string, int>, TextureEntry> textures;
    std::map<std::uint64_t, std::shared_ptr<const SceneTexture>> prelit_textures;
    WaterSurface water;
    float water_phase = 0.0f;
    // Library models by file, then by name (both "<ROOT>/<MODEL>" and "<ROOT>/#<index>"); a file read once.
    std::map<std::string, ModelFile, std::less<>> models;
    std::uint32_t generation = 0;
    std::uint32_t map = 0;       // counts begin_map(): the stamp of what the current map uses
    std::uint32_t released = 0;  // counts begin_map() calls that released files
    std::vector<CellRef> placed_cells;
    std::vector<ObjectRef> placed_objects;
    RefreshedTextures refreshed;
};

Store& store() {
    static Store* s = new Store;  // never destroyed: renderers may outlive static destructors at exit
    return *s;
}

std::string upper(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

// "ER0402" -> "ER": the cell stem without its four grid digits.
std::string area_of(std::string_view stem) {
    return stem.size() > 4 ? std::string(stem.substr(0, stem.size() - 4)) : std::string(stem);
}

void load_area(const std::string& area) {
    Store& s = store();
    if (!s.areas_tried.insert(area).second) return;
    const std::string path = "maps/areas/" + area + ".glb";
    std::optional<std::vector<std::uint8_t>> bytes;
    if (s.reader) bytes = s.reader(path);
    if (!bytes) {
        log(1, "[SCENE] " + path + ": not found; its cells stay undrawn");
        return;
    }
    auto cells = read_area_cells(*bytes, path);
    if (!cells) {
        log(1, "[SCENE] " + cells.error());
        return;
    }
    std::size_t vertices = 0;
    AreaEntry& entry = s.areas[area];
    entry.used = s.map;
    const bool from_mod = s.mod_file_query && s.mod_file_query(path);
    for (CellMesh& c : cells.value()) {
        vertices += c.vertices.size();
        c.from_mod = from_mod;
        const std::string stem = c.stem;
        s.cells[stem] = std::make_shared<const CellMesh>(std::move(c));
        entry.stems.push_back(stem);
    }
    log(0, "[SCENE] " + path + ": " + std::to_string(cells.value().size()) + " cells, " + std::to_string(vertices) +
               " vertices");
}

}  // namespace

Frame& current_frame() { return store().frame; }

ViewerOverrides& viewer_overrides() {
    static ViewerOverrides overrides;
    return overrides;
}

const std::vector<CellRef>& placed_cells() { return store().placed_cells; }
const std::vector<ObjectRef>& placed_objects() { return store().placed_objects; }

void set_file_reader(FileReader reader) { store().reader = std::move(reader); }

void set_mipmap_query(MipmapQuery query) { store().mipmap_query = std::move(query); }

void set_mod_file_query(ModFileQuery query) { store().mod_file_query = std::move(query); }

void set_log(LogFn log) { store().log = std::move(log); }

std::optional<std::vector<std::uint8_t>> read_scene_file(std::string_view relative) {
    Store& s = store();
    if (!s.reader) return std::nullopt;
    return s.reader(relative);
}

void log(int level, const std::string& message) {
    if (store().log) store().log(level, message);
}

std::shared_ptr<const CellMesh> find_cell(std::string_view stem) {
    Store& s = store();
    const std::string key = upper(stem);
    if (auto it = s.cells.find(key); it != s.cells.end()) return it->second;
    load_area(area_of(key));
    if (auto it = s.cells.find(key); it != s.cells.end()) return it->second;
    if (s.missing_logged.insert(key).second) log(1, "[SCENE] cell " + key + " is in no area file; it is not drawn");
    return nullptr;
}

std::shared_ptr<const LibraryModel> find_model(std::string_view file, std::string_view name, bool log_missing) {
    Store& s = store();
    auto fit = s.models.find(file);
    if (fit == s.models.end()) {
        fit = s.models.emplace(std::string(file), ModelFile{}).first;
        std::optional<std::vector<std::uint8_t>> bytes;
        if (s.reader) bytes = s.reader(file);
        if (!bytes) {
            log(1, "[SCENE] " + std::string(file) + ": not found; its models stay undrawn");
        } else if (auto models = read_glb_models(*bytes, file); !models) {
            log(1, "[SCENE] " + models.error());
        } else {
            const bool from_mod = s.mod_file_query && s.mod_file_query(file);
            for (LibraryModel& m : models.value()) {
                m.mesh.from_mod = from_mod;
                auto shared = std::make_shared<const LibraryModel>(std::move(m));
                fit->second.models.emplace(shared->name, shared);
                if (!shared->index_name.empty()) fit->second.models.emplace(shared->index_name, shared);
            }
            log(0, "[SCENE] " + std::string(file) + ": " + std::to_string(models.value().size()) + " library models");
        }
    }
    fit->second.used = s.map;
    const std::string key = upper(name);
    if (auto it = fit->second.models.find(key); it != fit->second.models.end()) return it->second;
    if (log_missing && s.missing_logged.insert(std::string(file) + "#" + key).second)
        log(1, "[SCENE] model " + key + " is not in " + std::string(file) + "; it is not drawn");
    return nullptr;
}

void prefetch_textures(std::span<const std::pair<std::string, int>> wanted) {
    Store& s = store();
    if (!s.reader) return;
    struct Job {
        std::pair<std::string, int> key;
        std::optional<std::vector<std::uint8_t>> png, tim;
        bool mipmapped = false;
        std::optional<SceneTexture> out;
        std::string error;
    };
    std::vector<Job> jobs;
    std::set<std::pair<std::string, int>> seen;
    for (const auto& key : wanted) {
        if (s.textures.count(key) || !seen.insert(key).second) continue;
        Job j;
        j.key = key;
        // The reader and the mipmap query keep state of their own: they run here, only the decoding is spread.
        j.png = s.reader(key.first);
        if (!j.png) continue;
        j.mipmapped = s.mipmap_query && s.mipmap_query(key.first);
        if (const std::string tim_path = tim_json_path(key.first); !tim_path.empty()) j.tim = s.reader(tim_path);
        jobs.push_back(std::move(j));
    }
    if (jobs.size() < 2) return;
    std::atomic<std::size_t> next{0};
    auto work = [&] {
        for (std::size_t i; (i = next.fetch_add(1)) < jobs.size();) {
            Job& j = jobs[i];
            auto decoded = decode_scene_texture(
                *j.png, j.tim ? std::span<const std::uint8_t>(*j.tim) : std::span<const std::uint8_t>(), j.key.second);
            if (!decoded) {
                j.error = decoded.error();
                continue;
            }
            j.out = std::move(decoded.value());
            j.out->mipmapped = j.mipmapped;
            if (j.mipmapped) j.out->levels = mip_levels(*j.out);
        }
    };
    const std::size_t threads =
        std::min<std::size_t>({jobs.size(), std::max(1u, std::thread::hardware_concurrency()), std::size_t{16}});
    std::vector<std::thread> pool;
    for (std::size_t t = 1; t < threads; ++t) pool.emplace_back(work);
    work();
    for (std::thread& t : pool) t.join();
    for (Job& j : jobs) {
        std::shared_ptr<const SceneTexture> tex;
        if (j.out)
            tex = std::make_shared<const SceneTexture>(std::move(*j.out));
        else
            log(1, "[SCENE] " + j.key.first + ": " + j.error);
        s.textures.emplace(j.key, TextureEntry{tex, s.map});
    }
}

std::shared_ptr<const SceneTexture> find_texture(const std::string& path, int palette) {
    Store& s = store();
    const auto key = std::make_pair(path, palette);
    if (auto it = s.textures.find(key); it != s.textures.end()) {
        it->second.used = s.map;
        return it->second.texture;
    }
    std::shared_ptr<const SceneTexture> tex;
    std::optional<std::vector<std::uint8_t>> png;
    if (s.reader) png = s.reader(path);
    if (png) {
        std::optional<std::vector<std::uint8_t>> tim;
        const std::string tim_path = tim_json_path(path);
        if (!tim_path.empty() && s.reader) tim = s.reader(tim_path);
        auto decoded = decode_scene_texture(*png, tim ? std::span<const std::uint8_t>(*tim) : std::span<const std::uint8_t>(),
                                            palette);
        if (decoded) {
            decoded.value().mipmapped = s.mipmap_query && s.mipmap_query(path);
            tex = std::make_shared<const SceneTexture>(std::move(decoded.value()));
        } else {
            log(1, "[SCENE] " + path + ": " + decoded.error());
        }
    } else {
        log(1, "[SCENE] " + path + ": not found; drawn white");
    }
    s.textures.emplace(key, TextureEntry{tex, s.map});
    return tex;
}

void add_prelit_texture(std::uint64_t key, int width, int height, const std::uint8_t* rgba) {
    Store& s = store();
    if (key == 0 || rgba == nullptr || width <= 0 || height <= 0 || s.prelit_textures.count(key)) return;
    SceneTexture t;
    t.width = width;
    t.height = height;
    t.rgba.assign(rgba, rgba + static_cast<std::size_t>(width) * height * 4);
    s.prelit_textures.emplace(key, std::make_shared<const SceneTexture>(std::move(t)));
}

std::shared_ptr<const SceneTexture> prelit_texture(std::uint64_t key) {
    Store& s = store();
    auto it = s.prelit_textures.find(key);
    return it == s.prelit_textures.end() ? nullptr : it->second;
}

void refresh_textures(const std::function<bool(const std::string& path)>& match) {
    Store& s = store();
    for (auto it = s.textures.begin(); it != s.textures.end();) {
        if (!match(it->first.first)) {
            ++it;
            continue;
        }
        s.refreshed.textures.push_back(it->first);
        it = s.textures.erase(it);
    }
}

void refresh_prelit_texture(std::uint64_t key) {
    Store& s = store();
    if (s.prelit_textures.erase(key) > 0) s.refreshed.prelit.push_back(key);
}

RefreshedTextures take_refreshed_textures() { return std::exchange(store().refreshed, {}); }

void set_water_surface(WaterSurface surface) { store().water = std::move(surface); }
const WaterSurface& water_surface() { return store().water; }
void set_water_phase(float phase) { store().water_phase = phase; }
float water_phase() { return store().water_phase; }

bool water_surface_placed() {
    Store& s = store();
    if (s.water.material.empty()) return false;
    for (const CellRef& ref : s.placed_cells) {
        auto it = s.cells.find(ref.stem);
        if (it == s.cells.end() || !it->second) continue;
        for (const CellMaterial& m : it->second->materials)
            if (m.unpictured == s.water.material) return true;
    }
    return false;
}

void forget_assets() {
    Store& s = store();
    s.cells.clear();
    s.areas_tried.clear();
    s.missing_logged.clear();
    s.textures.clear();
    s.prelit_textures.clear();
    s.models.clear();
    s.areas.clear();
    s.placed_cells.clear();
    s.placed_objects.clear();
    s.refreshed = {};
    ++s.generation;
}

std::uint32_t asset_generation() { return store().generation; }

std::vector<LoadedTexture> loaded_textures() {
    std::vector<LoadedTexture> out;
    for (const auto& [key, entry] : store().textures) out.push_back({key.first, key.second, entry.texture});
    return out;
}

std::vector<LoadedModel> loaded_models() {
    std::vector<LoadedModel> out;
    for (const auto& [file, entry] : store().models)
        for (const auto& [name, model] : entry.models)
            if (model && name == model->name) out.push_back({file, model});
    return out;
}

std::vector<std::shared_ptr<const CellMesh>> loaded_cells() {
    std::vector<std::shared_ptr<const CellMesh>> out;
    for (const auto& [stem, cell] : store().cells)
        if (cell) out.push_back(cell);
    return out;
}

void begin_map() {
    Store& s = store();
    ++s.map;
    if (s.map <= kMapsKept) return;
    const std::uint32_t oldest_kept = s.map - kMapsKept;

    std::set<std::string> dropped_stems;
    std::set<std::string> kept_stems;
    for (auto it = s.areas.begin(); it != s.areas.end();) {
        if (it->second.used >= oldest_kept) {
            kept_stems.insert(it->second.stems.begin(), it->second.stems.end());
            ++it;
            continue;
        }
        dropped_stems.insert(it->second.stems.begin(), it->second.stems.end());
        s.areas_tried.erase(it->first);
        it = s.areas.erase(it);
    }
    std::size_t cells = 0;
    for (const std::string& stem : dropped_stems)
        if (!kept_stems.count(stem)) cells += s.cells.erase(stem);

    std::size_t files = 0;
    for (auto it = s.models.begin(); it != s.models.end();) {
        if (it->second.used >= oldest_kept) {
            ++it;
        } else {
            it = s.models.erase(it);
            ++files;
        }
    }

    // A texture stays while a resident cell or model draws with it: the renderers keep their GPU copy for as long
    // as the mesh, and the surface queries read its texels.
    std::set<std::pair<std::string, int>> drawn;
    auto note = [&drawn](const CellMesh& mesh) {
        for (const CellMaterial& m : mesh.materials)
            if (!m.texture.empty()) drawn.emplace(m.texture, m.palette);
    };
    for (const auto& [stem, mesh] : s.cells)
        if (mesh) note(*mesh);
    for (const auto& [file, entry] : s.models)
        for (const auto& [name, model] : entry.models)
            if (model) note(model->mesh);
    std::size_t textures = 0;
    for (auto it = s.textures.begin(); it != s.textures.end();) {
        if (it->second.used >= oldest_kept || drawn.count(it->first)) {
            ++it;
        } else {
            it = s.textures.erase(it);
            ++textures;
        }
    }

    if (cells + files + textures == 0) return;
    ++s.released;
    log(0, "[SCENE] released " + std::to_string(cells) + " cells, " + std::to_string(files) + " model files and " +
               std::to_string(textures) + " textures of maps left " + std::to_string(kMapsKept) + "+ maps ago");
}

std::uint32_t release_generation() { return store().released; }

std::unordered_set<const CellMesh*> resident_meshes() {
    Store& s = store();
    std::unordered_set<const CellMesh*> out;
    for (const auto& [stem, mesh] : s.cells)
        if (mesh) out.insert(mesh.get());
    for (const auto& [file, entry] : s.models)
        for (const auto& [name, model] : entry.models)
            if (model) out.insert(&model->mesh);
    return out;
}

}  // namespace sh::scene

using namespace sh::scene;

extern "C" void ShScene_SetCamera(const double rot[9], const double trans[3], float h, float ofsX, float ofsY) {
    Frame& f = current_frame();
    f.world = true;
    std::copy(rot, rot + 9, f.camera.rot.begin());
    std::copy(trans, trans + 3, f.camera.trans.begin());
    f.h = h;
    f.geom_ofs_x = ofsX;
    f.geom_ofs_y = ofsY;
}

extern "C" void ShScene_SetLighting(const ShSceneLighting* light) {
    if (light == nullptr) return;
    Lighting& l = current_frame().light;
    l.mode = light->mode;
    std::copy(light->tint, light->tint + 3, l.tint.begin());
    std::copy(light->farColor, light->farColor + 3, l.far_color.begin());
    l.ambient = light->ambient;
    l.max_intensity = light->maxIntensity;
    l.depth_cue = light->depthCue;
    l.fog = light->fog != 0;
    std::copy(light->lightVec, light->lightVec + 3, l.light_vec.begin());
    std::copy(light->lightPos, light->lightPos + 3, l.light_pos.begin());
}

extern "C" void ShScene_AddCell(const char* stem, int cellX, int cellZ) {
    if (stem == nullptr || *stem == '\0') return;
    current_frame().cells.push_back({upper(stem), cellX, cellZ});
}

extern "C" int ShScene_AddPrelit(const ShScenePrelit* mesh) {
    if (mesh == nullptr || mesh->vertexCount <= 0 || mesh->indexCount < 3) return -1;
    if (mesh->textureKey != 0) add_prelit_texture(mesh->textureKey, mesh->textureWidth, mesh->textureHeight, mesh->rgba);
    PrelitMesh m;
    m.texture_key = mesh->textureKey;
    m.blend = mesh->blend == 1 ? Blend::Additive : (mesh->blend == 2 ? Blend::Average : Blend::Opaque);
    m.fade = mesh->fade;
    m.casts_shadow = mesh->noShadow == 0;
    m.vertices.resize(static_cast<std::size_t>(mesh->vertexCount));
    for (int i = 0; i < mesh->vertexCount; ++i) {
        const ShScenePrelitVertex& s = mesh->vertices[i];
        m.vertices[static_cast<std::size_t>(i)] = {s.x, s.y, s.z, s.u, s.v, s.r, s.g, s.b, s.a};
    }
    m.indices.assign(mesh->indices, mesh->indices + mesh->indexCount);
    current_frame().prelit.push_back(std::move(m));
    return static_cast<int>(current_frame().prelit.size()) - 1;
}

extern "C" unsigned ShScene_AddObject(const char* file, const char* name, const double rot[9], const double trans[3],
                                      int* otGroup) {
    if (file == nullptr || name == nullptr || rot == nullptr || trans == nullptr) return 0;
    const std::shared_ptr<const LibraryModel> model = find_model(file, name);
    if (!model || model->mesh.indices.empty()) return 0;
    if (otGroup != nullptr) *otGroup = model->ot_group;
    Frame& f = current_frame();
    ObjectRef o;
    o.mesh = std::shared_ptr<const CellMesh>(model, &model->mesh);
    std::copy(rot, rot + 9, o.rot.begin());
    std::copy(trans, trans + 3, o.trans.begin());
    f.objects.push_back(std::move(o));
    return static_cast<unsigned>(kObjectItem) + static_cast<unsigned>(f.objects.size());
}

extern "C" void ShScene_FrameEnd(void) {
    Frame& f = current_frame();
    ++f.serial;
    if (!f.cells.empty()) {
        Store& s = store();
        s.placed_cells.swap(f.cells);
        s.placed_objects.swap(f.objects);
        // The areas this map draws stay resident through the next begin_map() calls (kMapsKept).
        for (const CellRef& ref : s.placed_cells)
            if (auto it = s.areas.find(area_of(ref.stem)); it != s.areas.end()) it->second.used = s.map;
    }
    f.world = false;
    f.water_glow = 0;
    f.lamp_glows.clear();
    f.flakes.clear();
    f.cells.clear();
    f.prelit.clear();
    f.objects.clear();
}
