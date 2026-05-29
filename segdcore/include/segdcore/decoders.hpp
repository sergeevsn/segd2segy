#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace segdcore {

std::vector<float> decode_samples(
    int format_code,
    const std::uint8_t* data,
    std::size_t data_len,
    int count,
    double multiplier_power = 0.0);

}  // namespace segdcore
