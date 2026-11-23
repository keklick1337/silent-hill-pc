#include "render/gpu/renderer.h"

namespace sh::gpu {

namespace {
Renderer* g_renderer = nullptr;
}  // namespace

Renderer* renderer() { return g_renderer; }

void set_renderer(Renderer* r) { g_renderer = r; }

}  // namespace sh::gpu
