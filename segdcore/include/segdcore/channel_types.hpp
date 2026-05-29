#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace segdcore {

/// Human-readable SEG-D channel type name (legacy or Rev.3).
std::string channel_type_name(int channel_type, int revision_major);

/// Default seismic channel types: legacy type 1, Rev.3 type 0x10.
bool is_default_seismic_channel_type(int channel_type, int revision_major);

struct ChannelSet;

struct ChannelFilter {
    bool skip_service = false;
    std::unordered_set<int> include_types;
    std::unordered_set<int> exclude_types;

    /// When skip_service is enabled, keep only the highest channel set number in the current file.
    void begin_file(const std::vector<ChannelSet>& channel_sets);

    int keep_channel_set_number() const { return keep_channel_set_number_; }

    bool include_channel_set(int channel_set_number, int channel_type, int revision_major) const;

private:
    int keep_channel_set_number_ = -1;
};

/// Parse "1,16,0x10" into integer channel type codes.
std::unordered_set<int> parse_channel_type_list(const std::string& text);

}  // namespace segdcore
