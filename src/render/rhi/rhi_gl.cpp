#include "render/rhi/rhi_gl.h"

#include "render/rhi/rhi_pool.h"
#include "render/rhi/shader_overrides.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sh::rhi {

namespace {

// The handful of GL 3.3 core types and enums the backend uses, so it needs no GL header or loader library
// (glad lives in the ps1 layer, which the RHI must not depend on).
using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLdouble = double;
using GLchar = char;
using GLubyte = unsigned char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

#if defined(_WIN32) && !defined(_WIN64)
#define SH_GLAPI __stdcall
#else
#define SH_GLAPI
#endif

constexpr GLenum GL_TRIANGLES = 0x0004, GL_TRIANGLE_STRIP = 0x0005, GL_LINES = 0x0001;
constexpr GLenum GL_BYTE = 0x1400, GL_UNSIGNED_BYTE = 0x1401, GL_SHORT = 0x1402, GL_UNSIGNED_SHORT = 0x1403,
                 GL_UNSIGNED_INT = 0x1405, GL_FLOAT = 0x1406;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE0 = 0x84C0, GL_TEXTURE_2D_MULTISAMPLE = 0x9100;
constexpr GLenum GL_MAX_SAMPLES = 0x8D57, GL_MAX_COLOR_TEXTURE_SAMPLES = 0x910E,
                 GL_MAX_DEPTH_TEXTURE_SAMPLES = 0x910F, GL_SAMPLE_ALPHA_TO_COVERAGE = 0x809E;
constexpr GLenum GL_RGBA = 0x1908, GL_RGBA8 = 0x8058, GL_RG = 0x8227, GL_RG8 = 0x822B, GL_RED = 0x1903,
                 GL_R8 = 0x8229, GL_DEPTH_STENCIL = 0x84F9, GL_DEPTH24_STENCIL8 = 0x88F0,
                 GL_UNSIGNED_INT_24_8 = 0x84FA;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_WRAP_S = 0x2802,
                 GL_TEXTURE_WRAP_T = 0x2803, GL_TEXTURE_MAX_LEVEL = 0x813D, GL_NEAREST = 0x2600,
                 GL_LINEAR = 0x2601, GL_CLAMP_TO_EDGE = 0x812F, GL_REPEAT = 0x2901, GL_MIRRORED_REPEAT = 0x8370;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5, GL_PACK_ALIGNMENT = 0x0D05, GL_UNPACK_ROW_LENGTH = 0x0CF2;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892, GL_ELEMENT_ARRAY_BUFFER = 0x8893, GL_UNIFORM_BUFFER = 0x8A11,
                 GL_STREAM_DRAW = 0x88E0, GL_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT = 0x8A34, GL_MAX_TEXTURE_SIZE = 0x0D33;
constexpr GLbitfield GL_MAP_WRITE_BIT = 0x0002, GL_MAP_INVALIDATE_RANGE_BIT = 0x0004,
                     GL_MAP_UNSYNCHRONIZED_BIT = 0x0020;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31, GL_FRAGMENT_SHADER = 0x8B30, GL_COMPILE_STATUS = 0x8B81,
                 GL_LINK_STATUS = 0x8B82, GL_INFO_LOG_LENGTH = 0x8B84;
constexpr GLuint GL_INVALID_INDEX = 0xFFFFFFFFu;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40, GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9,
                 GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_STENCIL_ATTACHMENT = 0x821A,
                 GL_FRAMEBUFFER_COMPLETE = 0x8CD5, GL_BACK = 0x0405, GL_FRONT = 0x0404;
constexpr GLenum GL_DEPTH_TEST = 0x0B71, GL_STENCIL_TEST = 0x0B90, GL_BLEND = 0x0BE2, GL_SCISSOR_TEST = 0x0C11,
                 GL_CULL_FACE = 0x0B44;
constexpr GLbitfield GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x0100, GL_STENCIL_BUFFER_BIT = 0x0400;
constexpr GLenum GL_CW = 0x0900, GL_CCW = 0x0901;
constexpr GLenum GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02;
constexpr GLenum GL_NO_ERROR = 0;
constexpr GLenum GL_EXTENSIONS = 0x1F03, GL_NUM_EXTENSIONS = 0x821D;
constexpr GLenum GL_TIME_ELAPSED = 0x88BF, GL_QUERY_RESULT = 0x8866, GL_QUERY_RESULT_AVAILABLE = 0x8867;
constexpr GLenum GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX = 0x9048,
                 GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX = 0x9049, GL_TEXTURE_FREE_MEMORY_ATI = 0x87FC;

#define SH_GL_FUNCS(X)                                                                                      \
    X(void, GetIntegerv, (GLenum, GLint*))                                                                  \
    X(const GLubyte*, GetString, (GLenum))                                                                  \
    X(const GLubyte*, GetStringi, (GLenum, GLuint))                                                         \
    X(void, GenQueries, (GLsizei, GLuint*))                                                                 \
    X(void, DeleteQueries, (GLsizei, const GLuint*))                                                        \
    X(void, BeginQuery, (GLenum, GLuint))                                                                   \
    X(void, EndQuery, (GLenum))                                                                             \
    X(void, GetQueryObjectiv, (GLuint, GLenum, GLint*))                                                     \
    X(void, GetQueryObjectui64v, (GLuint, GLenum, std::uint64_t*))                                          \
    X(GLenum, GetError, ())                                                                                 \
    X(void, GenTextures, (GLsizei, GLuint*))                                                                \
    X(void, DeleteTextures, (GLsizei, const GLuint*))                                                       \
    X(void, BindTexture, (GLenum, GLuint))                                                                  \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))       \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*))    \
    X(void, CopyTexSubImage2D, (GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei))               \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                         \
    X(void, PixelStorei, (GLenum, GLint))                                                                   \
    X(void, ActiveTexture, (GLenum))                                                                        \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                                 \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                                        \
    X(void, BindBuffer, (GLenum, GLuint))                                                                   \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                                          \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))                                     \
    X(void, BindBufferRange, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr))                                \
    X(void*, MapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield))                                    \
    X(GLboolean, UnmapBuffer, (GLenum))                                                                     \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                                            \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                                   \
    X(void, BindVertexArray, (GLuint))                                                                      \
    X(void, EnableVertexAttribArray, (GLuint))                                                              \
    X(void, DisableVertexAttribArray, (GLuint))                                                             \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))                  \
    X(void, VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void*))                            \
    X(GLuint, CreateShader, (GLenum))                                                                       \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                            \
    X(void, CompileShader, (GLuint))                                                                        \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                                          \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                         \
    X(void, DeleteShader, (GLuint))                                                                         \
    X(GLuint, CreateProgram, ())                                                                            \
    X(void, AttachShader, (GLuint, GLuint))                                                                 \
    X(void, LinkProgram, (GLuint))                                                                          \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                                         \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                        \
    X(void, DeleteProgram, (GLuint))                                                                        \
    X(void, UseProgram, (GLuint))                                                                           \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                                   \
    X(void, Uniform1i, (GLint, GLint))                                                                      \
    X(GLuint, GetUniformBlockIndex, (GLuint, const GLchar*))                                                \
    X(void, UniformBlockBinding, (GLuint, GLuint, GLuint))                                                  \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                                            \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                                   \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                              \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))  \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                  \
    X(void, TexImage2DMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLboolean))                  \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                                             \
    X(void, ReadBuffer, (GLenum))                                                                           \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                                     \
    X(void, DepthRange, (GLdouble, GLdouble))                                                               \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                                      \
    X(void, Enable, (GLenum))                                                                               \
    X(void, Disable, (GLenum))                                                                              \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                            \
    X(void, BlendEquationSeparate, (GLenum, GLenum))                                                        \
    X(void, BlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                               \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                                        \
    X(void, DepthMask, (GLboolean))                                                                         \
    X(void, DepthFunc, (GLenum))                                                                            \
    X(void, StencilFunc, (GLenum, GLint, GLuint))                                                           \
    X(void, StencilOp, (GLenum, GLenum, GLenum))                                                            \
    X(void, StencilMask, (GLuint))                                                                          \
    X(void, CullFace, (GLenum))                                                                             \
    X(void, FrontFace, (GLenum))                                                                            \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                               \
    X(void, ClearDepth, (GLdouble))                                                                         \
    X(void, ClearStencil, (GLint))                                                                          \
    X(void, Clear, (GLbitfield))                                                                            \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                                           \
    X(void, DrawElementsBaseVertex, (GLenum, GLsizei, GLenum, const void*, GLint))                          \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))                            \
    X(void, Finish, ())                                                                                     \
    X(void, Flush, ())                                                                                      \
    X(void, GenSamplers, (GLsizei, GLuint*))                                                                \
    X(void, DeleteSamplers, (GLsizei, const GLuint*))                                                       \
    X(void, BindSampler, (GLuint, GLuint))                                                                  \
    X(void, SamplerParameteri, (GLuint, GLenum, GLint))

struct Gl {
#define SH_GL_MEMBER(ret, name, args) ret(SH_GLAPI* name) args = nullptr;
    SH_GL_FUNCS(SH_GL_MEMBER)
#undef SH_GL_MEMBER

    Result<void> load(void* (*getProc)(const char*)) {
#define SH_GL_LOAD(ret, name, args)                                                      \
    name = reinterpret_cast<ret(SH_GLAPI*) args>(getProc("gl" #name));                   \
    if (!name) return Result<void>::fail("gl: missing entry point gl" #name);
        SH_GL_FUNCS(SH_GL_LOAD)
#undef SH_GL_LOAD
        // Optional: GLES (ANGLE) has no polygon mode, and only the editor's wireframe view asks for it.
        PolygonMode = reinterpret_cast<void(SH_GLAPI*)(GLenum, GLenum)>(getProc("glPolygonMode"));
        return Result<void>::ok();
    }

    void(SH_GLAPI* PolygonMode)(GLenum, GLenum) = nullptr;
};

// Vertex and fragment slots share GL's binding namespaces; fragment slots start here.
constexpr GLuint kFragmentSlotBase = 8;
constexpr std::size_t kUniformRingBytes = 1u << 20;

struct TexFormatGl {
    GLenum internal, format, type;
    std::uint32_t bytesPerTexel;
};

TexFormatGl glFormat(TextureFormat f) {
    switch (f) {
        case TextureFormat::RGBA8:
            return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
        case TextureFormat::RG8:
            return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2};
        case TextureFormat::R8:
            return {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1};
        case TextureFormat::D24S8:
            return {GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 4};
        case TextureFormat::RGBA16F:
            break;  // refused by createTexture (Caps::floatTargets)
    }
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4};
}

GLenum glBlendFactor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return 0;
        case BlendFactor::One: return 1;
        case BlendFactor::SrcColor: return 0x0300;
        case BlendFactor::OneMinusSrcColor: return 0x0301;
        case BlendFactor::SrcAlpha: return 0x0302;
        case BlendFactor::OneMinusSrcAlpha: return 0x0303;
        case BlendFactor::DstAlpha: return 0x0304;
        case BlendFactor::OneMinusDstAlpha: return 0x0305;
        case BlendFactor::DstColor: return 0x0306;
        case BlendFactor::OneMinusDstColor: return 0x0307;
        case BlendFactor::ConstantColor: return 0x8001;
        case BlendFactor::OneMinusConstantColor: return 0x8002;
    }
    return 1;
}

GLenum glBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return 0x8006;
        case BlendOp::Subtract: return 0x800A;
        case BlendOp::ReverseSubtract: return 0x800B;
        case BlendOp::Min: return 0x8007;
        case BlendOp::Max: return 0x8008;
    }
    return 0x8006;
}

GLenum glCompare(CompareOp op) { return 0x0200 + static_cast<GLenum>(op); }

GLenum glStencilOpOf(StencilOp op) {
    switch (op) {
        case StencilOp::Keep: return 0x1E00;
        case StencilOp::Zero: return 0;
        case StencilOp::Replace: return 0x1E01;
        case StencilOp::IncrementClamp: return 0x1E02;
        case StencilOp::DecrementClamp: return 0x1E03;
        case StencilOp::Invert: return 0x150A;
        case StencilOp::IncrementWrap: return 0x8507;
        case StencilOp::DecrementWrap: return 0x8508;
    }
    return 0x1E00;
}

GLenum glFilter(Filter f) { return f == Filter::Linear ? GL_LINEAR : GL_NEAREST; }
GLenum glAddress(AddressMode m) {
    switch (m) {
        case AddressMode::ClampToEdge: return GL_CLAMP_TO_EDGE;
        case AddressMode::Repeat: return GL_REPEAT;
        case AddressMode::MirroredRepeat: return GL_MIRRORED_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

struct GlTexture {
    GLuint id = 0;  // 0 with isDefault = the window's framebuffer
    TextureDesc desc;
    bool isDefault = false;
    std::uint64_t bytes = 0;  // video memory this allocation counts for the profiler
};
GLenum textureTarget(const GlTexture& t) { return t.desc.samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D; }

struct GlBuffer {
    GLuint id = 0;
    BufferDesc desc;
    std::uint32_t storage = 0;  // bytes of the current GL storage (an orphaning write sizes it)
    std::uint64_t bytes = 0;    // video memory counted for the profiler: the allocated size
};
struct GlSampler {
    GLuint id = 0;
};
struct GlShader {
    GLuint id = 0;
    ShaderStage stage = ShaderStage::Vertex;
    std::vector<std::string> samplers;
    std::vector<std::string> blocks;
    // A mod's shader (shader_overrides.h) keeps the built-in source: a pipeline it does not link with goes back to it.
    std::string builtin;
    std::string name;
};
struct GlPipeline {
    GLuint program = 0;
    PipelineDesc desc;  // vertexLayout.attributes points into `attributes`
    std::vector<VertexAttribute> attributes;
};

// Fixed-function state as last applied, so a pipeline bind sends only what differs (RENDER.md section 2).
struct Applied {
    bool valid = false;
    GLuint program = 0;
    BlendState blend;
    DepthStencilState ds;
    CullMode cull = CullMode::None;
    FrontFace frontFace = FrontFace::CounterClockwise;
    bool wireframe = false;
    std::array<float, 4> blendConstants{-1.f, -1.f, -1.f, -1.f};
    int stencilRef = -1;
    bool scissorEnabled = false;
};

bool sameBlend(const BlendState& a, const BlendState& b) {
    return a.enable == b.enable && a.srcColor == b.srcColor && a.dstColor == b.dstColor && a.colorOp == b.colorOp &&
           a.srcAlpha == b.srcAlpha && a.dstAlpha == b.dstAlpha && a.alphaOp == b.alphaOp &&
           a.writeMask == b.writeMask && a.alphaToCoverage == b.alphaToCoverage;
}

class GlDevice;

class GlRenderPass final : public RenderPass {
public:
    explicit GlRenderPass(GlDevice& device) : dev_(device) {}
    void bindPipeline(PipelineH pipeline) override;
    void setViewport(const Viewport& viewport) override;
    void setScissor(const Rect& rect) override;
    void setBlendConstants(const std::array<float, 4>& rgba) override;
    void setStencilReference(std::uint8_t reference) override;
    void bindVertexBuffer(BufferH buffer, std::uint32_t offset) override;
    void bindIndexBuffer(BufferH buffer, IndexType type, std::uint32_t offset) override;
    void bindSampler(ShaderStage stage, std::uint32_t slot, TextureH texture, SamplerH sampler) override;
    void pushUniforms(ShaderStage stage, std::uint32_t slot, std::span<const std::byte> data) override;
    void draw(std::uint32_t vertexCount, std::uint32_t firstVertex) override;
    void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t vertexOffset) override;

    std::int32_t targetHeight = 0;

private:
    bool prepareDraw();
    void writeUniforms(GLuint binding, std::span<const std::byte> data);
    GlDevice& dev_;
};

class GlCommandList final : public CommandList {
public:
    explicit GlCommandList(GlDevice& device) : dev_(device) {}
    void uploadTexture(TextureH texture, const Rect& rect, std::span<const std::byte> pixels) override;
    void uploadTextureLevel(TextureH texture, std::uint32_t level, const Rect& rect,
                            std::span<const std::byte> pixels) override;
    void uploadBuffer(BufferH buffer, std::uint32_t offset, std::span<const std::byte> data) override;
    void copyTexture(TextureH src, const Rect& srcRect, TextureH dst, std::int32_t dstX, std::int32_t dstY) override;
    void blitTexture(TextureH src, const Rect& srcRect, TextureH dst, const Rect& dstRect, Filter filter) override;
    void resolveTexture(TextureH multisampled, TextureH dst) override;
    RenderPass* beginRenderPass(const RenderPassDesc& desc) override;
    void endRenderPass() override;
    TextureH acquireSwapchainTexture(std::uint32_t* width, std::uint32_t* height) override;

private:
    GlDevice& dev_;
};

class GlDevice final : public Device {
public:
    explicit GlDevice(GlDeviceDesc desc) : desc_(std::move(desc)), cmd_(*this), pass_(*this) {}
    ~GlDevice() override;

    Result<void> init();

    Backend backend() const override { return Backend::Gl33; }
    const Caps& caps() const override { return caps_; }
    Result<TextureH> createTexture(const TextureDesc& desc) override;
    Result<BufferH> createBuffer(const BufferDesc& desc) override;
    Result<SamplerH> createSampler(const SamplerDesc& desc) override;
    Result<ShaderH> createShader(const ShaderDesc& desc) override;
    Result<PipelineH> createPipeline(const PipelineDesc& desc) override;
    void destroy(TextureH handle) override;
    void destroy(BufferH handle) override;
    void destroy(SamplerH handle) override;
    void destroy(ShaderH handle) override;
    void destroy(PipelineH handle) override;
    CommandList* begin() override;
    Result<void> submit() override;
    Result<void> readback(TextureH texture, const Rect& rect, std::span<std::byte> rgba8) override;
    void waitIdle() override { gl_.Finish(); }
    void setProfiling(bool on) override;
    void countDraw(PrimitiveType primitive, std::uint32_t count) {
        ++drawCalls_;
        if (primitive == PrimitiveType::TriangleList) triangles_ += count / 3;
        else if (primitive == PrimitiveType::TriangleStrip && count > 2) triangles_ += count - 2;
    }
    ProfileStats takeProfileStats() override;

private:
    friend class GlRenderPass;
    friend class GlCommandList;

    GLuint framebufferFor(TextureH color, TextureH depth, Result<void>& status);
    // GLSL 450 as GL 3.3 takes it; 0 and the log on a compile error.
    GLuint compileShader(ShaderStage stage, std::string_view glsl, std::string& log);
    void applyPipeline(const GlPipeline& p);
    void applyStencilFunc();

    GlDeviceDesc desc_;
    Gl gl_;
    Caps caps_;
    detail::Pool<TextureH, GlTexture> textures_;
    detail::Pool<BufferH, GlBuffer> buffers_;
    detail::Pool<SamplerH, GlSampler> samplers_;
    detail::Pool<ShaderH, GlShader> shaders_;
    detail::Pool<PipelineH, GlPipeline> pipelines_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, GLuint> framebuffers_;  // (color, depth) handles
    GLuint vao_ = 0;
    GLuint readFbo_ = 0;
    GLuint uniformRing_ = 0;
    std::size_t uniformOffset_ = 0;
    std::size_t uniformAlign_ = 256;
    std::array<std::vector<std::byte>, 16> uniformLast_;  // per GL binding: the block last pushed there
    TextureH swapchain_;
    bool swapchainAcquired_ = false;
    bool open_ = false;

    // A timer query result arrives a frame or two late, so a few are in flight at once.
    struct TimerQuery {
        GLuint id = 0;
        bool pending = false;
    };
    void collectTimerQueries();
    std::array<TimerQuery, 4> timerQueries_{};
    int activeTimer_ = -1;
    bool profiling_ = false;
    bool memInfoNvx_ = false, memInfoAti_ = false, memInfoProbed_ = false;
    std::uint64_t gpuNs_ = 0;
    std::uint64_t appVramBytes_ = 0;  // what the live textures and buffers of this device take (profiler)
    std::uint32_t drawCalls_ = 0;
    std::uint64_t triangles_ = 0;
    bool inPass_ = false;

    Applied applied_;
    GlCommandList cmd_;
    GlRenderPass pass_;
    const GlPipeline* pipeline_ = nullptr;
    PipelineH pipelineH_;
    GLuint vertexBuffer_ = 0;
    std::uint32_t vertexOffset_ = 0;
    bool vertexDirty_ = true;
    const GlPipeline* vertexLayoutOf_ = nullptr;
    std::uint32_t enabledAttribs_ = 0;
    GLuint indexBuffer_ = 0;
    IndexType indexType_ = IndexType::U16;
    std::uint32_t indexOffset_ = 0;
    int stencilRef_ = 0;
};

GlDevice::~GlDevice() {
    pipelines_.forEach([&](GlPipeline& p) { gl_.DeleteProgram(p.program); });
    shaders_.forEach([&](GlShader& s) { gl_.DeleteShader(s.id); });
    samplers_.forEach([&](GlSampler& s) { gl_.DeleteSamplers(1, &s.id); });
    buffers_.forEach([&](GlBuffer& b) { gl_.DeleteBuffers(1, &b.id); });
    textures_.forEach([&](GlTexture& t) {
        if (t.id) gl_.DeleteTextures(1, &t.id);
    });
    for (auto& [key, fbo] : framebuffers_) gl_.DeleteFramebuffers(1, &fbo);
    if (readFbo_) gl_.DeleteFramebuffers(1, &readFbo_);
    if (uniformRing_) gl_.DeleteBuffers(1, &uniformRing_);
    if (vao_) gl_.DeleteVertexArrays(1, &vao_);
    for (TimerQuery& q : timerQueries_)
        if (q.id) gl_.DeleteQueries(1, &q.id);
}

void GlDevice::setProfiling(bool on) {
    if (on == profiling_) return;
    profiling_ = on;
    gpuNs_ = 0;
    if (on && !memInfoProbed_) {
        memInfoProbed_ = true;
        GLint count = 0;
        gl_.GetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (GLint i = 0; i < count; ++i) {
            const char* name = reinterpret_cast<const char*>(gl_.GetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
            if (!name) continue;
            if (std::strcmp(name, "GL_NVX_gpu_memory_info") == 0) memInfoNvx_ = true;
            if (std::strcmp(name, "GL_ATI_meminfo") == 0) memInfoAti_ = true;
        }
    }
}

void GlDevice::collectTimerQueries() {
    for (TimerQuery& q : timerQueries_) {
        if (!q.pending) continue;
        GLint ready = 0;
        gl_.GetQueryObjectiv(q.id, GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) continue;
        std::uint64_t ns = 0;
        gl_.GetQueryObjectui64v(q.id, GL_QUERY_RESULT, &ns);
        gpuNs_ += ns;
        q.pending = false;
    }
}

ProfileStats GlDevice::takeProfileStats() {
    ProfileStats out;
    out.drawCalls = drawCalls_;
    out.triangles = triangles_;
    drawCalls_ = 0;
    triangles_ = 0;
    out.appVramKb = static_cast<std::int64_t>(appVramBytes_ / 1024);
    if (!profiling_) return out;
    collectTimerQueries();
    out.gpuMs = static_cast<double>(gpuNs_) / 1.0e6;
    gpuNs_ = 0;
    if (memInfoNvx_) {
        GLint total = 0, avail = 0;
        gl_.GetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &total);
        gl_.GetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &avail);
        if (total > 0) {
            out.vramTotalKb = total;
            out.vramFreeKb = avail;
            out.vramUsedKb = total - avail;
        }
    } else if (memInfoAti_) {
        GLint info[4] = {0, 0, 0, 0};
        gl_.GetIntegerv(GL_TEXTURE_FREE_MEMORY_ATI, info);
        if (info[0] > 0) out.vramFreeKb = info[0];
    }
    return out;
}

Result<void> GlDevice::init() {
    if (!desc_.getProcAddress) return Result<void>::fail("gl: no getProcAddress");
    if (auto r = gl_.load(desc_.getProcAddress); !r) return r;
    const char* version = reinterpret_cast<const char*>(gl_.GetString(GL_VERSION));
    const char* renderer = reinterpret_cast<const char*>(gl_.GetString(GL_RENDERER));
    const char* vendor = reinterpret_cast<const char*>(gl_.GetString(GL_VENDOR));
    if (!version) return Result<void>::fail("gl: no current context");
    caps_.driver = std::string("opengl ") + version;
    caps_.adapter = std::string(vendor ? vendor : "") + " / " + (renderer ? renderer : "");
    caps_.clipZeroToOne = false;
    caps_.fragCoordTopLeft = false;

    GLint maxTexture = 0;
    gl_.GetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    caps_.maxTextureSize = static_cast<std::uint32_t>(std::max(maxTexture, 0));
    // A scene target pairs an RGBA8 colour texture with a D24S8 one: both must take the count.
    GLint maxSamples = 0, maxColor = 0, maxDepth = 0;
    gl_.GetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    gl_.GetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &maxColor);
    gl_.GetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &maxDepth);
    caps_.maxSamples = static_cast<std::uint32_t>(std::max(1, std::min({maxSamples, maxColor, maxDepth})));

    GLint align = 256;
    gl_.GetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
    uniformAlign_ = static_cast<std::size_t>(std::max(align, 16));
    gl_.GenVertexArrays(1, &vao_);
    gl_.GenFramebuffers(1, &readFbo_);
    gl_.GenBuffers(1, &uniformRing_);
    gl_.BindBuffer(GL_UNIFORM_BUFFER, uniformRing_);
    gl_.BufferData(GL_UNIFORM_BUFFER, kUniformRingBytes, nullptr, GL_STREAM_DRAW);
    GlTexture def;
    def.isDefault = true;
    def.desc.usage = kUsageColorTarget;
    swapchain_ = textures_.add(def);
    if (gl_.GetError() != GL_NO_ERROR) return Result<void>::fail("gl: error during device init");
    return Result<void>::ok();
}

// A texture's video memory for the profiler: its texels with a full mip chain for sampled textures (the
// mip level is 0, so just the level), times the samples of a multisampled target.
static std::uint64_t textureBytes(const TextureDesc& desc) {
    const std::uint64_t bpp = desc.format == TextureFormat::R8 ? 1 : desc.format == TextureFormat::RG8 ? 2 : 4;
    std::uint64_t bytes = 0;
    for (std::uint32_t l = 0; l < std::max<std::uint32_t>(desc.mipLevels, 1); ++l)
        bytes += static_cast<std::uint64_t>(std::max(desc.width >> l, 1u)) * std::max(desc.height >> l, 1u) * bpp;
    return bytes * (desc.samples > 1 ? desc.samples : 1);
}

Result<TextureH> GlDevice::createTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0) return Result<TextureH>::fail("gl: empty texture");
    if (desc.format == TextureFormat::RGBA16F) return Result<TextureH>::fail("gl: no float targets");
    if (caps_.maxTextureSize != 0 && (desc.width > caps_.maxTextureSize || desc.height > caps_.maxTextureSize))
        return Result<TextureH>::fail("gl: " + std::to_string(desc.width) + "x" + std::to_string(desc.height) +
                                      " is over the device's " + std::to_string(caps_.maxTextureSize) + " texture limit");
    const TexFormatGl f = glFormat(desc.format);
    GlTexture t;
    t.desc = desc;
    t.desc.name = {};
    // The context is shared with the PS1 renderer: an error it left pending must not read as this allocation's.
    for (int i = 0; i < 16 && gl_.GetError() != GL_NO_ERROR; ++i) {
    }
    gl_.GenTextures(1, &t.id);
    if (desc.samples > 1) {
        if (desc.samples > caps_.maxSamples || (desc.usage & kUsageSampled) != 0) {
            gl_.DeleteTextures(1, &t.id);
            return Result<TextureH>::fail("gl: " + std::to_string(desc.samples) +
                                          " samples need a target-only texture of at most " +
                                          std::to_string(caps_.maxSamples) + " samples");
        }
        gl_.BindTexture(GL_TEXTURE_2D_MULTISAMPLE, t.id);
        gl_.TexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, static_cast<GLsizei>(desc.samples), f.internal,
                                  static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), 1);
        gl_.BindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
        if (gl_.GetError() != GL_NO_ERROR) {
            gl_.DeleteTextures(1, &t.id);
            return Result<TextureH>::fail("gl: glTexImage2DMultisample failed for " + std::string(desc.name));
        }
        t.bytes = textureBytes(desc);
        appVramBytes_ += t.bytes;
        return Result<TextureH>::ok(textures_.add(t));
    }
    gl_.BindTexture(GL_TEXTURE_2D, t.id);
    const std::uint32_t levels = std::max<std::uint32_t>(desc.mipLevels, 1);
    gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(levels - 1));
    gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (std::uint32_t l = 0; l < levels; ++l)
        gl_.TexImage2D(GL_TEXTURE_2D, static_cast<GLint>(l), static_cast<GLint>(f.internal),
                       static_cast<GLsizei>(std::max(desc.width >> l, 1u)),
                       static_cast<GLsizei>(std::max(desc.height >> l, 1u)), 0, f.format, f.type, nullptr);
    gl_.BindTexture(GL_TEXTURE_2D, 0);
    if (gl_.GetError() != GL_NO_ERROR) {
        gl_.DeleteTextures(1, &t.id);
        return Result<TextureH>::fail("gl: glTexImage2D failed for " + std::string(desc.name));
    }
    t.bytes = textureBytes(desc);
    appVramBytes_ += t.bytes;
    return Result<TextureH>::ok(textures_.add(t));
}

Result<BufferH> GlDevice::createBuffer(const BufferDesc& desc) {
    GlBuffer b;
    b.desc = desc;
    b.desc.name = {};
    gl_.GenBuffers(1, &b.id);
    const GLenum target = desc.usage == BufferUsage::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    if (target == GL_ELEMENT_ARRAY_BUFFER) gl_.BindVertexArray(vao_);
    gl_.BindBuffer(target, b.id);
    gl_.BufferData(target, desc.size, nullptr, GL_DYNAMIC_DRAW);
    if (target == GL_ELEMENT_ARRAY_BUFFER) indexBuffer_ = b.id;
    b.bytes = desc.size;
    appVramBytes_ += b.bytes;
    return Result<BufferH>::ok(buffers_.add(b));
}

Result<SamplerH> GlDevice::createSampler(const SamplerDesc& desc) {
    GlSampler s;
    gl_.GenSamplers(1, &s.id);
    gl_.SamplerParameteri(s.id, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(glFilter(desc.minFilter)));
    gl_.SamplerParameteri(s.id, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(glFilter(desc.magFilter)));
    gl_.SamplerParameteri(s.id, GL_TEXTURE_WRAP_S, static_cast<GLint>(glAddress(desc.addressU)));
    gl_.SamplerParameteri(s.id, GL_TEXTURE_WRAP_T, static_cast<GLint>(glAddress(desc.addressV)));
    return Result<SamplerH>::ok(samplers_.add(s));
}

GLuint GlDevice::compileShader(ShaderStage stage, std::string_view glsl, std::string& log) {
    // The source is GLSL 450 with Vulkan-only qualifiers behind SH_RHI_VULKAN; GL 3.3 takes it as 330 core.
    std::string src(glsl);
    if (src.rfind("#version", 0) == 0) {
        const std::size_t eol = src.find('\n');
        src.replace(0, eol, "#version 330 core");
    } else {
        src.insert(0, "#version 330 core\n");
    }
    const GLuint id = gl_.CreateShader(stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
    const GLchar* text = src.c_str();
    gl_.ShaderSource(id, 1, &text, nullptr);
    gl_.CompileShader(id);
    GLint ok = 0;
    gl_.GetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        gl_.GetShaderiv(id, GL_INFO_LOG_LENGTH, &len);
        log.assign(static_cast<std::size_t>(std::max(len, 1)), '\0');
        gl_.GetShaderInfoLog(id, len, nullptr, log.data());
        gl_.DeleteShader(id);
        return 0;
    }
    return id;
}

Result<ShaderH> GlDevice::createShader(const ShaderDesc& desc) {
    if (desc.stage != ShaderStage::Vertex && desc.stage != ShaderStage::Fragment)
        return Result<ShaderH>::fail("gl: shader " + std::string(desc.name) + ": no such stage on this backend");
    GlShader s;
    s.stage = desc.stage;
    s.name = desc.name;
    std::string log;
    if (const std::optional<std::string> mod = shader_override(desc.name, ""); mod) {
        s.id = compileShader(desc.stage, *mod, log);
        if (s.id != 0) {
            s.builtin = desc.glsl;
            shader_log(0, "shader " + std::string(desc.name) + ": the mod's shaders/" + shader_file_name(desc.name));
        } else {
            shader_log(1, "shaders/" + shader_file_name(desc.name) + " does not compile, the built-in shader is used: " +
                              log);
        }
    }
    if (s.id == 0) {
        s.id = compileShader(desc.stage, desc.glsl, log);
    }
    if (s.id == 0) {
        return Result<ShaderH>::fail("gl: compile " + std::string(desc.name) + ": " + log);
    }
    for (std::string_view n : desc.samplerNames) s.samplers.emplace_back(n);
    for (std::string_view n : desc.uniformBlockNames) s.blocks.emplace_back(n);
    return Result<ShaderH>::ok(shaders_.add(std::move(s)));
}

Result<PipelineH> GlDevice::createPipeline(const PipelineDesc& desc) {
    GlShader* vs = shaders_.get(desc.vertexShader);
    GlShader* fs = shaders_.get(desc.fragmentShader);
    if (!vs || !fs) return Result<PipelineH>::fail("gl: pipeline with a released shader");
    GlPipeline p;
    GLint ok = 0;
    const auto link = [&] {
        p.program = gl_.CreateProgram();
        gl_.AttachShader(p.program, vs->id);
        gl_.AttachShader(p.program, fs->id);
        gl_.LinkProgram(p.program);
        gl_.GetProgramiv(p.program, GL_LINK_STATUS, &ok);
    };
    link();
    if (!ok && (!vs->builtin.empty() || !fs->builtin.empty())) {
        // A mod's shader that compiles but does not fit its partner (in/out, uniforms): both go back to the built-in.
        gl_.DeleteProgram(p.program);
        for (GlShader* s : {vs, fs}) {
            if (s->builtin.empty()) {
                continue;
            }
            std::string log;
            const GLuint id = compileShader(s->stage, s->builtin, log);
            if (id == 0) {
                return Result<PipelineH>::fail("gl: compile " + s->name + ": " + log);
            }
            shader_log(1, "shaders/" + shader_file_name(s->name) + " does not link in " + std::string(desc.name) +
                              ", the built-in shader is used");
            gl_.DeleteShader(s->id);
            s->id = id;
            s->builtin.clear();
        }
        link();
    }
    if (!ok) {
        GLint len = 0;
        gl_.GetProgramiv(p.program, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<std::size_t>(std::max(len, 1)), '\0');
        gl_.GetProgramInfoLog(p.program, len, nullptr, log.data());
        gl_.DeleteProgram(p.program);
        return Result<PipelineH>::fail("gl: link " + std::string(desc.name) + ": " + log);
    }
    gl_.UseProgram(p.program);
    applied_.program = p.program;
    for (const GlShader* s : {static_cast<const GlShader*>(vs), static_cast<const GlShader*>(fs)}) {
        const GLuint base = s->stage == ShaderStage::Fragment ? kFragmentSlotBase : 0;
        for (std::size_t i = 0; i < s->blocks.size(); ++i) {
            const GLuint index = gl_.GetUniformBlockIndex(p.program, s->blocks[i].c_str());
            if (index != GL_INVALID_INDEX) gl_.UniformBlockBinding(p.program, index, base + static_cast<GLuint>(i));
        }
        for (std::size_t i = 0; i < s->samplers.size(); ++i) {
            const GLint loc = gl_.GetUniformLocation(p.program, s->samplers[i].c_str());
            if (loc >= 0) gl_.Uniform1i(loc, static_cast<GLint>(base + i));
        }
    }
    p.desc = desc;
    p.desc.name = {};
    p.attributes.assign(desc.vertexLayout.attributes.begin(), desc.vertexLayout.attributes.end());
    PipelineH h = pipelines_.add(std::move(p));
    GlPipeline* stored = pipelines_.get(h);
    stored->desc.vertexLayout.attributes = stored->attributes;
    return Result<PipelineH>::ok(h);
}

void GlDevice::destroy(TextureH handle) {
    if (handle == swapchain_) return;
    auto t = textures_.remove(handle);
    if (!t) return;
    for (auto it = framebuffers_.begin(); it != framebuffers_.end();) {
        if (it->first.first == handle.raw || it->first.second == handle.raw) {
            gl_.DeleteFramebuffers(1, &it->second);
            it = framebuffers_.erase(it);
        } else {
            ++it;
        }
    }
    gl_.DeleteTextures(1, &t->id);
    appVramBytes_ -= t->bytes;
}

void GlDevice::destroy(BufferH handle) {
    if (auto b = buffers_.remove(handle)) {
        if (vertexBuffer_ == b->id) vertexBuffer_ = 0, vertexDirty_ = true;
        if (indexBuffer_ == b->id) indexBuffer_ = 0;
        gl_.DeleteBuffers(1, &b->id);
        appVramBytes_ -= b->bytes;
    }
}

void GlDevice::destroy(SamplerH handle) {
    if (auto s = samplers_.remove(handle)) gl_.DeleteSamplers(1, &s->id);
}

void GlDevice::destroy(ShaderH handle) {
    // Linked programs keep their own copy of the code, so pipelines stay valid.
    if (auto s = shaders_.remove(handle)) gl_.DeleteShader(s->id);
}

void GlDevice::destroy(PipelineH handle) {
    GlPipeline* p = pipelines_.get(handle);
    if (!p) return;
    if (pipeline_ == p) pipeline_ = nullptr;
    if (vertexLayoutOf_ == p) vertexLayoutOf_ = nullptr, vertexDirty_ = true;
    if (applied_.program == p->program) applied_.program = 0;
    gl_.DeleteProgram(p->program);
    pipelines_.remove(handle);
}

GLuint GlDevice::framebufferFor(TextureH color, TextureH depth, Result<void>& status) {
    status = Result<void>::ok();
    if (color == swapchain_) return 0;
    const auto key = std::make_pair(color.raw, depth.raw);
    if (auto it = framebuffers_.find(key); it != framebuffers_.end()) return it->second;
    GlTexture* c = textures_.get(color);
    GlTexture* d = depth ? textures_.get(depth) : nullptr;
    if (!c || (depth && !d)) {
        status = Result<void>::fail("gl: render pass with a released texture");
        return 0;
    }
    GLuint fbo = 0;
    gl_.GenFramebuffers(1, &fbo);
    gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl_.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, textureTarget(*c), c->id, 0);
    if (d) gl_.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, textureTarget(*d), d->id, 0);
    if (gl_.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        gl_.DeleteFramebuffers(1, &fbo);
        status = Result<void>::fail("gl: incomplete framebuffer");
        return 0;
    }
    framebuffers_.emplace(key, fbo);
    return fbo;
}

void GlDevice::applyPipeline(const GlPipeline& p) {
    const PipelineDesc& d = p.desc;
    const bool all = !applied_.valid;
    if (all || applied_.program != p.program) {
        gl_.UseProgram(p.program);
        applied_.program = p.program;
    }
    const BlendState& b = d.blend;
    if (all || !sameBlend(applied_.blend, b)) {
        if (b.enable)
            gl_.Enable(GL_BLEND);
        else
            gl_.Disable(GL_BLEND);
        gl_.BlendFuncSeparate(glBlendFactor(b.srcColor), glBlendFactor(b.dstColor), glBlendFactor(b.srcAlpha),
                              glBlendFactor(b.dstAlpha));
        gl_.BlendEquationSeparate(glBlendOp(b.colorOp), glBlendOp(b.alphaOp));
        gl_.ColorMask((b.writeMask & kWriteR) != 0, (b.writeMask & kWriteG) != 0, (b.writeMask & kWriteB) != 0,
                      (b.writeMask & kWriteA) != 0);
        if (b.alphaToCoverage)
            gl_.Enable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        else
            gl_.Disable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        applied_.blend = b;
    }
    const DepthStencilState& s = d.depthStencil;
    DepthStencilState& a = applied_.ds;
    if (all || a.depthTest != s.depthTest) s.depthTest ? gl_.Enable(GL_DEPTH_TEST) : gl_.Disable(GL_DEPTH_TEST);
    if (all || a.depthWrite != s.depthWrite) gl_.DepthMask(s.depthWrite);
    if (all || a.depthCompare != s.depthCompare) gl_.DepthFunc(glCompare(s.depthCompare));
    if (all || a.stencilTest != s.stencilTest)
        s.stencilTest ? gl_.Enable(GL_STENCIL_TEST) : gl_.Disable(GL_STENCIL_TEST);
    if (all || a.stencil.fail != s.stencil.fail || a.stencil.depthFail != s.stencil.depthFail ||
        a.stencil.pass != s.stencil.pass)
        gl_.StencilOp(glStencilOpOf(s.stencil.fail), glStencilOpOf(s.stencil.depthFail),
                      glStencilOpOf(s.stencil.pass));
    if (all || a.stencilWriteMask != s.stencilWriteMask) gl_.StencilMask(s.stencilWriteMask);
    const bool funcChanged = all || a.stencil.compare != s.stencil.compare || a.stencilReadMask != s.stencilReadMask;
    a = s;
    if (funcChanged) applied_.stencilRef = -1;
    if (all || applied_.cull != d.cull) {
        if (d.cull == CullMode::None) {
            gl_.Disable(GL_CULL_FACE);
        } else {
            gl_.Enable(GL_CULL_FACE);
            gl_.CullFace(d.cull == CullMode::Front ? GL_FRONT : GL_BACK);
        }
        applied_.cull = d.cull;
    }
    // GL rasterises with +Y up while the RHI's targets are top-left: the image is mirrored in memory, not in
    // NDC, so winding is unchanged.
    if (all || applied_.frontFace != d.frontFace) {
        gl_.FrontFace(d.frontFace == FrontFace::Clockwise ? GL_CW : GL_CCW);
        applied_.frontFace = d.frontFace;
    }
    if ((all || applied_.wireframe != d.wireframe) && gl_.PolygonMode) {
        constexpr GLenum kFrontAndBack = 0x0408, kLine = 0x1B01, kFill = 0x1B02;
        gl_.PolygonMode(kFrontAndBack, d.wireframe ? kLine : kFill);
        applied_.wireframe = d.wireframe;
    }
    applied_.valid = true;
    applyStencilFunc();
}

void GlDevice::applyStencilFunc() {
    if (applied_.stencilRef == stencilRef_) return;
    gl_.StencilFunc(glCompare(applied_.ds.stencil.compare), stencilRef_, applied_.ds.stencilReadMask);
    applied_.stencilRef = stencilRef_;
}

CommandList* GlDevice::begin() {
    if (open_) return nullptr;
    open_ = true;
    swapchainAcquired_ = false;
    gl_.BindVertexArray(vao_);
    if (profiling_ && activeTimer_ < 0) {
        collectTimerQueries();
        for (std::size_t i = 0; i < timerQueries_.size(); ++i) {
            TimerQuery& q = timerQueries_[i];
            if (q.pending) continue;
            if (!q.id) gl_.GenQueries(1, &q.id);
            gl_.BeginQuery(GL_TIME_ELAPSED, q.id);
            activeTimer_ = static_cast<int>(i);
            break;
        }
    }
    return &cmd_;
}

Result<void> GlDevice::submit() {
    if (!open_) return Result<void>::fail("gl: submit without begin");
    if (inPass_) cmd_.endRenderPass();
    open_ = false;
    if (activeTimer_ >= 0) {
        gl_.EndQuery(GL_TIME_ELAPSED);
        timerQueries_[static_cast<std::size_t>(activeTimer_)].pending = true;
        activeTimer_ = -1;
    }
    gl_.Flush();
    if (swapchainAcquired_ && desc_.swapBuffers) desc_.swapBuffers();
    swapchainAcquired_ = false;
    const GLenum err = gl_.GetError();
    if (err != GL_NO_ERROR) return Result<void>::fail("gl: error " + std::to_string(err) + " in frame");
    return Result<void>::ok();
}

Result<void> GlDevice::readback(TextureH texture, const Rect& rect, std::span<std::byte> rgba8) {
    GlTexture* t = textures_.get(texture);
    if (!t) return Result<void>::fail("gl: readback of a released texture");
    if (!t->isDefault && t->desc.format != TextureFormat::RGBA8) return Result<void>::fail("gl: readback needs RGBA8");
    if (t->desc.samples > 1) return Result<void>::fail("gl: readback of a multisampled texture (resolve it first)");
    const std::size_t row = static_cast<std::size_t>(rect.w) * 4;
    if (rgba8.size() < row * static_cast<std::size_t>(rect.h)) return Result<void>::fail("gl: readback buffer small");
    std::uint32_t height = t->desc.height;
    if (t->isDefault) {
        std::uint32_t w = 0;
        if (desc_.drawableSize) desc_.drawableSize(&w, &height);
        gl_.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        gl_.ReadBuffer(GL_BACK);
    } else {
        gl_.BindFramebuffer(GL_READ_FRAMEBUFFER, readFbo_);
        gl_.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->id, 0);
        gl_.ReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    std::vector<std::byte> tmp(row * static_cast<std::size_t>(rect.h));
    gl_.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl_.ReadPixels(rect.x, static_cast<GLint>(height) - rect.y - rect.h, rect.w, rect.h, GL_RGBA, GL_UNSIGNED_BYTE,
                   tmp.data());
    gl_.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    // GL rows are bottom-up; the RHI hands back the top row first.
    for (std::int32_t y = 0; y < rect.h; ++y)
        std::memcpy(rgba8.data() + static_cast<std::size_t>(y) * row,
                    tmp.data() + static_cast<std::size_t>(rect.h - 1 - y) * row, row);
    const GLenum err = gl_.GetError();
    if (err != GL_NO_ERROR) return Result<void>::fail("gl: readback error " + std::to_string(err));
    return Result<void>::ok();
}

// ---- command list -----------------------------------------------------------------------------------------

void GlCommandList::uploadTexture(TextureH texture, const Rect& rect, std::span<const std::byte> pixels) {
    uploadTextureLevel(texture, 0, rect, pixels);
}

void GlCommandList::uploadTextureLevel(TextureH texture, std::uint32_t level, const Rect& rect,
                                       std::span<const std::byte> pixels) {
    GlTexture* t = dev_.textures_.get(texture);
    if (!t || t->isDefault) return;
    const TexFormatGl f = glFormat(t->desc.format);
    if (pixels.size() < static_cast<std::size_t>(rect.w) * rect.h * f.bytesPerTexel) return;
    auto& gl = dev_.gl_;
    gl.BindTexture(GL_TEXTURE_2D, t->id);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.TexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), rect.x, rect.y, rect.w, rect.h, f.format, f.type,
                     pixels.data());
    gl.BindTexture(GL_TEXTURE_2D, 0);
}

void GlCommandList::uploadBuffer(BufferH buffer, std::uint32_t offset, std::span<const std::byte> data) {
    GlBuffer* b = dev_.buffers_.get(buffer);
    if (!b || offset + data.size() > b->desc.size) return;
    auto& gl = dev_.gl_;
    const GLenum target = b->desc.usage == BufferUsage::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    gl.BindBuffer(target, b->id);
    // Orphan on a write from the start: the driver hands out fresh storage instead of stalling on draws that
    // still read the previous contents (a stall measured on a Mali-T720). The new
    // storage is a power-of-two bucket of the write, not the capacity: the game's vertex stream has an 18 MB
    // capacity for a frame of 1-2 MB, and orphaning all of it several times a frame cost llvmpipe 60%.
    const std::uint32_t end = offset + static_cast<std::uint32_t>(data.size());
    if (offset == 0) {
        std::uint32_t bucket = 64u << 10;
        while (bucket < end && bucket < b->desc.size) bucket <<= 1;
        b->storage = std::min(bucket, b->desc.size);
        gl.BufferData(target, b->storage, nullptr, GL_DYNAMIC_DRAW);
    } else if (end > b->storage) {
        // A write past the storage the last full write sized: the earlier contents do not survive.
        b->storage = b->desc.size;
        gl.BufferData(target, b->storage, nullptr, GL_DYNAMIC_DRAW);
    }
    gl.BufferSubData(target, offset, static_cast<GLsizeiptr>(data.size()), data.data());
    if (target == GL_ELEMENT_ARRAY_BUFFER) dev_.indexBuffer_ = b->id;
}

void GlCommandList::copyTexture(TextureH src, const Rect& srcRect, TextureH dst, std::int32_t dstX,
                                std::int32_t dstY) {
    GlTexture* s = dev_.textures_.get(src);
    GlTexture* d = dev_.textures_.get(dst);
    if (!s || !d || s->isDefault || d->isDefault || s->desc.samples > 1 || d->desc.samples > 1) return;
    auto& gl = dev_.gl_;
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, dev_.readFbo_);
    gl.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->id, 0);
    gl.BindTexture(GL_TEXTURE_2D, d->id);
    gl.CopyTexSubImage2D(GL_TEXTURE_2D, 0, dstX, dstY, srcRect.x, srcRect.y, srcRect.w, srcRect.h);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

// Rectangles are top-left based like every RHI rectangle; GL's are bottom-left, so both are flipped.
void GlCommandList::blitTexture(TextureH src, const Rect& srcRect, TextureH dst, const Rect& dstRect, Filter filter) {
    GlTexture* s = dev_.textures_.get(src);
    GlTexture* d = dev_.textures_.get(dst);
    if (!s || !d || s->isDefault || s->desc.samples > 1 || d->desc.samples > 1) return;
    if (dev_.inPass_) endRenderPass();
    Result<void> status = Result<void>::ok();
    const GLuint drawFbo = dev_.framebufferFor(dst, {}, status);
    if (!status) return;
    std::uint32_t dh = d->desc.height, dw = d->desc.width;
    if (d->isDefault && dev_.desc_.drawableSize) dev_.desc_.drawableSize(&dw, &dh);
    const GLint sh = static_cast<GLint>(s->desc.height), dH = static_cast<GLint>(dh);
    auto& gl = dev_.gl_;
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, dev_.readFbo_);
    gl.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->id, 0);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    gl.BlitFramebuffer(srcRect.x, sh - srcRect.y - srcRect.h, srcRect.x + srcRect.w, sh - srcRect.y,
                       dstRect.x, dH - dstRect.y - dstRect.h, dstRect.x + dstRect.w, dH - dstRect.y,
                       GL_COLOR_BUFFER_BIT, glFilter(filter));
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GlCommandList::resolveTexture(TextureH multisampled, TextureH dst) {
    GlTexture* s = dev_.textures_.get(multisampled);
    GlTexture* d = dev_.textures_.get(dst);
    if (!s || !d || d->isDefault || s->desc.samples <= 1 || d->desc.samples > 1) return;
    if (s->desc.width != d->desc.width || s->desc.height != d->desc.height) return;
    if (dev_.inPass_) endRenderPass();
    Result<void> status = Result<void>::ok();
    const GLuint readFbo = dev_.framebufferFor(multisampled, {}, status);
    if (!status) return;
    const GLuint drawFbo = dev_.framebufferFor(dst, {}, status);
    if (!status) return;
    auto& gl = dev_.gl_;
    const GLint w = static_cast<GLint>(d->desc.width), h = static_cast<GLint>(d->desc.height);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFbo);
    gl.BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

RenderPass* GlCommandList::beginRenderPass(const RenderPassDesc& desc) {
    if (dev_.inPass_) endRenderPass();
    Result<void> status = Result<void>::ok();
    const GLuint fbo = dev_.framebufferFor(desc.color.texture, desc.depthStencil.texture, status);
    if (!status) return nullptr;
    GlTexture* c = dev_.textures_.get(desc.color.texture);
    if (!c) return nullptr;
    std::uint32_t w = c->desc.width, h = c->desc.height;
    if (c->isDefault && dev_.desc_.drawableSize) dev_.desc_.drawableSize(&w, &h);
    auto& gl = dev_.gl_;
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.BindVertexArray(dev_.vao_);
    gl.Viewport(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
    gl.DepthRange(0.0, 1.0);
    gl.Enable(GL_SCISSOR_TEST);
    gl.Scissor(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
    GLbitfield clear = 0;
    if (desc.color.load == LoadOp::Clear) {
        gl.ColorMask(1, 1, 1, 1);
        gl.ClearColor(desc.color.clear[0], desc.color.clear[1], desc.color.clear[2], desc.color.clear[3]);
        clear |= GL_COLOR_BUFFER_BIT;
    }
    if (desc.depthStencil.texture && desc.depthStencil.load == LoadOp::Clear) {
        gl.DepthMask(1);
        gl.StencilMask(0xFF);
        gl.ClearDepth(desc.depthStencil.clearDepth);
        gl.ClearStencil(desc.depthStencil.clearStencil);
        clear |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    }
    if (clear) {
        gl.Clear(clear);
        dev_.applied_.valid = false;  // the masks above changed
    }
    dev_.inPass_ = true;
    dev_.pass_.targetHeight = static_cast<std::int32_t>(h);
    dev_.vertexDirty_ = true;
    return &dev_.pass_;
}

void GlCommandList::endRenderPass() {
    if (!dev_.inPass_) return;
    dev_.inPass_ = false;
    dev_.gl_.Disable(GL_SCISSOR_TEST);
    dev_.applied_.valid = false;
}

TextureH GlCommandList::acquireSwapchainTexture(std::uint32_t* width, std::uint32_t* height) {
    if (!dev_.desc_.swapBuffers) return {};
    std::uint32_t w = 0, h = 0;
    if (dev_.desc_.drawableSize) dev_.desc_.drawableSize(&w, &h);
    if (w == 0 || h == 0) return {};
    if (GlTexture* t = dev_.textures_.get(dev_.swapchain_)) {
        t->desc.width = w;
        t->desc.height = h;
    }
    if (width) *width = w;
    if (height) *height = h;
    dev_.swapchainAcquired_ = true;
    return dev_.swapchain_;
}

// ---- render pass ------------------------------------------------------------------------------------------

void GlRenderPass::bindPipeline(PipelineH pipeline) {
    GlPipeline* p = dev_.pipelines_.get(pipeline);
    if (!p) return;
    dev_.pipeline_ = p;
    dev_.pipelineH_ = pipeline;
    dev_.applyPipeline(*p);
}

void GlRenderPass::setViewport(const Viewport& v) {
    dev_.gl_.Viewport(static_cast<GLint>(v.x), static_cast<GLint>(static_cast<float>(targetHeight) - v.y - v.h),
                      static_cast<GLsizei>(v.w), static_cast<GLsizei>(v.h));
    dev_.gl_.DepthRange(v.minDepth, v.maxDepth);
}

void GlRenderPass::setScissor(const Rect& r) { dev_.gl_.Scissor(r.x, targetHeight - r.y - r.h, r.w, r.h); }

void GlRenderPass::setBlendConstants(const std::array<float, 4>& c) {
    if (dev_.applied_.blendConstants == c) return;
    dev_.gl_.BlendColor(c[0], c[1], c[2], c[3]);
    dev_.applied_.blendConstants = c;
}

void GlRenderPass::setStencilReference(std::uint8_t reference) {
    dev_.stencilRef_ = reference;
    if (dev_.applied_.valid) dev_.applyStencilFunc();
}

void GlRenderPass::bindVertexBuffer(BufferH buffer, std::uint32_t offset) {
    GlBuffer* b = dev_.buffers_.get(buffer);
    const GLuint id = b ? b->id : 0;
    if (id != dev_.vertexBuffer_ || offset != dev_.vertexOffset_) dev_.vertexDirty_ = true;
    dev_.vertexBuffer_ = id;
    dev_.vertexOffset_ = offset;
}

void GlRenderPass::bindIndexBuffer(BufferH buffer, IndexType type, std::uint32_t offset) {
    GlBuffer* b = dev_.buffers_.get(buffer);
    dev_.gl_.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, b ? b->id : 0);
    dev_.indexBuffer_ = b ? b->id : 0;
    dev_.indexType_ = type;
    dev_.indexOffset_ = offset;
}

void GlRenderPass::bindSampler(ShaderStage stage, std::uint32_t slot, TextureH texture, SamplerH sampler) {
    GlTexture* t = dev_.textures_.get(texture);
    GlSampler* s = dev_.samplers_.get(sampler);
    const GLuint unit = (stage == ShaderStage::Fragment ? kFragmentSlotBase : 0) + slot;
    auto& gl = dev_.gl_;
    gl.ActiveTexture(GL_TEXTURE0 + unit);
    gl.BindTexture(GL_TEXTURE_2D, t ? t->id : 0);
    gl.BindSampler(unit, s ? s->id : 0);
    gl.ActiveTexture(GL_TEXTURE0);
}

void GlRenderPass::pushUniforms(ShaderStage stage, std::uint32_t slot, std::span<const std::byte> data) {
    const GLuint binding = (stage == ShaderStage::Fragment ? kFragmentSlotBase : 0) + slot;
    if (binding >= dev_.uniformLast_.size()) return;
    auto& gl = dev_.gl_;
    gl.BindBuffer(GL_UNIFORM_BUFFER, dev_.uniformRing_);
    const std::size_t aligned = (dev_.uniformOffset_ + dev_.uniformAlign_ - 1) / dev_.uniformAlign_ * dev_.uniformAlign_;
    if (aligned + data.size() > kUniformRingBytes) {
        // Orphaning replaces the store every bound range points into, so the blocks of the other slots, which
        // stay valid until they are pushed again (SDL3 GPU semantics), are written into the new store too.
        // Without this a block pushed once per frame (fog, projection) read whatever the new store held.
        gl.BufferData(GL_UNIFORM_BUFFER, kUniformRingBytes, nullptr, GL_STREAM_DRAW);
        dev_.uniformOffset_ = 0;
        for (GLuint b = 0; b < dev_.uniformLast_.size(); ++b)
            if (b != binding && !dev_.uniformLast_[b].empty()) writeUniforms(b, dev_.uniformLast_[b]);
    }
    dev_.uniformLast_[binding].assign(data.begin(), data.end());
    writeUniforms(binding, data);
}

void GlRenderPass::writeUniforms(GLuint binding, std::span<const std::byte> data) {
    auto& gl = dev_.gl_;
    const std::size_t size = data.size();
    const std::size_t offset = (dev_.uniformOffset_ + dev_.uniformAlign_ - 1) / dev_.uniformAlign_ * dev_.uniformAlign_;
    // Unsynchronised: the ring only hands out a range once per wrap, and a wrap orphans the storage, so no draw
    // still reads it. glBufferSubData into a buffer with pending draws makes llvmpipe wait for them: a frame of a
    // few hundred splits ran five times slower.
    void* dst = gl.MapBufferRange(GL_UNIFORM_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size),
                                  GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
    if (dst) {
        std::memcpy(dst, data.data(), size);
        gl.UnmapBuffer(GL_UNIFORM_BUFFER);
    } else {
        gl.BufferSubData(GL_UNIFORM_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data.data());
    }
    gl.BindBufferRange(GL_UNIFORM_BUFFER, binding, dev_.uniformRing_, static_cast<GLintptr>(offset),
                       static_cast<GLsizeiptr>(size));
    dev_.uniformOffset_ = offset + size;
}

bool GlRenderPass::prepareDraw() {
    const GlPipeline* p = dev_.pipeline_;
    if (!p) return false;
    if (!dev_.applied_.valid) dev_.applyPipeline(*p);
    if (!dev_.vertexDirty_ && dev_.vertexLayoutOf_ &&
        dev_.vertexLayoutOf_->desc.vertexLayout.stride == p->desc.vertexLayout.stride &&
        std::equal(dev_.vertexLayoutOf_->attributes.begin(), dev_.vertexLayoutOf_->attributes.end(),
                   p->attributes.begin(), p->attributes.end(), [](const VertexAttribute& a, const VertexAttribute& b) {
                       return a.location == b.location && a.format == b.format && a.offset == b.offset;
                   }))
        return true;
    auto& gl = dev_.gl_;
    gl.BindBuffer(GL_ARRAY_BUFFER, dev_.vertexBuffer_);
    std::uint32_t enabled = 0;
    const GLsizei stride = static_cast<GLsizei>(p->desc.vertexLayout.stride);
    for (const VertexAttribute& a : p->attributes) {
        const void* ptr = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(dev_.vertexOffset_ + a.offset));
        switch (a.format) {
            case VertexFormat::Float: gl.VertexAttribPointer(a.location, 1, GL_FLOAT, 0, stride, ptr); break;
            case VertexFormat::Float2: gl.VertexAttribPointer(a.location, 2, GL_FLOAT, 0, stride, ptr); break;
            case VertexFormat::Float3: gl.VertexAttribPointer(a.location, 3, GL_FLOAT, 0, stride, ptr); break;
            case VertexFormat::Float4: gl.VertexAttribPointer(a.location, 4, GL_FLOAT, 0, stride, ptr); break;
            case VertexFormat::UByte4Norm: gl.VertexAttribPointer(a.location, 4, GL_UNSIGNED_BYTE, 1, stride, ptr); break;
            case VertexFormat::Short4: gl.VertexAttribIPointer(a.location, 4, GL_SHORT, stride, ptr); break;
            case VertexFormat::UByte4: gl.VertexAttribIPointer(a.location, 4, GL_UNSIGNED_BYTE, stride, ptr); break;
            case VertexFormat::Byte4: gl.VertexAttribIPointer(a.location, 4, GL_BYTE, stride, ptr); break;
        }
        gl.EnableVertexAttribArray(a.location);
        enabled |= 1u << a.location;
    }
    for (GLuint i = 0; i < 16; ++i)
        if ((dev_.enabledAttribs_ & (1u << i)) && !(enabled & (1u << i))) gl.DisableVertexAttribArray(i);
    dev_.enabledAttribs_ = enabled;
    dev_.vertexLayoutOf_ = p;
    dev_.vertexDirty_ = false;
    return true;
}

GLenum glPrimitive(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::TriangleList: return GL_TRIANGLES;
        case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
        case PrimitiveType::LineList: return GL_LINES;
    }
    return GL_TRIANGLES;
}

void GlRenderPass::draw(std::uint32_t vertexCount, std::uint32_t firstVertex) {
    if (vertexCount == 0 || !prepareDraw()) return;
    dev_.countDraw(dev_.pipeline_->desc.primitive, vertexCount);
    dev_.gl_.DrawArrays(glPrimitive(dev_.pipeline_->desc.primitive), static_cast<GLint>(firstVertex),
                        static_cast<GLsizei>(vertexCount));
}

void GlRenderPass::drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex, std::int32_t vertexOffset) {
    if (indexCount == 0 || !prepareDraw()) return;
    dev_.countDraw(dev_.pipeline_->desc.primitive, indexCount);
    const bool u32 = dev_.indexType_ == IndexType::U32;
    const std::uintptr_t byteOffset = dev_.indexOffset_ + static_cast<std::uintptr_t>(firstIndex) * (u32 ? 4u : 2u);
    dev_.gl_.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, dev_.indexBuffer_);
    dev_.gl_.DrawElementsBaseVertex(glPrimitive(dev_.pipeline_->desc.primitive), static_cast<GLsizei>(indexCount),
                                    u32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                                    reinterpret_cast<const void*>(byteOffset), vertexOffset);
}

}  // namespace

Result<std::unique_ptr<Device>> createGlDevice(const GlDeviceDesc& desc) {
    auto device = std::make_unique<GlDevice>(desc);
    if (auto r = device->init(); !r) return Result<std::unique_ptr<Device>>::fail(r.error());
    return Result<std::unique_ptr<Device>>::ok(std::move(device));
}

}  // namespace sh::rhi
