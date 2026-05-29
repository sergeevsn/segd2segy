#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace segd2segy {

struct SegyTraceMeta {
    int tracl = 0;
    int fldr = 0;
    int tracf = 0;
    int cdp = 0;
    int ns = 0;
    int dt_us = 0;
    int channel_set_number = 0;
    int scan_type_number = 0;
};

class SegyWriter {
public:
    explicit SegyWriter(std::string path);

    void write_text_header(const std::string& text);
    void write_binary_header(int sample_interval_us, int samples_per_trace, int format_code = 5);
    void write_trace(const SegyTraceMeta& meta, const std::vector<float>& samples);

    std::uint64_t trace_count() const { return trace_count_; }

private:
    static void write_int16_be(std::uint8_t* dst, std::int16_t value);
    static void write_int32_be(std::uint8_t* dst, std::int32_t value);
    static void write_trace_header(const SegyTraceMeta& meta, std::uint8_t* header);

    std::string path_;
    std::ofstream output_;
    std::uint64_t trace_count_ = 0;
};

}  // namespace segd2segy
