#include "src/webp/decode.h"

#include <cstddef>
#include <cstdint>

extern "C" __declspec(dllexport) int
ArmrWebP103GetDecoderVersion() {
    return WebPGetDecoderVersion();
}

extern "C" __declspec(dllexport) std::uint8_t*
ArmrWebP103DecodeRGBAInto(const std::uint8_t* data,
                           std::size_t data_size,
                           std::uint8_t* output_buffer,
                           std::size_t output_buffer_size,
                           int output_stride) {
    return WebPDecodeRGBAInto(data, data_size, output_buffer,
                              output_buffer_size, output_stride);
}
