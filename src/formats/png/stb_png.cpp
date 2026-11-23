// stb_image for PNG only, with every symbol static: the one copy of it in the build.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb/include/stb_image.h"

#include "stb_png.h"

unsigned char* sh_png_stb_load_rgba(const unsigned char* data, int size, int* width, int* height) {
    int channels_in_file = 0;
    return stbi_load_from_memory(data, size, width, height, &channels_in_file, 4);
}

void sh_png_stb_free(unsigned char* pixels) {
    stbi_image_free(pixels);
}

const char* sh_png_stb_error(void) {
    const char* reason = stbi_failure_reason();
    return reason ? reason : "unknown error";
}
