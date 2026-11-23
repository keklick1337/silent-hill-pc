#pragma once

// The scene's built-in shaders, embedded at build time like the RHI's (cmake/rhi_embed_shader.cmake): GLSL 450 text
// for the GL backend and the SPIR-V glslc made from it for SDL3 GPU.

#include "render/rhi/shaders/embedded.h"

namespace sh::rhi::embedded {

Shader sceneVert();
Shader sceneFrag();
// scene.frag with SH_SCENE_EXACT_DEPTH: the fragment writes its own view depth (overlay layers, RENDER.md 6.6). Its
// GLSL text is the plain source; the define is added where the shader is created.
Shader sceneFragExactDepth();
// Model upscaling (model_upscale.h), native Vulkan only: scene.vert with SH_SCENE_PN (the vertex stage of the
// tessellation path) and the stages after it.
Shader sceneVertPn();
Shader scenePnTesc();
Shader scenePnTese();
Shader scenePnTask();
Shader scenePnMesh();

}  // namespace sh::rhi::embedded

namespace sh::scene {
namespace shaders = sh::rhi::embedded;
}
