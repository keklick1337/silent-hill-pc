#pragma once

// Options > Graphics rows of the HDR frame, bloom and HDR10 output (render/post/hdr.h). Native Vulkan only: under
// another renderer the HDR row is shown unavailable with the reason and the rows that depend on it are hidden.

namespace sh::menu {
class Registry;
}

namespace sh::ui {

void register_hdr_options(sh::menu::Registry& reg);

}  // namespace sh::ui
