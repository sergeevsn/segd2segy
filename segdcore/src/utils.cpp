#include "segdcore/utils.hpp"

#include "segdcore/exception.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace segdcore {
namespace {

int nibble(const std::uint8_t* buf, int nibble_index) {
    const int byte_index = nibble_index / 2;
    const std::uint8_t byte_value = buf[byte_index];
    if (nibble_index % 2 == 0) {
        return byte_value >> 4;
    }
    return byte_value & 0x0F;
}

}  // namespace

std::string hex_nibbles(const std::uint8_t* buf, std::size_t len, int pos, int count) {
    std::string out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const int nibble_index = pos - 1 + i;
        const int byte_index = nibble_index / 2;
        if (byte_index < 0 || static_cast<std::size_t>(byte_index) >= len) {
            throw std::out_of_range("hex_nibbles out of range");
        }
        const int value = nibble(buf, nibble_index);
        out.push_back("0123456789ABCDEF"[value]);
    }
    return out;
}

int bcd_int(const std::uint8_t* buf, std::size_t len, int pos, int count) {
    const std::string text = hex_nibbles(buf, len, pos, count);
    if (text.empty()) {
        return 0;
    }
    if (text.front() == 'F' || is_all_sentinel_f(text)) {
        return -1;
    }
    return parse_nibble_text(text);
}

int int_from_nibbles(const std::uint8_t* buf, std::size_t len, int pos, int count) {
    const std::string text = hex_nibbles(buf, len, pos, count);
    if (text.empty()) {
        return 0;
    }
    return int_from_hex_text(text, 0);
}

int int_from_hex_text(const std::string& text, int default_value) {
    if (text.empty() || is_all_sentinel_f(text)) {
        return default_value;
    }
    return parse_nibble_text(text);
}

std::string bcd_str(const std::uint8_t* buf, std::size_t len, int pos, int count) {
    return hex_nibbles(buf, len, pos, count);
}

int uint_item(const std::uint8_t* buf, std::size_t len, int pos, int count) {
    const std::string text = hex_nibbles(buf, len, pos, count);
    if (text.empty()) {
        return 0;
    }
    return static_cast<int>(std::stoul(text, nullptr, 16));
}

int sint_item(const std::uint8_t* buf, std::size_t len, int pos, int count) {
    int value = uint_item(buf, len, pos, count);
    const int bits = count * 4;
    const int sign_bit = 1 << (bits - 1);
    if (value & sign_bit) {
        value -= (1 << bits);
    }
    return value;
}

float float_item(const std::uint8_t* buf, std::size_t len, int pos, int count) {
    const unsigned raw = static_cast<unsigned>(uint_item(buf, len, pos, count));
    if (count == 8) {
        const std::uint32_t word = static_cast<std::uint32_t>(raw);
        float value = 0.0f;
        std::memcpy(&value, &word, sizeof(value));
        return value;
    }
    if (count == 16) {
        std::array<std::uint8_t, 8> bytes{};
        for (int i = 0; i < 8; ++i) {
            bytes[7 - i] = static_cast<std::uint8_t>((raw >> (i * 8)) & 0xFF);
        }
        double value = 0.0;
        std::memcpy(&value, bytes.data(), sizeof(value));
        return static_cast<float>(value);
    }
    throw std::invalid_argument("Unsupported float nibble width");
}

int sample_bits(int format_code) {
    switch (format_code) {
        case 15:
        case 8015:
            return 20;
        case 22:
        case 8022:
        case 42:
        case 8042:
            return 8;
        case 24:
        case 8024:
        case 44:
        case 8044:
            return 16;
        case 36:
        case 8036:
        case 9036:
            return 24;
        case 38:
        case 8038:
        case 9038:
            return 32;
        case 48:
        case 8048:
        case 58:
        case 8058:
        case 9058:
            return 32;
        case 80:
        case 8080:
        case 9080:
            return 64;
        default:
            return 0;
    }
}

bool is_supported_format(int format_code) {
    return sample_bits(format_code) > 0 || format_code == 200;
}

int sample_byte_count(int format_code, int sample_count) {
    const int bits = sample_bits(format_code);
    return static_cast<int>((static_cast<long long>(sample_count) * bits) / 8);
}

bool is_all_sentinel_f(const std::string& text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](unsigned char c) { return c == 'F'; });
}

int parse_nibble_text(const std::string& text) {
    try {
        return std::stoi(text, nullptr, 10);
    } catch (const std::exception&) {
    }
    try {
        return static_cast<int>(std::stoul(text, nullptr, 16));
    } catch (const std::exception&) {
        throw SegdFormatError("Invalid numeric field in SEG-D header: \"" + text + "\"");
    }
}

}  // namespace segdcore
