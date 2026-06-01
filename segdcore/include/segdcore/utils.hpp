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
/// Parse hex_nibbles text as base-10 int; all-F fields (FF, FFFF, …) return default_value.
int int_from_hex_text(const std::string& text, int default_value = 0);
bool is_all_sentinel_f(const std::string& text);
int parse_nibble_text(const std::string& text);
std::string bcd_str(const std::uint8_t* buf, std::size_t len, int pos, int count);
int uint_item(const std::uint8_t* buf, std::size_t len, int pos, int count);
int sint_item(const std::uint8_t* buf, std::size_t len, int pos, int count);
float float_item(const std::uint8_t* buf, std::size_t len, int pos, int count);

int sample_bits(int format_code);
bool is_supported_format(int format_code);
int sample_byte_count(int format_code, int sample_count);

}  // namespace segdcore
