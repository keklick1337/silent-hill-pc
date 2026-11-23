// The render state objects (render_state.h); the shipped defaults are the member initialisers.
#include "render/gpu/render_state.h"

namespace sh::render::detail {

GraphicsSettings g_graphics;
PictureShape g_picture;
FrameState g_frame_state;
PublishedView g_published;
FeedbackControl g_feedback_control;
Flashlight g_flashlight;
Fog g_fog;
PreciseVertices g_precise;
WholeTownView g_whole_town;
RenderDebug g_debug;

}  // namespace sh::render::detail
