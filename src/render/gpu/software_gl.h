// Rendering on the CPU (RENDER.md, section 2.2c): a machine without a usable GPU driver still runs the game through the
// GL backend on Mesa llvmpipe. Linux gets llvmpipe from the system's Mesa; the Windows package carries it in
// <exe>/software_gl/, loaded only when the system's driver has no OpenGL 3.3.
#pragma once

#include <string_view>

namespace sh::gpu {

// The `software_gl` key.
enum class SoftwareGl : int {
    Auto = 0,   // the bundled llvmpipe only without a GL 3.3 driver; a CPU renderer starts at the lightest settings
    Force = 1,  // always render on the CPU (testing)
    Off = 2,    // never load the bundled llvmpipe, never change the settings for a CPU renderer
};

// True for the name of a renderer that draws on the CPU: llvmpipe, softpipe, lavapipe, SwiftShader, WARP and the
// GDI generic implementation.
bool is_software_adapter(std::string_view adapter);

// Force: asks the system's Mesa for llvmpipe (LIBGL_ALWAYS_SOFTWARE). Before the first GL window.
void request_system_software_gl();

// Points SDL's GL loader at <exe>/software_gl/opengl32.dll. False when it is not Windows or the folder is missing.
// Before a GL window is created; the GL library of an earlier window must be unloaded.
bool use_bundled_software_gl();

}  // namespace sh::gpu
