#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace segdcore {

std::string hex_nibbles(const std::uint8_t* buf, std::size_t len, int pos, int count);
int bcd_int(const std::uint8_t* buf, std::size_t len, int pos, int count);
/// Parse nibble field as a decimal integer (matches Python int(hex_nibbles(...))).
int int_from_nibbles(const std::uint8_t* buf, std::size_t len, int pos, int count);
std::string bcd_str(const std::uint8_t* buf, std::size_t len, int pos, int count);
int uint_item(const std::uint8_t* buf, std::size_t len, int pos, int count);
int sint_item(const std::uint8_t* buf, std::size_t len, int pos, int count);
float float_item(const std::uint8_t* buf, std::size_t len, int pos, int count);

int sample_bits(int format_code);
bool is_supported_format(int format_code);
int sample_byte_count(int format_code, int sample_count);

}  // namespace segdcore
