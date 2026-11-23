#include "engine/app/first_run/progress_window.h"

#include "engine/desktop/window_icon.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sh::first_run {

namespace {

constexpr int kWidth = 520;
constexpr int kHeight = 150;
constexpr int kPickerWidth = 680;
constexpr int kPickerHeight = 340;

// 3x5 glyphs, one row per element, bit 2 = left column: 0-9 and '%'.
constexpr std::array<std::array<std::uint8_t, 5>, 11> kGlyphs{{
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7},
    {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {5, 1, 2, 4, 5},
}};
constexpr int kPercentGlyph = 10;

constexpr const char* kStages[] = {"read", "raw", "convert", "verify", "commit"};

void fill(SDL_Renderer* r, int x, int y, int w, int h) {
    const SDL_FRect rect{static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)};
    SDL_RenderFillRect(r, &rect);
}

void draw_glyph(SDL_Renderer* r, int glyph, int x, int y, int scale) {
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (kGlyphs[static_cast<std::size_t>(glyph)][static_cast<std::size_t>(row)] & (4 >> col)) {
                fill(r, x + col * scale, y + row * scale, scale, scale);
            }
        }
    }
}

SDL_Texture* as_texture(ImTextureID id) { return reinterpret_cast<SDL_Texture*>(static_cast<std::intptr_t>(id)); }

// The file dialog answers from another thread on some systems, and possibly after the picker has gone: the state is
// shared with the callback, which owns one reference.
struct DialogState {
    std::mutex mutex;
    bool pending = false;
    bool answered = false;
    std::string path;   // empty on cancel
    std::string error;  // set when the dialog could not be shown
    std::string filter_name;
    SDL_DialogFileFilter filter{};
};

void SDLCALL on_dialog(void* userdata, const char* const* files, int) {
    std::unique_ptr<std::shared_ptr<DialogState>> holder(static_cast<std::shared_ptr<DialogState>*>(userdata));
    DialogState& d = **holder;
    std::lock_guard<std::mutex> lock(d.mutex);
    d.pending = false;
    d.answered = true;
    d.path.clear();
    d.error.clear();
    if (!files) {
        d.error = SDL_GetError();
        if (d.error.empty()) {
            d.error = "?";
        }
    } else if (files[0]) {
        d.path = files[0];
    }
}

void sync_texture(SDL_Renderer* r, ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
        SDL_Texture* t = as_texture(tex->GetTexID());
        if (tex->Status == ImTextureStatus_WantCreate) {
            t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, tex->Width, tex->Height);
            if (t) {
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            }
            tex->SetTexID(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(t)));
        }
        if (t) {
            // The whole texture again: the atlas only grows on new glyphs.
            SDL_UpdateTexture(t, nullptr, tex->GetPixels(), tex->GetPitch());
        }
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        SDL_DestroyTexture(as_texture(tex->GetTexID()));
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

}  // namespace

ProgressWindow::~ProgressWindow() { close(); }

bool ProgressWindow::open() {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return false;
    }
    video_ = true;
    if (!create(kWidth, kHeight)) {
        close();
        return false;
    }
    return true;
}

bool ProgressWindow::create(int width, int height) {
    // SDL3 centres a new window by default.
    window_ = SDL_CreateWindow("Silent Hill", width, height, 0);
    if (window_) {
        if (const char* base = SDL_GetBasePath()) {
            sh::desktop::set_window_icon(window_, std::filesystem::path(base) / "assets" / "icon");
        }
        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (!renderer_) {
            renderer_ = SDL_CreateRenderer(window_, SDL_SOFTWARE_RENDERER);
        }
    }
    title_.clear();
    return renderer_ != nullptr;
}

void ProgressWindow::close() {
    destroy_window();
    if (video_) {
        // The game window's start (sh::platform::start) brings video up again.
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        video_ = false;
    }
}

void ProgressWindow::destroy_window() {
    if (imgui_) {
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            if (tex->GetTexID() != ImTextureID_Invalid) {
                SDL_DestroyTexture(as_texture(tex->GetTexID()));
                tex->SetTexID(ImTextureID_Invalid);
            }
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imgui_ = false;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

bool ProgressWindow::stop_requested() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT ||
            e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
            (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE)) {
            stop_ = true;
        }
    }
    return stop_;
}

bool ProgressWindow::interactive() {
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver && SDL_strcmp(driver, "offscreen") != 0 && SDL_strcmp(driver, "dummy") != 0;
}

bool ProgressWindow::can_pick() const { return renderer_ && interactive(); }

bool ProgressWindow::init_imgui() {
    if (imgui_) {
        return true;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_)) {
        ImGui::DestroyContext();
        return false;
    }
    io.BackendRendererName = "sh_first_run";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::StyleColorsDark();
    // The console's font next to the exe has the Cyrillic of the translation; ImGui's own (ASCII) when it is missing.
    if (const char* base = SDL_GetBasePath()) {
        const std::string font = std::string(base) + "fonts/JetBrainsMonoNL-Regular.ttf";
        if (SDL_GetPathInfo(font.c_str(), nullptr)) {
            font_loaded_ = io.Fonts->AddFontFromFileTTF(font.c_str(), 16.0f) != nullptr;
        }
    }
    if (!font_loaded_) {
        io.Fonts->AddFontDefault();
    }
    imgui_ = true;
    return true;
}

void ProgressWindow::render_imgui() {
    ImDrawData* data = ImGui::GetDrawData();
    if (data->Textures) {
        for (ImTextureData* tex : *data->Textures) {
            sync_texture(renderer_, tex);
        }
    }
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    for (const ImDrawList* list : data->CmdLists) {
        vertices.resize(static_cast<std::size_t>(list->VtxBuffer.Size));
        for (int i = 0; i < list->VtxBuffer.Size; ++i) {
            const ImDrawVert& v = list->VtxBuffer[i];
            SDL_Vertex& o = vertices[static_cast<std::size_t>(i)];
            o.position = {v.pos.x, v.pos.y};
            o.tex_coord = {v.uv.x, v.uv.y};
            // ImU32 colours keep red in the low byte.
            o.color = {static_cast<float>(v.col & 0xFF) / 255.0f, static_cast<float>((v.col >> 8) & 0xFF) / 255.0f,
                       static_cast<float>((v.col >> 16) & 0xFF) / 255.0f,
                       static_cast<float>((v.col >> 24) & 0xFF) / 255.0f};
        }
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback || cmd.ElemCount == 0) {
                continue;
            }
            const SDL_Rect clip{static_cast<int>(cmd.ClipRect.x), static_cast<int>(cmd.ClipRect.y),
                                static_cast<int>(cmd.ClipRect.z - cmd.ClipRect.x),
                                static_cast<int>(cmd.ClipRect.w - cmd.ClipRect.y)};
            if (clip.w <= 0 || clip.h <= 0) {
                continue;
            }
            indices.resize(cmd.ElemCount);
            for (unsigned i = 0; i < cmd.ElemCount; ++i) {
                indices[i] = static_cast<int>(cmd.VtxOffset + list->IdxBuffer[static_cast<int>(cmd.IdxOffset + i)]);
            }
            SDL_SetRenderClipRect(renderer_, &clip);
            SDL_RenderGeometry(renderer_, as_texture(cmd.GetTexID()), vertices.data(),
                               static_cast<int>(vertices.size()), indices.data(), static_cast<int>(indices.size()));
        }
    }
    SDL_SetRenderClipRect(renderer_, nullptr);
}

void ProgressWindow::pick(sh::import::ui::ImagePicker& picker, sh::import::ui::Lang lang,
                          const std::string& gamedata_dir) {
    namespace iu = sh::import::ui;
    using iu::ImagePicker;
    using iu::Str;
    // A window of its own size: a resize reaches the renderer late on some drivers (offscreen, Wayland).
    destroy_window();
    if (!video_ || !create(kPickerWidth, kPickerHeight) || !init_imgui()) {
        picker.quit();
        return;
    }
    SDL_SetWindowTitle(window_, ("Silent Hill - " + iu::format(lang, Str::Title)).c_str());
    title_.clear();
    std::fprintf(stderr, "first run: the disc image picker is open\n");
    std::fflush(stderr);

    auto dialog = std::make_shared<DialogState>();
    dialog->filter_name = iu::format(lang, Str::PickerFilter);
    dialog->filter = SDL_DialogFileFilter{dialog->filter_name.c_str(), "cue;bin;iso;img"};
    std::string buffer_text;
    std::vector<char> buffer(4096, '\0');

    while (picker.state() == ImagePicker::State::Picking) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 50)) {
            do {
                ImGui_ImplSDL3_ProcessEvent(&e);
                if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    picker.quit();
                } else if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) {
                    picker.choose(e.drop.data);
                }
            } while (SDL_PollEvent(&e));
        }
        bool dialog_open = false;
        {
            std::lock_guard<std::mutex> lock(dialog->mutex);
            dialog_open = dialog->pending;
            if (dialog->answered) {
                dialog->answered = false;
                if (!dialog->error.empty()) {
                    picker.dialog_failed(dialog->error);
                } else if (!dialog->path.empty()) {
                    picker.choose(dialog->path);
                }
            }
        }
        if (picker.state() != ImagePicker::State::Picking) {
            break;
        }
        if (picker.text() != buffer_text) {
            buffer_text = picker.text();
            const std::size_t n = std::min(buffer_text.size(), buffer.size() - 1);
            std::copy_n(buffer_text.data(), n, buffer.data());
            buffer[n] = '\0';
        }

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::Begin("picker", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::TextUnformatted(iu::format(lang, Str::PickerTitle).c_str());
        ImGui::Separator();
        ImGui::TextWrapped("%s", iu::format(lang, Str::IntroNeedImage, {gamedata_dir}).c_str());
        ImGui::TextWrapped("%s", iu::format(lang, Str::PickerHint).c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted(iu::format(lang, Str::PathLabel).c_str());
        const std::string browse = iu::format(lang, Str::ActionBrowse);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float browse_w = ImGui::CalcTextSize(browse.c_str()).x + style.FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browse_w - style.ItemSpacing.x);
        bool submit = ImGui::InputText("##path", buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);
        if (buffer_text != buffer.data()) {
            buffer_text = buffer.data();
            picker.set_text(buffer_text);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(dialog_open);
        if (ImGui::Button(browse.c_str())) {
            {
                std::lock_guard<std::mutex> lock(dialog->mutex);
                dialog->pending = true;
            }
            SDL_ShowOpenFileDialog(on_dialog, new std::shared_ptr<DialogState>(dialog), window_, &dialog->filter, 1,
                                   nullptr, false);
        }
        ImGui::EndDisabled();
        if (!picker.error().empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
            ImGui::TextWrapped("%s", picker.error().c_str());
            ImGui::PopStyleColor();
        }
        ImGui::SetCursorPosY(
            std::max(ImGui::GetCursorPosY(), vp->Size.y - ImGui::GetFrameHeight() - style.WindowPadding.y));
        if (ImGui::Button(iu::format(lang, Str::ActionImport).c_str())) {
            submit = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(iu::format(lang, Str::ActionQuit).c_str())) {
            picker.quit();
        }
        ImGui::End();
        ImGui::Render();
        if (submit) {
            picker.submit();
        }

        SDL_SetRenderDrawColor(renderer_, 16, 16, 18, 255);
        SDL_RenderClear(renderer_);
        render_imgui();
        SDL_RenderPresent(renderer_);
    }
    destroy_window();
    if (picker.state() == ImagePicker::State::ImportRequested) {
        create(kWidth, kHeight);
    }
}

void ProgressWindow::draw(const sh::import::ui::ViewModel& v) {
    if (!renderer_) {
        return;
    }
    using sh::import::ui::Phase;

    std::string title = "Silent Hill - " + v.title;
    if (v.has_progress) {
        title += ": " + v.stage_text + " " + std::to_string(v.percent) + "%, " + v.eta_text;
    }
    if (title != title_) {
        title_ = title;
        SDL_SetWindowTitle(window_, title_.c_str());
    }

    const bool stopping = v.phase == Phase::Cancelling || v.phase == Phase::Cancelled || v.phase == Phase::Failed;
    SDL_Renderer* r = renderer_;
    SDL_SetRenderDrawColor(r, 16, 16, 18, 255);
    SDL_RenderClear(r);

    // Stage marks: done, current, still to come.
    const auto current = std::find(std::begin(kStages), std::end(kStages), v.stage) - std::begin(kStages);
    for (int i = 0; i < 5; ++i) {
        if (i < current) {
            SDL_SetRenderDrawColor(r, 120, 120, 128, 255);
        } else if (i == current) {
            SDL_SetRenderDrawColor(r, 220, 220, 228, 255);
        } else {
            SDL_SetRenderDrawColor(r, 48, 48, 54, 255);
        }
        fill(r, 20 + i * 100, 16, 80, 6);
    }

    // Bar: frame, then the filled part.
    const int bx = 20, by = 36, bw = kWidth - 40, bh = 28;
    SDL_SetRenderDrawColor(r, 90, 90, 98, 255);
    fill(r, bx, by, bw, bh);
    SDL_SetRenderDrawColor(r, 28, 28, 32, 255);
    fill(r, bx + 2, by + 2, bw - 4, bh - 4);
    const double fraction = std::clamp(v.fraction, 0.0, 1.0);
    if (stopping) {
        SDL_SetRenderDrawColor(r, 170, 40, 36, 255);
    } else {
        SDL_SetRenderDrawColor(r, 200, 200, 208, 255);
    }
    fill(r, bx + 4, by + 4, static_cast<int>((bw - 8) * fraction), bh - 8);

    // Percentage under the bar.
    const int scale = 8;
    const std::string digits = std::to_string(std::clamp(v.percent, 0, 100));
    const int glyphs = static_cast<int>(digits.size()) + 1;
    int x = (kWidth - (glyphs * 4 - 1) * scale) / 2;
    const int y = by + bh + 12;
    for (const char c : digits) {
        draw_glyph(r, c - '0', x, y, scale);
        x += 4 * scale;
    }
    draw_glyph(r, kPercentGlyph, x, y, scale);

    SDL_RenderPresent(r);
}

}  // namespace sh::first_run
