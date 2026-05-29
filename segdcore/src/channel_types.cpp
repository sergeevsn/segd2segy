#include "segdcore/channel_types.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "segdcore/headers.hpp"

namespace segdcore {
namespace {

const char* kLegacyNames[] = {
    "Undefined",
    "Seis",
    "Time break",
    "Up hole",
    "Water break",
    "Time counter",
    "External Data",
    "Other",
    "Signature/unfiltered",
    "Signature/filtered",
    "Auxiliary Data Trailer",
};

const char* kRev3Names[] = {
    "Unused",
    "Seis",
    "Electromagnetic (EM)",
    "Time break",
    "Clock timebreak",
    "Field timebreak",
    "Up hole",
    "Water break",
    "Timecounter",
    "External Data",
};

}  // namespace

std::string channel_type_name(int channel_type, int revision_major) {
    if (revision_major >= 3) {
        const int index = channel_type >> 4;
        if (index >= 0 && index < static_cast<int>(sizeof(kRev3Names) / sizeof(kRev3Names[0]))) {
            return kRev3Names[index];
        }
        return "User defined";
    }
    if (channel_type >= 0 && channel_type < static_cast<int>(sizeof(kLegacyNames) / sizeof(kLegacyNames[0]))) {
        return kLegacyNames[channel_type];
    }
    return "User defined";
}

bool is_default_seismic_channel_type(int channel_type, int revision_major) {
    if (revision_major >= 3) {
        return channel_type == 0x10;
    }
    return channel_type == 1;
}

void ChannelFilter::begin_file(const std::vector<ChannelSet>& channel_sets) {
    keep_channel_set_number_ = -1;
    if (!skip_service) {
        return;
    }
    for (const ChannelSet& channel_set : channel_sets) {
        keep_channel_set_number_ = std::max(keep_channel_set_number_, channel_set.channel_set_number);
    }
}

bool ChannelFilter::include_channel_set(
    int channel_set_number,
    int channel_type,
    int revision_major) const {
    if (!include_types.empty()) {
        return include_types.count(channel_type) > 0;
    }
    if (exclude_types.count(channel_type) > 0) {
        return false;
    }
    if (skip_service) {
        (void)revision_major;
        return keep_channel_set_number_ >= 0 && channel_set_number == keep_channel_set_number_;
    }
    return true;
}

std::unordered_set<int> parse_channel_type_list(const std::string& text) {
    std::unordered_set<int> values;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        auto start = token.find_first_not_of(" \t");
        if (start == std::string::npos) {
            continue;
        }
        auto end = token.find_last_not_of(" \t");
        token = token.substr(start, end - start + 1);
        int base = 10;
        if (token.size() > 2 && (token[0] == '0') && (token[1] == 'x' || token[1] == 'X')) {
            base = 16;
        }
        values.insert(static_cast<int>(std::stoul(token, nullptr, base)));
    }
    return values;
}

}  // namespace segdcore
