/* Renderer queries and video-memory helpers (render_queries.cpp, frame_driver.cpp): the frame's pixel aspect and
 * widescreen factors, the scene scratch redirect, a direct video-memory write sent up, a 1:1 textured rect
 * rasterised straight into video memory, the picture rect and video-memory reads and writes. */
#ifndef SH_RENDER_GPU_RENDER_QUERIES_H
#define SH_RENDER_GPU_RENDER_QUERIES_H

#ifdef __cplusplus
extern "C" {
#endif

float GR_LivePixelAspect(void);
int GR_HorPlusHalfWidths(float* out43, float* outWide);
float GR_Ui2dWideFactor(void);
void GR_SetSceneFbRedirect(int x, int y, int w, int h);
void GR_DirectUploadVRAMRegion(int x, int y, int w, int h);

#ifdef __cplusplus
}

namespace sh::gpu {
// A window pixel as a 0..1 fraction of the picture rect; false when it falls in the bars.
bool window_to_picture(int mx, int my, float* frac_x, float* frac_y);
// A window position in the pixels of the 2D overlay's target (top-left origin), the space overlay::Frame is in.
void window_to_overlay(int mx, int my, float* x, float* y);
// The picture rect in render pixels (bottom-left origin, as the overlays lay out against it).
void picture_viewport(int* x, int* y, int* w, int* h);
// Video memory words of a rect (w * h, top row first).
void read_video_memory(unsigned short* dst, int x, int y, int w, int h);
}  // namespace sh::gpu
#endif

#endif
