#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>

namespace jpeg {

// Encode RGB data (3 bytes/pixel) into a JPEG byte vector.
// quality = 1..100 (higher = better quality / larger file).
bool encode(const uint8_t* rgb_data, int width, int height, int quality,
            std::vector<uint8_t>& output);

// Decode JPEG bytes back into RGB (3 bytes/pixel).
// width / height are filled from the JPEG metadata.
bool decode(const uint8_t* jpeg_data, size_t jpeg_size,
            std::vector<uint8_t>& rgb_output, int& width, int& height);

}  // namespace jpeg
