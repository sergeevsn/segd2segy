#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "segdcore/headers.hpp"

namespace segdcore {

class SegdFile {
public:
    SegdFile() = default;
    explicit SegdFile(std::vector<std::uint8_t> data, std::string source = {});

    static SegdFile open(const std::string& path, bool headers_only = false);
    static SegdFile from_bytes(std::vector<std::uint8_t> data, bool headers_only = false);

    const GeneralHeader& general() const { return general_; }
    const std::vector<ChannelSet>& channel_sets() const { return channel_sets_; }
    const std::vector<Trace>& traces() const { return traces_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    const std::string& source() const { return source_; }

    std::vector<float> read_samples(const Trace& trace) const;

private:
    void parse(bool headers_only);

    std::vector<std::uint8_t> data_;
    std::string source_;
    GeneralHeader general_;
    std::vector<ChannelSet> channel_sets_;
    std::vector<Trace> traces_;
    std::vector<std::string> warnings_;
};

}  // namespace segdcore
