#include "segdcore/decoders.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "segdcore/exception.hpp"

namespace segdcore {
namespace {

using DecoderFn = std::vector<float> (*)(const std::uint8_t*, std::size_t, int);

std::uint16_t read_u16_be(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(p[0]) << 8) | p[1]);
}

std::int32_t read_i32_be(const std::uint8_t* p) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, p, 4);
    raw = ((raw & 0x000000FFu) << 24) | ((raw & 0x0000FF00u) << 8) | ((raw & 0x00FF0000u) >> 8) |
          ((raw & 0xFF000000u) >> 24);
    return static_cast<std::int32_t>(raw);
}

float read_f32_be(const std::uint8_t* p) {
    std::uint32_t raw = 0;
    std::memcpy(&raw, p, 4);
    raw = ((raw & 0x000000FFu) << 24) | ((raw & 0x0000FF00u) << 8) | ((raw & 0x00FF0000u) >> 8) |
          ((raw & 0xFF000000u) >> 24);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

float read_f32_le(const std::uint8_t* p) {
    float value = 0.0f;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

std::vector<float> decode_8015(const std::uint8_t* data, std::size_t data_len, int count) {
    std::vector<float> out(static_cast<std::size_t>(count));
    std::size_t offset = 0;
    int written = 0;
    while (written < count) {
        if (offset + 10 > data_len) {
            throw std::runtime_error("Not enough bytes for 8015 samples");
        }
        const int exponents = read_u16_be(data + offset);
        offset += 2;
        for (int shift : {12, 8, 4, 0}) {
            if (written >= count) {
                break;
            }
            const int exponent = ((exponents >> shift) & 0x0F) - 15;
            std::int16_t fraction = static_cast<std::int16_t>(read_u16_be(data + offset));
            offset += 2;
            if (fraction < 0) {
                fraction = static_cast<std::int16_t>(-(~fraction));
            }
            out[static_cast<std::size_t>(written++)] =
                static_cast<float>(std::ldexp(static_cast<double>(fraction), exponent));
        }
    }
    return out;
}

std::vector<float> decode_8022(const std::uint8_t* data, std::size_t, int count) {
    std::vector<float> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int value = data[i];
        const int exponent = ((value >> 3) & 14) - 4;
        int fraction = value & 15;
        if (value & 128) {
            fraction = -(15 ^ fraction);
        }
        out[static_cast<std::size_t>(i)] = static_cast<float>(std::ldexp(static_cast<double>(fraction), exponent));
    }
    return out;
}

std::vector<float> decode_8024(const std::uint8_t* data, std::size_t, int count) {
    std::vector<float> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int value = read_u16_be(data + i * 2);
        const int exponent = ((value >> 11) & 14) - 12;
        int fraction = value & 4095;
        if (value & 32768) {
            fraction = -(4095 ^ fraction);
        }
        out[static_cast<std::size_t>(i)] = static_cast<float>(std::ldexp(static_cast<double>(fraction), exponent));
    }
    return out;
}

std::vector<float> decode_int24(const std::uint8_t* data, std::size_t, int count, bool little_endian) {
    std::vector<float> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::uint8_t* p = data + i * 3;
        int value = 0;
        if (little_endian) {
            value = (p[2] << 16) | (p[1] << 8) | p[0];
        } else {
            value = (p[0] << 16) | (p[1] << 8) | p[2];
        }
        if (value & 0x800000) {
            value -= 0x1000000;
        }
        out[static_cast<std::size_t>(i)] = static_cast<float>(value);
    }
    return out;
}

std::vector<float> decode_8048(const std::uint8_t* data, std::size_t, int count) {
    std::vector<float> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int ex1_4 = read_u16_be(data + i * 4);
        const int expo = ((ex1_4 >> 6) & 508) - (24 + 256);
        long long fraction = ex1_4 & 255;
        fraction <<= 16;
        fraction |= (read_u16_be(data + i * 4 + 2) & 65535);
        if (ex1_4 & 32768) {
            fraction = -fraction;
        }
        out[static_cast<std::size_t>(i)] = static_cast<float>(std::ldexp(static_cast<double>(fraction), expo));
    }
    return out;
}

std::vector<float> decode_ieee_float(const std::uint8_t* data, std::size_t, int count, bool little_endian) {
    std::vector<float> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        out[static_cast<std::size_t>(i)] =
            little_endian ? read_f32_le(data + i * 4) : read_f32_be(data + i * 4);
    }
    return out;
}

std::vector<float> decode_8080(const std::uint8_t* data, std::size_t, int count, bool little_endian) {
    std::vector<float> out(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::uint8_t* p = data + i * 8;
        std::array<std::uint8_t, 8> bytes{};
        if (little_endian) {
            std::memcpy(bytes.data(), p, 8);
        } else {
            for (int b = 0; b < 8; ++b) {
                bytes[static_cast<std::size_t>(b)] = p[7 - b];
            }
        }
        double value = 0.0;
        std::memcpy(&value, bytes.data(), sizeof(value));
        out[static_cast<std::size_t>(i)] = static_cast<float>(value);
    }
    return out;
}

DecoderFn decoder_for(int format_code) {
    switch (format_code) {
        case 8015:
            return decode_8015;
        case 8022:
            return decode_8022;
        case 8024:
            return decode_8024;
        case 8036:
            return [](const std::uint8_t* d, std::size_t n, int c) { return decode_int24(d, n, c, false); };
        case 9036:
            return [](const std::uint8_t* d, std::size_t n, int c) { return decode_int24(d, n, c, true); };
        case 8038:
            return [](const std::uint8_t* d, std::size_t n, int c) {
                std::vector<float> out(static_cast<std::size_t>(c));
                for (int i = 0; i < c; ++i) {
                    out[static_cast<std::size_t>(i)] = static_cast<float>(read_i32_be(d + i * 4));
                }
                return out;
            };
        case 9038:
            return [](const std::uint8_t* d, std::size_t, int c) {
                std::vector<float> out(static_cast<std::size_t>(c));
                for (int i = 0; i < c; ++i) {
                    std::int32_t value = 0;
                    std::memcpy(&value, d + i * 4, 4);
                    out[static_cast<std::size_t>(i)] = static_cast<float>(value);
                }
                return out;
            };
        case 8042:
            return decode_8022;
        case 8044:
            return decode_8024;
        case 8048:
            return decode_8048;
        case 8058:
            return [](const std::uint8_t* d, std::size_t n, int c) { return decode_ieee_float(d, n, c, false); };
        case 9058:
            return [](const std::uint8_t* d, std::size_t n, int c) { return decode_ieee_float(d, n, c, true); };
        case 8080:
            return [](const std::uint8_t* d, std::size_t n, int c) { return decode_8080(d, n, c, false); };
        case 9080:
            return [](const std::uint8_t* d, std::size_t n, int c) { return decode_8080(d, n, c, true); };
        default:
            return nullptr;
    }
}

}  // namespace

std::vector<float> decode_samples(
    int format_code,
    const std::uint8_t* data,
    std::size_t data_len,
    int count,
    double multiplier_power) {
    const DecoderFn decoder = decoder_for(format_code);
    if (!decoder) {
        throw UnsupportedFormatError("Sample format " + std::to_string(format_code) + " is not implemented");
    }
    std::vector<float> samples = decoder(data, data_len, count);
    if (multiplier_power != 0.0) {
        const float scale = static_cast<float>(std::pow(2.0, multiplier_power));
        for (float& sample : samples) {
            sample *= scale;
        }
    }
    return samples;
}

}  // namespace segdcore
