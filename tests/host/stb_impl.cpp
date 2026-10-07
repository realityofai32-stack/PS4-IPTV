// stb_image / stb_image_write implementations for the host tests. On the PS4 the same stb_image v2.23
// implementation is compiled into libcross2d (gl_texture.cpp); src/images only includes its declarations.
#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "cross2d/skeleton/stb_image.h"
#include "cross2d/skeleton/stb_image_write.h"
#pragma warning(pop)
