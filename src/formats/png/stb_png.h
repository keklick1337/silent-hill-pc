#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// RGBA8 pixels of a PNG in memory, or NULL (the reason is sh_png_stb_error()). Free with sh_png_stb_free.
unsigned char* sh_png_stb_load_rgba(const unsigned char* data, int size, int* width, int* height);
void sh_png_stb_free(unsigned char* pixels);
const char* sh_png_stb_error(void);

#ifdef __cplusplus
}
#endif
