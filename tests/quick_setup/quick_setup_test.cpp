// The quick setup's decisions (src/engine/app/quick_setup/setup_model.h): what the hardware suggests, what it
// allows, the config lines the choices become, when the screen is shown, and its strings in every language.

#include "engine/app/quick_setup/setup_model.h"
#include "engine/app/quick_setup/setup_text.h"
#include "framework/sh_test.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace sh::quick_setup;

constexpr std::uint64_t kGiB = 1024ull * 1024ull * 1024ull;

// The table as render_preset.cpp has it, with the experimental presets another table may add.
std::vector<Preset> presets() {
    return {
        {"original", false}, {"enhanced", false},         {"high", false}, {"ultra", false}, {"ultra_upscaled", false},
        {"ultra_rtx", true}, {"ultra_rtx_upscaled", true}};
}

Hardware discrete(std::uint64_t vram, bool rt) {
    Hardware hw;
    hw.gl = true;
    hw.gl_renderer = "GeForce";
    hw.vulkan = true;
    hw.gpu_name = "GeForce";
    hw.kind = GpuKind::Discrete;
    hw.vram_bytes = vram;
    hw.ray_tracing = rt;
    return hw;
}

}  // namespace

SH_TEST(quick_setup_cpu_renderer_gets_the_lightest) {
    Hardware hw;
    hw.gl = true;
    hw.gl_software = true;
    hw.gl_renderer = "llvmpipe (LLVM 19.1, 256 bits)";
    hw.vulkan_error = "no device";
    const auto table = presets();
    SH_CHECK(recommend_renderer(hw) == Renderer::Gl);
    SH_CHECK(recommend_preset(hw, Renderer::Gl, table) == "original");
    SH_CHECK(renderer_availability(hw, Renderer::Vulkan) == Availability::NoVulkan);

    // Lavapipe is a Vulkan device too, but one the game does not draw on.
    hw.vulkan = true;
    hw.kind = GpuKind::Cpu;
    SH_CHECK(renderer_availability(hw, Renderer::Vulkan) == Availability::NoVulkan);
    SH_CHECK(recommend_renderer(hw) == Renderer::Gl);
}

SH_TEST(quick_setup_integrated_gets_enhanced_on_gl) {
    Hardware hw;
    hw.gl = true;
    hw.vulkan = true;
    hw.kind = GpuKind::Integrated;
    hw.vram_bytes = 2 * kGiB;
    const auto table = presets();
    SH_CHECK(recommend_renderer(hw) == Renderer::Gl);
    SH_CHECK(recommend_preset(hw, Renderer::Gl, table) == "enhanced");
    SH_CHECK(recommend_preset(hw, Renderer::Vulkan, table) == "enhanced");
    SH_CHECK(renderer_availability(hw, Renderer::Vulkan) == Availability::Available);
}

SH_TEST(quick_setup_legacy_gl_prefers_vulkan) {
    // macOS: an integrated GPU whose OpenGL is the frozen layer over Metal, Vulkan through MoltenVK.
    Hardware hw;
    hw.gl = true;
    hw.gl_legacy = true;
    hw.vulkan = true;
    hw.kind = GpuKind::Integrated;
    hw.vram_bytes = 32 * kGiB;
    const auto table = presets();
    SH_CHECK(recommend_renderer(hw) == Renderer::Vulkan);
    SH_CHECK(recommend_preset(hw, Renderer::Vulkan, table) == "enhanced");
    // Without a usable Vulkan device OpenGL stays the choice.
    hw.vulkan = false;
    SH_CHECK(recommend_renderer(hw) == Renderer::Gl);
}

SH_TEST(quick_setup_discrete_by_memory_and_rays) {
    const auto table = presets();
    SH_CHECK(recommend_renderer(discrete(4 * kGiB, false)) == Renderer::Vulkan);
    SH_CHECK(recommend_preset(discrete(4 * kGiB, false), Renderer::Vulkan, table) == "high");
    SH_CHECK(recommend_preset(discrete(0, false), Renderer::Vulkan, table) == "high");
    SH_CHECK(recommend_preset(discrete(8 * kGiB, false), Renderer::Vulkan, table) == "ultra");
    SH_CHECK(recommend_preset(discrete(6 * kGiB, true), Renderer::Vulkan, table) == "ultra");
    SH_CHECK(recommend_preset(discrete(12 * kGiB, true), Renderer::Vulkan, table) == "ultra_rtx");
    // On OpenGL a discrete card gets high: what lies above it is mostly Vulkan's.
    SH_CHECK(recommend_preset(discrete(12 * kGiB, true), Renderer::Gl, table) == "high");
    // The experimental presets are never suggested.
    for (const auto& vram : {2 * kGiB, 6 * kGiB, 16 * kGiB})
        SH_CHECK(recommend_preset(discrete(vram, true), Renderer::Vulkan, table).find("upscaled") == std::string::npos);
}

SH_TEST(quick_setup_presets_the_hardware_cannot_run) {
    const auto table = presets();
    const Preset& rtx = table[5];
    SH_CHECK(preset_availability(discrete(8 * kGiB, true), Renderer::Vulkan, rtx) == Availability::Available);
    SH_CHECK(preset_availability(discrete(8 * kGiB, true), Renderer::Gl, rtx) == Availability::NoRayTracing);
    SH_CHECK(preset_availability(discrete(8 * kGiB, false), Renderer::Vulkan, rtx) == Availability::NoRayTracing);
    SH_CHECK(preset_availability(discrete(8 * kGiB, false), Renderer::Gl, table[3]) == Availability::Available);
    // A table without the wished preset falls to the nearest lighter one; one without any known name to its first
    // available entry.
    const std::vector<Preset> short_table = {{"original", false}, {"enhanced", false}, {"high", false}};
    SH_CHECK(recommend_preset(discrete(12 * kGiB, true), Renderer::Vulkan, short_table) == "high");
    const std::vector<Preset> odd = {{"rays_only", true}, {"plain", false}};
    SH_CHECK(recommend_preset(discrete(1 * kGiB, false), Renderer::Vulkan, odd) == "plain");
    SH_CHECK(recommend_preset(discrete(1 * kGiB, false), Renderer::Vulkan, {}).empty());
}

SH_TEST(quick_setup_vulkan_device_types) {
    SH_CHECK(gpu_kind_from_vulkan_type("discrete") == GpuKind::Discrete);
    SH_CHECK(gpu_kind_from_vulkan_type("integrated") == GpuKind::Integrated);
    SH_CHECK(gpu_kind_from_vulkan_type("cpu") == GpuKind::Cpu);
    SH_CHECK(gpu_kind_from_vulkan_type("virtual") == GpuKind::Other);
    SH_CHECK(gpu_kind_from_vulkan_type("") == GpuKind::None);
}

SH_TEST(quick_setup_resolutions) {
    const std::vector<Resolution> modes = {{1920, 1080}, {1280, 720}, {1920, 1080}, {640, 400}, {2560, 1440}};
    const auto choices = resolution_choices(modes, {2560, 1440});
    SH_CHECK(choices.size() == 3);
    SH_CHECK(choices[0] == (Resolution{2560, 1440}) && choices[2] == (Resolution{1280, 720}));
    SH_CHECK(nearest_resolution(choices, {1920, 1080}) == 1);
    SH_CHECK(nearest_resolution(choices, {1600, 900}) == 2);  // the largest that fits
    SH_CHECK(nearest_resolution(choices, {320, 240}) == 0);
    // The desktop is always offered; nothing known gives a safe window size.
    SH_CHECK(resolution_choices({}, {3440, 1440}).front() == (Resolution{3440, 1440}));
    SH_CHECK(resolution_choices({}, {}).front() == (Resolution{1024, 768}));
}

SH_TEST(quick_setup_config_lines) {
    Choices c;
    c.language = "ru";
    c.renderer = Renderer::Vulkan;
    c.preset = "ultra_rtx";
    c.resolution = {2560, 1440};
    c.window_mode = WindowMode::Borderless;
    const KeyValues values = config_values(c);
    const KeyValues expected = {{"language", "ru"},  {"renderer", "vulkan"}, {"graphics_preset", "ultra_rtx"},
                                {"width", "2560"},   {"height", "1440"},     {"fullscreen", "2"},
                                {"quick_setup", "0"}};
    SH_CHECK(values == expected);
    c.window_mode = WindowMode::Fullscreen;
    c.renderer = Renderer::Gl;
    SH_CHECK(config_values(c)[5].second == "1" && config_values(c)[1].second == "gl");

    const std::string text = new_config_text({{"quick_setup", "0"}, {"width", "800"}}, 10);
    SH_CHECK(text.find("\nconfig_version = 10\nquick_setup = 0\nwidth = 800\n") != std::string::npos);
    SH_CHECK(text.front() == '#');
}

SH_TEST(quick_setup_when_it_shows) {
    SH_CHECK(should_show({false, false, 0, false}));  // no config.cfg: the first start
    SH_CHECK(should_show({false, true, 1, false}));   // the shipped config, or Options asked for it
    SH_CHECK(!should_show({false, true, 0, false}));  // answered already
    SH_CHECK(!should_show({false, false, 0, true}));  // a command script drives the run
    SH_CHECK(!should_show({false, true, 1, true}));
    SH_CHECK(should_show({true, true, 0, true}));  // --setup always
}

SH_TEST(quick_setup_starting_language) {
    const std::vector<std::string> codes = {"en", "ru", "pt", "zh"};
    SH_CHECK(initial_language("ru", "de_DE.UTF-8", codes) == "ru");
    SH_CHECK(initial_language("auto", "pt_BR.UTF-8", codes) == "pt");
    SH_CHECK(initial_language("", "zh_CN", codes) == "zh");
    SH_CHECK(initial_language("auto", "de_DE.UTF-8", codes) == "en");
    SH_CHECK(initial_language("xx", "C", codes) == "en");
    SH_CHECK(initial_language("auto", "", codes) == "en");
}

SH_TEST(quick_setup_script_lines) {
    const auto commands = parse_script("# a comment\n\n  lang ru \nshot /tmp/a b.bmp\r\napply");
    const std::vector<ScriptCommand> expected = {{"lang", "ru"}, {"shot", "/tmp/a b.bmp"}, {"apply", ""}};
    SH_CHECK(commands == expected);
    SH_CHECK(parse_script("").empty());
}

SH_TEST(quick_setup_strings_in_every_language) {
    const std::filesystem::path text = std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "text";
    Strings en;
    en.load(text, "en");
    SH_CHECK(en.get("ui.setup.title") == "Quick Setup");
    SH_CHECK(en.format("ui.setup.step", {"2", "4", "Renderer"}) == "Step 2 of 4: Renderer");
    SH_CHECK(en.has("ui.options.value.ultra_rtx"));
    SH_CHECK(en.get("ui.setup.no_such_key") == "ui.setup.no_such_key");
    for (const char* code : {"ar", "de", "es", "fr", "it", "ja", "ko", "pl", "pt", "ru", "tr", "zh"}) {
        Strings s;
        s.load(text, code);
        SH_CHECK(s.get("ui.setup.title") != en.get("ui.setup.title"));
        SH_CHECK(s.get("ui.setup.apply") != en.get("ui.setup.apply"));
    }
    SH_CHECK(fill("{0}-{1}-{2}-{9}", {"a", "b"}) == "a-b-{2}-{9}");
}
