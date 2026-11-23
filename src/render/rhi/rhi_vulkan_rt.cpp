// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "render/rhi/rhi_vulkan_rt.h"

#include "render/rhi/rhi_vulkan.h"
#include "render/rhi/rhi_vulkan_native.h"
#include "render/rhi/shaders/embedded.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

namespace sh::rhi {

namespace {

bool hasExt(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const auto& e : list)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

// New bottom-level structures per frame: entering a map builds its cells over a few frames instead of in one hitch.
constexpr std::uint32_t kBuildsPerFrame = 48;
constexpr VkDeviceSize kBuildScratchBudget = 64ull << 20;
// A kept structure no frame used for this long belongs to a map left behind.
constexpr std::uint64_t kEvictFrames = 900;
constexpr std::uint32_t kCompactQueries = 128;

struct Buf {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    VkDeviceAddress address = 0;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct Blas {
    VkAccelerationStructureKHR as = VK_NULL_HANDLE;
    Buf buf;
    VkDeviceAddress address = 0;
    VkBuffer vertices = VK_NULL_HANDLE, indices = VK_NULL_HANDLE;
    std::uint32_t vertexCount = 0;
    std::uint32_t geometries = 0;
    std::uint64_t triangles = 0;
    std::uint64_t lastUsed = 0;
    bool compacted = false;
    int query = -1;  // compacted-size query of `querySlot`, -1 none
    std::uint32_t querySlot = 0;
};

struct Slot {
    Buf instances;
    Buf scratch;
    Buf tlasBuf;
    VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
    std::vector<Blas> transient;  // the posed meshes' structures of the frame this slot carried
    VkQueryPool queries = VK_NULL_HANDLE;
    std::uint32_t queryCount = 0;
    std::vector<const void*> queried;  // the keys whose compacted size the queries hold
    // What the shaders read about a hit: one record per mesh range (rt_trace.comp Geo) and the textures they sample.
    Buf geos;
    std::vector<VkImageView> textures;
    Buf frame;  // the RtFrame block
    Buf regions;  // rt_trace.comp Region: the texture rectangles with their own material
    Buf lights;   // rt_trace.comp Light: the frame's map lights
    VkDescriptorPool pool = VK_NULL_HANDLE;
    bool built = false;  // build() ran in this slot's current frame
};

// rt_trace.comp Geo, scalar layout.
struct GeoRecord {
    std::uint32_t vtx[2];
    std::uint32_t idx[2];
    std::uint32_t first;
    std::uint32_t flags;
    std::uint32_t tex;
    std::uint32_t regions;  // first << 8 | count (RayGeometry regionFirst, regionCount)
    float surface[4];
};
static_assert(sizeof(GeoRecord) == 48);

// The RtFrame block of the compute shaders, std140.
struct FrameBlock {
    float prevFromCur[16];
    float pixelRay[4];
    float prevPixelRay[4];
    float viewport[4];
    float lens[4];
    float beamDir[4];
    float beam[4];
    float beamColor[4];
    float fog[4];
    float ambient[4];
    float effects[4];
    float params[4];
    float up[4];
    float fogParams[4];
    float fogRamp[128];
    float lightInfo[4];  // x: map lights, y: 1 rt_lights
    float sunDir[4];
    float sunColor[4];
    float sky[4];
    float ceilingInfo[4];  // x: roofs
    float ceilings[3 * kMaxRayCeilings][4];
};

// rt_trace.comp Light, scalar layout.
struct LightRecord {
    float position[4];
    float direction[4];
    float color[4];
    float radius;
    std::uint32_t flags;  // 1 casts shadows, 2 bounces
    float nearClip;
    float pad;
};
static_assert(sizeof(LightRecord) == 64);
static_assert(sizeof(FrameBlock) == 848 + 16 + 16 * 3 * kMaxRayCeilings);

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    VkImageView view = VK_NULL_HANDLE;
};

// Texture slots of the shaders' sampler array; slot 0 is white.
constexpr std::uint32_t kMaxTextures = 1024;
enum Signal { kShadow = 0, kReflect = 1, kGi = 2, kSignals = 3 };

}  // namespace

namespace embedded {  // generated from shaders/*.comp (CMakeLists.txt)
Shader rtTraceComp();
Shader rtDenoiseComp();
}  // namespace embedded

struct RayTracer::Impl {
    Device& device;
    VulkanNative vk;
    VkDeviceSize scratchAlign = 128;
    std::unordered_map<const void*, Blas> kept;
    std::array<Slot, kVulkanFramesInFlight> slots{};
    std::uint64_t frame = 0;
    Stats stats;

    // Tracing and denoising.
    VkDescriptorSetLayout traceLayout = VK_NULL_HANDLE, denoiseLayout = VK_NULL_HANDLE;
    VkPipelineLayout tracePipeLayout = VK_NULL_HANDLE, denoisePipeLayout = VK_NULL_HANDLE;
    VkPipeline tracePipe = VK_NULL_HANDLE, denoisePipe = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    Image white;
    bool whiteReady = false;
    std::uint32_t width = 0, height = 0;
    std::array<Image, 2> gbuf;  // by frame parity: this frame's and the last frame's
    std::array<Image, kSignals> raw, tmp, tmp2, out;
    std::array<std::array<Image, kSignals>, 2> hist;
    std::array<TextureH, kSignals> outTex;
    std::uint32_t parity = 0;
    bool haveHistory = false;
    std::array<double, 12> prevCamera{};
    std::array<float, 4> prevPixelRay{}, prevViewport{};

    explicit Impl(Device& d) : device(d) {}

    void destroyImage(Image& i) {
        if (i.view) vkDestroyImageView(vk.device, i.view, nullptr);
        if (i.image) vmaDestroyImage(vk.allocator, i.image, i.alloc);
        i = {};
    }

    void destroyTargets() {
        for (TextureH& t : outTex) {
            if (t) device.destroy(t);
            t = {};
        }
        for (Image& i : gbuf) destroyImage(i);
        for (auto* set : {&raw, &tmp, &tmp2, &out, &hist[0], &hist[1]})
            for (Image& i : *set) destroyImage(i);
        width = height = 0;
    }

    ~Impl() {
        vkDeviceWaitIdle(vk.device);
        destroyTargets();
        destroyImage(white);
        for (VkPipeline p : {tracePipe, denoisePipe})
            if (p) vkDestroyPipeline(vk.device, p, nullptr);
        for (VkPipelineLayout l : {tracePipeLayout, denoisePipeLayout})
            if (l) vkDestroyPipelineLayout(vk.device, l, nullptr);
        for (VkDescriptorSetLayout l : {traceLayout, denoiseLayout})
            if (l) vkDestroyDescriptorSetLayout(vk.device, l, nullptr);
        if (sampler) vkDestroySampler(vk.device, sampler, nullptr);
        for (Slot& s : slots) {
            freeBuf(s.geos);
            freeBuf(s.frame);
            freeBuf(s.regions);
            freeBuf(s.lights);
            if (s.pool) vkDestroyDescriptorPool(vk.device, s.pool, nullptr);
        }
        for (auto& [key, b] : kept) destroyBlas(b);
        for (Slot& s : slots) {
            for (Blas& b : s.transient) destroyBlas(b);
            if (s.tlas) vkDestroyAccelerationStructureKHR(vk.device, s.tlas, nullptr);
            freeBuf(s.tlasBuf);
            freeBuf(s.instances);
            freeBuf(s.scratch);
            if (s.queries) vkDestroyQueryPool(vk.device, s.queries, nullptr);
        }
    }

    Result<Buf> makeBuf(VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
        Buf b;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = std::max<VkDeviceSize>(size, 256);
        info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        if (host) ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo out{};
        if (VkResult r = vmaCreateBuffer(vk.allocator, &info, &ai, &b.buffer, &b.alloc, &out); r != VK_SUCCESS)
            return Result<Buf>::fail("ray tracing: buffer of " + std::to_string(size >> 10) + " KB: error " +
                                     std::to_string(static_cast<int>(r)));
        b.mapped = out.pMappedData;
        b.size = info.size;
        VkBufferDeviceAddressInfo a{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        a.buffer = b.buffer;
        b.address = vkGetBufferDeviceAddress(vk.device, &a);
        return Result<Buf>::ok(b);
    }

    void freeBuf(Buf& b) {
        if (b.buffer) vmaDestroyBuffer(vk.allocator, b.buffer, b.alloc);
        b = {};
    }

    void destroyBlas(Blas& b) {
        if (b.as) vkDestroyAccelerationStructureKHR(vk.device, b.as, nullptr);
        freeBuf(b.buf);
        b.as = VK_NULL_HANDLE;
    }

    // Frees `b` once the GPU is done with every frame that may trace it.
    void retireBlas(Blas b) {
        VkDevice dev = vk.device;
        VmaAllocator allocator = vk.allocator;
        vulkanRetire(device, [dev, allocator, b] {
            if (b.as) vkDestroyAccelerationStructureKHR(dev, b.as, nullptr);
            if (b.buf.buffer) vmaDestroyBuffer(allocator, b.buf.buffer, b.buf.alloc);
        });
    }

    // Grows a per-slot buffer; the old one is the slot's own and its frame has completed.
    Result<void> ensure(Buf& b, VkDeviceSize size, VkBufferUsageFlags usage, bool host) {
        if (b.buffer && b.size >= size) return Result<void>::ok();
        freeBuf(b);
        auto r = makeBuf(size + size / 2, usage, host);
        if (!r) return Result<void>::fail(r.error());
        b = r.value();
        return Result<void>::ok();
    }

    VkDeviceAddress bufferAddress(BufferH h) {
        VkBufferDeviceAddressInfo a{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        a.buffer = vulkanBuffer(device, h);
        return a.buffer ? vkGetBufferDeviceAddress(vk.device, &a) : 0;
    }

    Result<void> createAs(Blas& b, VkDeviceSize size, VkAccelerationStructureTypeKHR type) {
        auto buf = makeBuf(size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, false);
        if (!buf) return Result<void>::fail(buf.error());
        b.buf = buf.value();
        VkAccelerationStructureCreateInfoKHR ci{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        ci.buffer = b.buf.buffer;
        ci.size = size;
        ci.type = type;
        if (VkResult r = vkCreateAccelerationStructureKHR(vk.device, &ci, nullptr, &b.as); r != VK_SUCCESS) {
            freeBuf(b.buf);
            return Result<void>::fail("ray tracing: vkCreateAccelerationStructureKHR error " +
                                      std::to_string(static_cast<int>(r)));
        }
        VkAccelerationStructureDeviceAddressInfoKHR ai{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        ai.accelerationStructure = b.as;
        b.address = vkGetAccelerationStructureDeviceAddressKHR(vk.device, &ai);
        return Result<void>::ok();
    }

    // The compacted sizes the slot's last frame asked for: each structure is copied into its compacted size and
    // the original freed after this frame.
    void compactFinished(VkCommandBuffer cmd, Slot& s) {
        if (!s.queries) return;
        if (s.queryCount > 0) {
            std::vector<VkDeviceSize> sizes(s.queryCount, 0);
            const VkResult r =
                vkGetQueryPoolResults(vk.device, s.queries, 0, s.queryCount, sizes.size() * sizeof(VkDeviceSize),
                                      sizes.data(), sizeof(VkDeviceSize), VK_QUERY_RESULT_64_BIT);
            for (std::uint32_t i = 0; r == VK_SUCCESS && i < s.queryCount; ++i) {
                auto it = kept.find(s.queried[i]);
                if (it == kept.end() || it->second.query != static_cast<int>(i) || sizes[i] == 0) continue;
                Blas& old = it->second;
                old.query = -1;
                if (sizes[i] >= old.buf.size) {
                    old.compacted = true;
                    continue;
                }
                Blas small = old;
                small.as = VK_NULL_HANDLE;
                small.buf = {};
                if (!createAs(small, sizes[i], VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)) continue;
                VkCopyAccelerationStructureInfoKHR copy{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
                copy.src = old.as;
                copy.dst = small.as;
                copy.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                vkCmdCopyAccelerationStructureKHR(cmd, &copy);
                stats.blasBytes -= old.buf.size;
                stats.blasBytes += small.buf.size;
                retireBlas(old);
                small.compacted = true;
                it->second = small;
                ++stats.compacted;
            }
        }
        vkCmdResetQueryPool(cmd, s.queries, 0, kCompactQueries);
        s.queryCount = 0;
        s.queried.clear();
    }

    struct Pending {
        const RayMesh* mesh = nullptr;
        Blas* target = nullptr;
        std::vector<VkAccelerationStructureGeometryKHR> geoms;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;
        VkAccelerationStructureBuildGeometryInfoKHR info{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        VkDeviceSize scratch = 0;
    };

    // Geometry and sizes of a mesh's bottom-level build; false for a mesh with nothing to build.
    bool describe(const RayMesh& m, bool keep, Pending* p, VkDeviceSize* asSize) {
        const VkDeviceAddress vb = bufferAddress(m.vertices), ib = bufferAddress(m.indices);
        if (!vb || !ib || m.vertexCount == 0 || m.vertexStride < 12) return false;
        std::vector<std::uint32_t> counts;
        for (const RayGeometry& g : m.geometries) {
            if (g.indexCount < 3) continue;
            VkAccelerationStructureGeometryKHR geo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
            geo.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
            // Textured ranges have alpha holes the rays must test; the rest stops a ray at once.
            const bool opaque =
                !(g.flags & (kRayTextured | kRayWater | kRayNoShadow)) && (g.flags & kRayBlendMask) == 0;
            geo.flags = opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
            auto& t = geo.geometry.triangles;
            t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
            t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
            t.vertexData.deviceAddress = vb;
            t.vertexStride = m.vertexStride;
            t.maxVertex = m.vertexCount - 1;
            t.indexType = VK_INDEX_TYPE_UINT32;
            t.indexData.deviceAddress = ib;
            p->geoms.push_back(geo);
            VkAccelerationStructureBuildRangeInfoKHR r{};
            r.primitiveCount = g.indexCount / 3;
            r.primitiveOffset = g.firstIndex * 4;
            p->ranges.push_back(r);
            counts.push_back(r.primitiveCount);
        }
        // A mesh keeps one geometry per range even when the range is empty, so the shaders' geometry index stays
        // the range's: an empty one is a degenerate triangle.
        if (p->geoms.size() != m.geometries.size() || p->geoms.empty()) return false;
        p->mesh = &m;
        p->info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        p->info.flags = keep ? (VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                                VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
                             : VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        p->info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        p->info.geometryCount = static_cast<std::uint32_t>(p->geoms.size());
        p->info.pGeometries = p->geoms.data();
        VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &p->info,
                                                counts.data(), &sizes);
        p->scratch = (sizes.buildScratchSize + scratchAlign - 1) / scratchAlign * scratchAlign;
        *asSize = sizes.accelerationStructureSize;
        return true;
    }

    void asBarrier(VkCommandBuffer cmd) {
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
                          VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 1, &b, 0, nullptr, 0, nullptr);
    }

    Result<void> build(const RayScene& scene);
    Result<void> createPipelines();
    Result<Image> makeImage(VkCommandBuffer cmd, std::uint32_t w, std::uint32_t h, VkImageLayout layout, float clear);
    Result<void> ensureTargets(VkCommandBuffer cmd, std::uint32_t w, std::uint32_t h);
    Result<Results> trace(const View& view);
};

Result<void> RayTracer::Impl::build(const RayScene& scene) {
    VkCommandBuffer cmd = vulkanNativeCommands(device);
    if (!cmd) return Result<void>::fail("ray tracing: no frame is recording");
    ++frame;
    Slot& s = slots[vulkanFrameSlot(device) % kVulkanFramesInFlight];
    vulkanBeginSpan(device, "rt build");

    // Everything recorded before (the scene's uploads) is visible to the builds.
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &mb, 0, nullptr, 0, nullptr);

    if (!s.queries) {
        VkQueryPoolCreateInfo q{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        q.queryType = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
        q.queryCount = kCompactQueries;
        vkCreateQueryPool(vk.device, &q, nullptr, &s.queries);
        if (s.queries) vkCmdResetQueryPool(cmd, s.queries, 0, kCompactQueries);
    } else {
        compactFinished(cmd, s);
        // The compacted copies are what this frame's top level references.
        asBarrier(cmd);
    }
    if (!whiteReady) {
        auto w = makeImage(cmd, 1, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1.0f);
        if (!w) return Result<void>::fail(w.error());
        white = w.value();
        whiteReady = true;
    }
    // The slot's frame completed: its posed meshes' structures go.
    for (Blas& b : s.transient) destroyBlas(b);
    s.transient.clear();

    stats.built = 0;
    stats.pending = 0;
    stats.triangles = 0;

    // Bottom level: what is kept, what is new within the budget, and the frame's own.
    std::vector<Pending> builds;
    builds.reserve(scene.meshes.size());
    std::vector<VkDeviceAddress> meshAddress(scene.meshes.size(), 0);
    std::vector<std::uint64_t> meshTriangles(scene.meshes.size(), 0);
    VkDeviceSize scratchTotal = 0;
    std::uint32_t fresh = 0;
    s.transient.reserve(scene.meshes.size());
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        const RayMesh& m = scene.meshes[i];
        const VkBuffer vb = vulkanBuffer(device, m.vertices), ib = vulkanBuffer(device, m.indices);
        std::uint64_t tris = 0;
        for (const RayGeometry& g : m.geometries) tris += g.indexCount / 3;
        meshTriangles[i] = tris;
        if (m.key) {
            auto it = kept.find(m.key);
            if (it != kept.end()) {
                Blas& b = it->second;
                if (b.vertices == vb && b.indices == ib && b.vertexCount == m.vertexCount &&
                    b.geometries == m.geometries.size()) {
                    b.lastUsed = frame;
                    meshAddress[i] = b.address;
                    continue;
                }
                stats.blasBytes -= b.buf.size;
                retireBlas(b);
                kept.erase(it);
            }
            if (fresh >= kBuildsPerFrame || scratchTotal > kBuildScratchBudget) continue;
        }
        Pending p;
        VkDeviceSize asSize = 0;
        if (!describe(m, m.key != nullptr, &p, &asSize)) continue;
        Blas b;
        if (auto r = createAs(b, asSize, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR); !r) return r;
        b.vertices = vb;
        b.indices = ib;
        b.vertexCount = m.vertexCount;
        b.geometries = static_cast<std::uint32_t>(m.geometries.size());
        b.triangles = tris;
        b.lastUsed = frame;
        if (m.key) {
            stats.blasBytes += b.buf.size;
            p.target = &kept.emplace(m.key, b).first->second;
            ++fresh;
        } else {
            s.transient.push_back(b);
            p.target = &s.transient.back();
        }
        meshAddress[i] = b.address;
        scratchTotal += p.scratch;
        builds.push_back(std::move(p));
    }

    // Top level: one instance per placed mesh whose structure exists.
    // The shaders' records: each mesh's ranges in order, so a hit's record is its instance's first plus its geometry
    // index; the textures they sample get slots of the sampler array.
    std::vector<std::uint32_t> meshRecord(scene.meshes.size(), 0);
    std::vector<GeoRecord> records;
    std::unordered_map<std::uint32_t, std::uint32_t> texSlot;
    s.textures.assign(1, white.view);
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        const RayMesh& m = scene.meshes[i];
        meshRecord[i] = static_cast<std::uint32_t>(records.size());
        const VkDeviceAddress vb = bufferAddress(m.vertices), ib = bufferAddress(m.indices);
        for (const RayGeometry& g : m.geometries) {
            GeoRecord r{};
            r.vtx[0] = static_cast<std::uint32_t>(vb);
            r.vtx[1] = static_cast<std::uint32_t>(vb >> 32);
            r.idx[0] = static_cast<std::uint32_t>(ib);
            r.idx[1] = static_cast<std::uint32_t>(ib >> 32);
            r.first = g.firstIndex;
            r.flags = g.flags;
            VulkanImageInfo info;
            if ((g.flags & kRayTextured) && g.texture && vulkanImageInfo(device, g.texture, &info)) {
                auto [it, added] = texSlot.try_emplace(g.texture.raw, static_cast<std::uint32_t>(s.textures.size()));
                if (added) {
                    if (s.textures.size() < kMaxTextures) {
                        s.textures.push_back(info.view);
                    } else {
                        it->second = 0;
                    }
                }
                r.tex = it->second;
                if (g.regionCount > 0 && g.regionFirst + g.regionCount <= scene.regions.size())
                    r.regions = g.regionFirst << 8 | std::min<std::uint32_t>(g.regionCount, 255);
            } else {
                r.flags &= ~static_cast<std::uint32_t>(kRayTextured);
            }
            std::memcpy(r.surface, g.surface.data(), sizeof r.surface);
            records.push_back(r);
        }
    }
    const VkDeviceSize recBytes = std::max<std::size_t>(records.size(), 1) * sizeof(GeoRecord);
    if (auto r = ensure(s.geos, recBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true); !r) return r;
    if (!records.empty()) std::memcpy(s.geos.mapped, records.data(), records.size() * sizeof(GeoRecord));
    vmaFlushAllocation(vk.allocator, s.geos.alloc, 0, VK_WHOLE_SIZE);
    const VkDeviceSize regionBytes = std::max<std::size_t>(scene.regions.size(), 1) * sizeof(RayRegion);
    if (auto r = ensure(s.regions, regionBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true); !r) return r;
    if (!scene.regions.empty())
        std::memcpy(s.regions.mapped, scene.regions.data(), scene.regions.size() * sizeof(RayRegion));
    vmaFlushAllocation(vk.allocator, s.regions.alloc, 0, VK_WHOLE_SIZE);

    std::vector<VkAccelerationStructureInstanceKHR> inst;
    inst.reserve(scene.instances.size());
    for (const RayInstance& ri : scene.instances) {
        if (ri.mesh >= meshAddress.size()) continue;
        if (!meshAddress[ri.mesh]) {
            ++stats.pending;
            continue;
        }
        VkAccelerationStructureInstanceKHR in{};
        std::memcpy(in.transform.matrix, ri.transform.data(), sizeof in.transform.matrix);
        in.instanceCustomIndex = meshRecord[ri.mesh] & 0xFFFFFF;
        in.mask = 0xFF;
        in.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        in.accelerationStructureReference = meshAddress[ri.mesh];
        inst.push_back(in);
        stats.triangles += meshTriangles[ri.mesh];
    }
    stats.instances = static_cast<std::uint32_t>(inst.size());
    const VkDeviceSize instBytes = std::max<std::size_t>(inst.size(), 1) * sizeof(VkAccelerationStructureInstanceKHR);
    if (auto r =
            ensure(s.instances, instBytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, true);
        !r)
        return r;
    if (!inst.empty()) std::memcpy(s.instances.mapped, inst.data(), inst.size() * sizeof(inst[0]));
    vmaFlushAllocation(vk.allocator, s.instances.alloc, 0, VK_WHOLE_SIZE);

    VkAccelerationStructureGeometryKHR tgeo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    tgeo.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tgeo.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tgeo.geometry.instances.data.deviceAddress = s.instances.address;
    VkAccelerationStructureBuildGeometryInfoKHR tinfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    tinfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tinfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tinfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tinfo.geometryCount = 1;
    tinfo.pGeometries = &tgeo;
    const std::uint32_t count = static_cast<std::uint32_t>(inst.size());
    VkAccelerationStructureBuildSizesInfoKHR tsize{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tinfo, &count,
                                            &tsize);
    const VkDeviceSize tscratch = (tsize.buildScratchSize + scratchAlign - 1) / scratchAlign * scratchAlign;

    if (auto r = ensure(s.scratch, std::max(scratchTotal, tscratch) + scratchAlign, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        false);
        !r)
        return r;
    stats.scratchBytes = s.scratch.size;
    const VkDeviceAddress scratchBase = (s.scratch.address + scratchAlign - 1) / scratchAlign * scratchAlign;

    if (!builds.empty()) {
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> infos;
        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges;
        VkDeviceSize at = 0;
        for (Pending& p : builds) {
            p.info.pGeometries = p.geoms.data();
            p.info.dstAccelerationStructure = p.target->as;
            p.info.scratchData.deviceAddress = scratchBase + at;
            at += p.scratch;
            infos.push_back(p.info);
            ranges.push_back(p.ranges.data());
        }
        vkCmdBuildAccelerationStructuresKHR(cmd, static_cast<std::uint32_t>(infos.size()), infos.data(), ranges.data());
        stats.built = static_cast<std::uint32_t>(builds.size());
        asBarrier(cmd);
        // Kept structures are compacted: their compacted size is read when this slot comes round again.
        for (Pending& p : builds) {
            if (!p.mesh->key || !s.queries || s.queryCount >= kCompactQueries) continue;
            vkCmdWriteAccelerationStructuresPropertiesKHR(cmd, 1, &p.target->as,
                                                          VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                                                          s.queries, s.queryCount);
            p.target->query = static_cast<int>(s.queryCount);
            p.target->querySlot = static_cast<std::uint32_t>(&s - slots.data());
            s.queried.push_back(p.mesh->key);
            ++s.queryCount;
        }
    }

    if (!s.tlas || s.tlasBuf.size < tsize.accelerationStructureSize) {
        if (s.tlas) vkDestroyAccelerationStructureKHR(vk.device, s.tlas, nullptr);
        freeBuf(s.tlasBuf);
        Blas t;
        if (auto r = createAs(t, tsize.accelerationStructureSize + tsize.accelerationStructureSize / 2 + 4096,
                              VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR);
            !r)
            return r;
        s.tlas = t.as;
        s.tlasBuf = t.buf;
    }
    stats.tlasBytes = s.tlasBuf.size;
    tinfo.dstAccelerationStructure = s.tlas;
    tinfo.scratchData.deviceAddress = scratchBase;
    VkAccelerationStructureBuildRangeInfoKHR trange{};
    trange.primitiveCount = count;
    const VkAccelerationStructureBuildRangeInfoKHR* tranges = &trange;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tinfo, &tranges);
    asBarrier(cmd);

    // Structures of maps left behind.
    for (auto it = kept.begin(); it != kept.end();) {
        if (frame - it->second.lastUsed > kEvictFrames) {
            stats.blasBytes -= it->second.buf.size;
            retireBlas(it->second);
            it = kept.erase(it);
        } else {
            ++it;
        }
    }
    stats.blas = static_cast<std::uint32_t>(kept.size());
    s.built = true;
    vulkanEndSpan(device);
    return Result<void>::ok();
}

namespace {

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
}

// Compute writes made visible to the next dispatch.
void computeBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &b, 0,
                         nullptr, 0, nullptr);
}

}  // namespace

Result<Image> RayTracer::Impl::makeImage(VkCommandBuffer cmd, std::uint32_t w, std::uint32_t h, VkImageLayout layout,
                                         float clear) {
    Image img;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    info.extent = {w, h, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    if (VkResult r = vmaCreateImage(vk.allocator, &info, &ai, &img.image, &img.alloc, nullptr); r != VK_SUCCESS)
        return Result<Image>::fail("ray tracing: image " + std::to_string(w) + "x" + std::to_string(h) + ": error " +
                                   std::to_string(static_cast<int>(r)));
    VkImageViewCreateInfo v{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    v.image = img.image;
    v.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v.format = info.format;
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(vk.device, &v, nullptr, &img.view);
    imageBarrier(cmd, img.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    const VkClearColorValue c{{clear, clear, clear, clear}};
    const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, img.image, VK_IMAGE_LAYOUT_GENERAL, &c, 1, &range);
    imageBarrier(cmd, img.image, VK_IMAGE_LAYOUT_GENERAL, layout);
    return Result<Image>::ok(img);
}

Result<void> RayTracer::Impl::ensureTargets(VkCommandBuffer cmd, std::uint32_t w, std::uint32_t h) {
    if (w == width && h == height) return Result<void>::ok();
    // Released after the frames that may still sample them.
    if (width) {
        vkDeviceWaitIdle(vk.device);
        destroyTargets();
    }
    auto make = [&](Image& i, VkImageLayout layout) -> Result<void> {
        auto r = makeImage(cmd, w, h, layout, 0.0f);
        if (!r) return Result<void>::fail(r.error());
        i = r.value();
        return Result<void>::ok();
    };
    for (Image& i : gbuf)
        if (auto r = make(i, VK_IMAGE_LAYOUT_GENERAL); !r) return r;
    for (auto* set : {&raw, &tmp, &tmp2, &hist[0], &hist[1]})
        for (Image& i : *set)
            if (auto r = make(i, VK_IMAGE_LAYOUT_GENERAL); !r) return r;
    for (std::size_t s = 0; s < kSignals; ++s) {
        if (auto r = make(out[s], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); !r) return r;
        outTex[s] = vulkanAdoptImage(device, out[s].image, out[s].view, VK_FORMAT_R16G16B16A16_SFLOAT, w, h);
    }
    width = w;
    height = h;
    haveHistory = false;
    return Result<void>::ok();
}

Result<void> RayTracer::Impl::createPipelines() {
    const embedded::Shader traceSrc = embedded::rtTraceComp(), denoiseSrc = embedded::rtDenoiseComp();
    if (traceSrc.spirv.empty() || denoiseSrc.spirv.empty())
        return Result<void>::fail("ray tracing: the build has no SPIR-V of its compute shaders");
    auto setLayout = [&](const std::vector<VkDescriptorSetLayoutBinding>& b, VkDescriptorSetLayout* out) {
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = static_cast<std::uint32_t>(b.size());
        ci.pBindings = b.data();
        return vkCreateDescriptorSetLayout(vk.device, &ci, nullptr, out);
    };
    const VkShaderStageFlags cs = VK_SHADER_STAGE_COMPUTE_BIT;
    std::vector<VkDescriptorSetLayoutBinding> tb = {
        {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, cs, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, cs, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, cs, nullptr},
        {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, cs, nullptr},
        {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, cs, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures, cs, nullptr},
    };
    for (std::uint32_t i = 4; i < 4 + 1 + kSignals; ++i)
        tb.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, cs, nullptr});
    std::vector<VkDescriptorSetLayoutBinding> db = {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, cs, nullptr}};
    for (std::uint32_t i = 1; i <= 11; ++i) db.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, cs, nullptr});
    if (setLayout(tb, &traceLayout) != VK_SUCCESS || setLayout(db, &denoiseLayout) != VK_SUCCESS)
        return Result<void>::fail("ray tracing: descriptor set layouts");
    const VkPushConstantRange push{cs, 0, 16};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &traceLayout;
    if (vkCreatePipelineLayout(vk.device, &pl, nullptr, &tracePipeLayout) != VK_SUCCESS)
        return Result<void>::fail("ray tracing: pipeline layout");
    pl.pSetLayouts = &denoiseLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(vk.device, &pl, nullptr, &denoisePipeLayout) != VK_SUCCESS)
        return Result<void>::fail("ray tracing: pipeline layout");
    auto pipeline = [&](std::span<const std::uint32_t> spirv, VkPipelineLayout layout, VkPipeline* out) {
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mi.codeSize = spirv.size_bytes();
        mi.pCode = spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(vk.device, &mi, nullptr, &module) != VK_SUCCESS) return VK_ERROR_INITIALIZATION_FAILED;
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.module = module;
        ci.stage.pName = "main";
        ci.layout = layout;
        const VkResult r = vkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &ci, nullptr, out);
        vkDestroyShaderModule(vk.device, module, nullptr);
        return r;
    };
    if (pipeline(traceSrc.spirv, tracePipeLayout, &tracePipe) != VK_SUCCESS ||
        pipeline(denoiseSrc.spirv, denoisePipeLayout, &denoisePipe) != VK_SUCCESS)
        return Result<void>::fail("ray tracing: the compute pipelines could not be made");
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    si.maxLod = 0.0f;
    if (vkCreateSampler(vk.device, &si, nullptr, &sampler) != VK_SUCCESS)
        return Result<void>::fail("ray tracing: sampler");
    for (Slot& s : slots) {
        const VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 2},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64},
        };
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 8;
        pi.poolSizeCount = static_cast<std::uint32_t>(std::size(sizes));
        pi.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(vk.device, &pi, nullptr, &s.pool) != VK_SUCCESS)
            return Result<void>::fail("ray tracing: descriptor pool");
    }
    return Result<void>::ok();
}

Result<RayTracer::Results> RayTracer::Impl::trace(const View& view) {
    VkCommandBuffer cmd = vulkanNativeCommands(device);
    Slot& s = slots[vulkanFrameSlot(device) % kVulkanFramesInFlight];
    if (!cmd || !s.built || !s.tlas) return Result<Results>::fail("ray tracing: trace without a build");
    s.built = false;
    VulkanImageInfo target;
    if (!vulkanImageInfo(device, view.target, &target)) return Result<Results>::fail("ray tracing: no target");
    if (auto r = ensureTargets(cmd, target.width, target.height); !r) return Result<Results>::fail(r.error());
    const auto vw = static_cast<std::uint32_t>(std::clamp(view.viewport[2], 0.0f, static_cast<float>(width)));
    const auto vh = static_cast<std::uint32_t>(std::clamp(view.viewport[3], 0.0f, static_cast<float>(height)));
    if (!vw || !vh) return Result<Results>::fail("ray tracing: empty viewport");

    // The last frame's view space from this one's: prev * inverse(cur), both rigid.
    const auto& c = view.camera;
    const auto& p = prevCamera;
    double m[12];
    for (int r = 0; r < 3; ++r) {
        double t = p[static_cast<std::size_t>(r * 4 + 3)];
        for (int col = 0; col < 3; ++col) {
            // prev.R * cur.R^T
            double v = 0;
            for (int k = 0; k < 3; ++k)
                v += p[static_cast<std::size_t>(r * 4 + k)] * c[static_cast<std::size_t>(col * 4 + k)];
            m[r * 4 + col] = v;
        }
        for (int k = 0; k < 3; ++k) t -= m[r * 4 + k] * c[static_cast<std::size_t>(k * 4 + 3)];
        m[r * 4 + 3] = t;
    }
    const bool reset = !haveHistory || view.viewport != prevViewport || view.pixelRay != prevPixelRay;

    FrameBlock fb{};
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 4; ++col) fb.prevFromCur[col * 4 + r] = static_cast<float>(m[r * 4 + col]);
    fb.prevFromCur[15] = 1.0f;
    std::memcpy(fb.pixelRay, view.pixelRay.data(), sizeof fb.pixelRay);
    std::memcpy(fb.prevPixelRay, reset ? view.pixelRay.data() : prevPixelRay.data(), sizeof fb.prevPixelRay);
    fb.viewport[0] = view.viewport[0];
    fb.viewport[1] = view.viewport[1];
    fb.viewport[2] = static_cast<float>(vw);
    fb.viewport[3] = static_cast<float>(vh);
    std::memcpy(fb.lens, view.lens.data(), sizeof fb.lens);
    std::memcpy(fb.beamDir, view.beamDir.data(), sizeof fb.beamDir);
    std::memcpy(fb.beam, view.beam.data(), sizeof fb.beam);
    std::memcpy(fb.beamColor, view.beamColor.data(), sizeof fb.beamColor);
    std::memcpy(fb.fog, view.fog.data(), sizeof fb.fog);
    std::memcpy(fb.ambient, view.ambient.data(), sizeof fb.ambient);
    fb.effects[0] = view.shadows ? 1.0f : 0.0f;
    fb.effects[1] = view.reflections ? 1.0f : 0.0f;
    fb.effects[2] = view.gi ? 1.0f : 0.0f;
    const std::size_t lightCount = view.lights ? std::min(view.mapLights.size(), kMaxRayLights) : 0;
    fb.lightInfo[0] = static_cast<float>(lightCount);
    fb.lightInfo[1] = view.lights ? 1.0f : 0.0f;
    std::memcpy(fb.sunDir, view.sunDir.data(), sizeof fb.sunDir);
    std::memcpy(fb.sunColor, view.sunColor.data(), sizeof fb.sunColor);
    std::memcpy(fb.sky, view.sky.data(), sizeof fb.sky);
    const std::size_t ceilings = view.lights ? std::min(view.ceilings.size() / 3, kMaxRayCeilings) : 0;
    fb.ceilingInfo[0] = static_cast<float>(ceilings);
    for (std::size_t i = 0; i < ceilings * 3; ++i)
        std::memcpy(fb.ceilings[i], view.ceilings[i].data(), sizeof fb.ceilings[i]);
    if (!view.lights) {
        std::fill(std::begin(fb.sunDir), std::end(fb.sunDir), 0.0f);
        std::fill(std::begin(fb.sky), std::end(fb.sky), 0.0f);
    }
    std::array<LightRecord, kMaxRayLights> lightRecs{};
    for (std::size_t i = 0; i < lightCount; ++i) {
        const View::Light& l = view.mapLights[i];
        LightRecord& r = lightRecs[i];
        std::memcpy(r.position, l.position.data(), sizeof r.position);
        std::memcpy(r.direction, l.direction.data(), sizeof r.direction);
        std::memcpy(r.color, l.color.data(), sizeof r.color);
        r.radius = l.radius;
        r.nearClip = l.nearClip;
        r.flags = (l.shadows ? 1u : 0u) | (l.gi ? 2u : 0u);
    }
    if (auto r = ensure(s.lights, sizeof lightRecs, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true); !r)
        return Result<Results>::fail(r.error());
    std::memcpy(s.lights.mapped, lightRecs.data(), sizeof lightRecs);
    vmaFlushAllocation(vk.allocator, s.lights.alloc, 0, VK_WHOLE_SIZE);
    fb.effects[3] = static_cast<float>(frame % 65536);
    fb.params[0] = static_cast<float>(view.shadowRays);
    fb.params[1] = view.giStrength;
    fb.params[2] = view.maxRoughness;
    fb.params[3] = view.nearClip;
    std::memcpy(fb.up, view.up.data(), sizeof fb.up);
    std::memcpy(fb.fogParams, view.fogParams.data(), sizeof fb.fogParams);
    std::memcpy(fb.fogRamp, view.fogRamp.data(), sizeof fb.fogRamp);
    if (auto r = ensure(s.frame, sizeof fb, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true); !r)
        return Result<Results>::fail(r.error());
    std::memcpy(s.frame.mapped, &fb, sizeof fb);
    vmaFlushAllocation(vk.allocator, s.frame.alloc, 0, VK_WHOLE_SIZE);

    // Descriptor sets of this frame: the trace and four denoise passes.
    vkResetDescriptorPool(vk.device, s.pool, 0);
    std::array<VkDescriptorSetLayout, 5> layouts{traceLayout, denoiseLayout, denoiseLayout, denoiseLayout,
                                                 denoiseLayout};
    std::array<VkDescriptorSet, 5> sets{};
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = s.pool;
    ai.descriptorSetCount = static_cast<std::uint32_t>(sets.size());
    ai.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(vk.device, &ai, sets.data()) != VK_SUCCESS)
        return Result<Results>::fail("ray tracing: descriptor sets");

    const std::uint32_t cur = parity, prev = parity ^ 1u;
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> images;
    images.reserve(kMaxTextures + 64);
    const VkDescriptorBufferInfo frameInfo{s.frame.buffer, 0, sizeof fb};
    const VkDescriptorBufferInfo geoInfo{s.geos.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo regionInfo{s.regions.buffer, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo lightInfo{s.lights.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSetAccelerationStructureKHR asInfo{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asInfo.accelerationStructureCount = 1;
    asInfo.pAccelerationStructures = &s.tlas;
    auto write = [&](VkDescriptorSet set, std::uint32_t binding, VkDescriptorType type) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        writes.push_back(w);
        return &writes.back();
    };
    writes.reserve(64);
    write(sets[0], 0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)->pNext = &asInfo;
    write(sets[0], 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)->pBufferInfo = &frameInfo;
    write(sets[0], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)->pBufferInfo = &geoInfo;
    write(sets[0], 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)->pBufferInfo = &regionInfo;
    write(sets[0], 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)->pBufferInfo = &lightInfo;
    const std::size_t texBase = images.size();
    for (std::uint32_t i = 0; i < kMaxTextures; ++i)
        images.push_back(
            {sampler, i < s.textures.size() ? s.textures[i] : white.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
    {
        VkWriteDescriptorSet* w = write(sets[0], 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        w->descriptorCount = kMaxTextures;
        w->pImageInfo = &images[texBase];
    }
    auto storage = [&](VkDescriptorSet set, std::uint32_t binding, const Image& img) {
        images.push_back({VK_NULL_HANDLE, img.view, VK_IMAGE_LAYOUT_GENERAL});
        write(set, binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)->pImageInfo = &images.back();
    };
    storage(sets[0], 4, gbuf[cur]);
    for (std::uint32_t i = 0; i < kSignals; ++i) storage(sets[0], 5 + i, raw[i]);
    // Denoise passes: temporal (raw + last history -> history), a-trous 1 (history -> tmp), 2 (tmp -> tmp2),
    // 3 (tmp2 -> the results).
    const std::array<const std::array<Image, kSignals>*, 4> ins{&raw, &hist[cur], &tmp, &tmp2};
    const std::array<const std::array<Image, kSignals>*, 4> outs{&hist[cur], &tmp, &tmp2, &out};
    for (std::size_t pass = 0; pass < 4; ++pass) {
        VkDescriptorSet set = sets[pass + 1];
        write(set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)->pBufferInfo = &frameInfo;
        storage(set, 1, gbuf[cur]);
        storage(set, 2, gbuf[prev]);
        for (std::uint32_t i = 0; i < kSignals; ++i) {
            storage(set, 3 + i, (*ins[pass])[i]);
            storage(set, 6 + i, hist[prev][i]);
            storage(set, 9 + i, (*outs[pass])[i]);
        }
    }
    vkUpdateDescriptorSets(vk.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);

    const std::uint32_t gx = (vw + 7) / 8, gy = (vh + 7) / 8;
    vulkanBeginSpan(device, "rt trace");
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tracePipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, tracePipeLayout, 0, 1, &sets[0], 0, nullptr);
    vkCmdDispatch(cmd, gx, gy, 1);
    vulkanEndSpan(device);
    vulkanBeginSpan(device, "rt denoise");
    for (const Image& o : out)
        imageBarrier(cmd, o.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, denoisePipe);
    for (int pass = 0; pass < 4; ++pass) {
        computeBarrier(cmd);
        const std::int32_t pc[4] = {pass == 0 ? 0 : 1, pass == 0 ? 0 : 1 << (pass - 1), pass == 3 ? 1 : 0,
                                    reset ? 1 : 0};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, denoisePipeLayout, 0, 1, &sets[pass + 1], 0,
                                nullptr);
        vkCmdPushConstants(cmd, denoisePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, pc);
        vkCmdDispatch(cmd, gx, gy, 1);
    }
    for (const Image& o : out)
        imageBarrier(cmd, o.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vulkanEndSpan(device);

    parity ^= 1u;
    haveHistory = true;
    prevCamera = view.camera;
    prevPixelRay = view.pixelRay;
    prevViewport = view.viewport;
    Results res;
    if (view.shadows) res.shadow = outTex[kShadow];
    // The map lights' specular joins the reflections and their diffuse light the bounce.
    if (view.reflections || view.lights) res.reflect = outTex[kReflect];
    if (view.gi || view.lights) res.gi = outTex[kGi];
    return Result<Results>::ok(res);
}

RayTracingSupport rayTracingSupport(Device& device) {
    const VulkanNative* n = vulkanNative(device);
    if (!n) return RayTracingSupport::NotNativeVulkan;
    return n->rayTracing ? RayTracingSupport::Available : RayTracingSupport::NoHardware;
}

void vulkanRayTracingChain(VkPhysicalDevice physical, std::uint32_t apiVersion,
                           const std::vector<VkExtensionProperties>& extensions, VulkanRayTracingSetup* setup,
                           std::vector<const char*>* names, void** chain) {
    setup->supported = false;
    if (apiVersion < VK_API_VERSION_1_2) return;
    for (const char* e : {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME,
                          VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME})
        if (!hasExt(extensions, e)) return;
    VulkanRayTracingSetup query;
    query.f12.pNext = &query.as;
    query.as.pNext = &query.rq;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &query.f12;
    vkGetPhysicalDeviceFeatures2(physical, &f2);
    if (!query.f12.bufferDeviceAddress || !query.f12.shaderSampledImageArrayNonUniformIndexing ||
        !query.f12.runtimeDescriptorArray || !query.f12.scalarBlockLayout || !query.as.accelerationStructure ||
        !query.rq.rayQuery)
        return;
    setup->f12.bufferDeviceAddress = VK_TRUE;
    setup->f12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    setup->f12.runtimeDescriptorArray = VK_TRUE;
    setup->f12.scalarBlockLayout = VK_TRUE;
    setup->as.accelerationStructure = VK_TRUE;
    setup->rq.rayQuery = VK_TRUE;
    setup->f12.pNext = &setup->as;
    setup->as.pNext = &setup->rq;
    setup->rq.pNext = *chain;
    *chain = &setup->f12;
    names->push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    names->push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    names->push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    setup->supported = true;
}

Result<std::unique_ptr<RayTracer>> RayTracer::create(Device& device) {
    if (rayTracingSupport(device) != RayTracingSupport::Available)
        return Result<std::unique_ptr<RayTracer>>::fail("ray tracing: the device has no KHR ray tracing");
    auto impl = std::make_unique<Impl>(device);
    impl->vk = *vulkanNative(device);
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asp{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.pNext = &asp;
    vkGetPhysicalDeviceProperties2(impl->vk.physical, &p2);
    impl->scratchAlign = std::max<VkDeviceSize>(asp.minAccelerationStructureScratchOffsetAlignment, 1);
    if (auto r = impl->createPipelines(); !r) return Result<std::unique_ptr<RayTracer>>::fail(r.error());
    return Result<std::unique_ptr<RayTracer>>::ok(std::unique_ptr<RayTracer>(new RayTracer(std::move(impl))));
}

RayTracer::RayTracer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RayTracer::~RayTracer() = default;

Result<void> RayTracer::build(const RayScene& scene) { return impl_->build(scene); }

const RayTracer::Stats& RayTracer::stats() const { return impl_->stats; }

Result<RayTracer::Results> RayTracer::trace(const View& view) { return impl_->trace(view); }

}  // namespace sh::rhi
