// Use exactly the stb decoder and forced RGBA output used by utils::image.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    std::vector<unsigned char> pixels[2];
    int widths[2]{}, heights[2]{};
    for (int i = 0; i < 2; ++i)
    {
        std::ifstream file(argv[i + 1], std::ios::binary);
        const std::vector<unsigned char> data{std::istreambuf_iterator<char>(file), {}};
        int channels{};
        auto* rgba = stbi_load_from_memory(data.data(), static_cast<int>(data.size()),
            &widths[i], &heights[i], &channels, 4);
        if (!rgba) return 3;
        pixels[i].assign(rgba, rgba + static_cast<size_t>(widths[i]) * heights[i] * 4);
        stbi_image_free(rgba);
    }
    if (widths[0] != widths[1] || heights[0] != heights[1] || pixels[0] != pixels[1]) return 1;
    std::printf("PASS: native stb RGBA parity, %dx%d, %zu bytes\n", widths[0], heights[0], pixels[0].size());
}
