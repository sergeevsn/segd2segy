#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace segdcore {

struct GeneralHeader {
    int offset = 0;
    int file_number = -1;
    int format_code = 0;
    int sample_bits = 0;
    double sample_interval_ms = 0.0;
    double record_length_ms = 0.0;
    int general_header_blocks = 0;
    int scan_types = 0;
    int channel_sets_per_scan_type = 0;
    int skew_blocks = 0;
    int extended_header_blocks = 0;
    int external_header_blocks = 0;
    int manufacturer_code = 0;
    int manufacturer_serial = 0;
    int revision_major = 0;
    int revision_minor = 0;
    int extended_general_header_blocks = 0;
    int source_point = 0;

    bool is_demultiplexed() const { return format_code / 100 > 0; }
};

struct ChannelSet {
    int index = 0;
    int scan_type_number = 0;
    int channel_set_number = 0;
    int channel_type_number = 0;
    int channel_count = 0;
    int sample_skew_per_scan = 1;
    double start_time_ms = 0.0;
    double end_time_ms = 0.0;
    double sample_interval_ms = 0.0;
    int sample_count = 0;
    int trace_header_extensions = 0;
    int trace_length_bytes = 0;
    double multiplier_power = 0.0;
};

struct TraceHeader {
    int offset = 0;
    int file_number = -1;
    int scan_type_number = 0;
    int channel_set_number = 0;
    int trace_number = -1;
    int extended_header_count = 0;
    int sample_count = 0;
    int sample_offset = 0;
    int sample_byte_count = 0;
};

struct Trace {
    TraceHeader header;
    const ChannelSet* channel_set = nullptr;
    int format_code = 0;
    const std::uint8_t* buffer = nullptr;
    std::size_t buffer_size = 0;
};

}  // namespace segdcore
