#include "segdcore/reader.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <optional>
#include <tuple>
#include <utility>

#include "segdcore/decoders.hpp"
#include "segdcore/exception.hpp"
#include "segdcore/utils.hpp"

namespace segdcore {
namespace {

const std::uint8_t* ptr(const std::vector<std::uint8_t>& data) {
    return data.data();
}

std::size_t len(const std::vector<std::uint8_t>& data) {
    return data.size();
}

bool is_valid_general_header_at(const std::uint8_t* buf, std::size_t buflen, int offset) {
    if (offset < 0 || static_cast<std::size_t>(offset) + 32 > buflen) {
        return false;
    }
    const std::uint8_t* block = buf + static_cast<std::size_t>(offset);
    const std::size_t block_len = buflen - static_cast<std::size_t>(offset);
    const int format_code = demux_format_from_nibbles(block, block_len);
    if (format_code == 200) {
        return true;
    }
    return is_supported_format(format_code);
}

void throw_unsupported_segd_error(const std::uint8_t* buf, std::size_t buflen) {
    std::string message = "Input does not look like a supported SEG-D record";
    for (int offset : {0, 32, 128}) {
        if (static_cast<std::size_t>(offset) + 32 > buflen) {
            continue;
        }
        const std::uint8_t* block = buf + static_cast<std::size_t>(offset);
        const std::size_t block_len = buflen - static_cast<std::size_t>(offset);
        const std::string format_nibbles = hex_nibbles(block, block_len, 5, 4);
        const int format_code = demux_format_from_nibbles(block, block_len);
        message += "\n  offset " + std::to_string(offset) + ": format nibbles '" + format_nibbles + "'";
        if (format_code > 0) {
            message += " (code " + std::to_string(format_code) + ")";
        }
    }
    throw SegdFormatError(message);
}

int detect_header_offset(const std::uint8_t* buf, std::size_t buflen) {
    if (buflen < 32) {
        throw SegdFormatError("Input is too short for a SEG-D general header");
    }

    // Demux SEG-D general header block #1 normally starts at file offset 0.
    if (is_valid_general_header_at(buf, buflen, 0)) {
        return 0;
    }

    if (buflen >= 160 && std::memcmp(buf + 4, "SD2.", 4) == 0) {
        if (is_valid_general_header_at(buf, buflen, 128)) {
            return 128;
        }
    }
    if (buflen >= 160 && std::memcmp(buf + 4, "SD3.", 4) == 0) {
        if (is_valid_general_header_at(buf, buflen, 128)) {
            return 128;
        }
    }
    if (buflen >= 288 && std::memcmp(buf + 132, "SD2.", 4) == 0) {
        if (is_valid_general_header_at(buf, buflen, 128)) {
            return 128;
        }
    }
    if (buflen >= 288 && std::memcmp(buf + 132, "SD3.", 4) == 0) {
        if (is_valid_general_header_at(buf, buflen, 128)) {
            return 128;
        }
    }
    if (buflen >= 64 && std::memcmp(buf, "SS36", 4) == 0) {
        if (is_valid_general_header_at(buf, buflen, 32)) {
            return 32;
        }
    }

    throw_unsupported_segd_error(buf, buflen);
    return 0;
}

const ChannelSet* find_channel_set(
    const std::vector<ChannelSet>& channel_sets,
    int scan_type_number,
    int channel_set_number) {
    for (const ChannelSet& channel_set : channel_sets) {
        if (channel_set.scan_type_number == scan_type_number &&
            channel_set.channel_set_number == channel_set_number) {
            return &channel_set;
        }
    }
    return nullptr;
}

ChannelSet parse_channel_set_legacy(const std::uint8_t* block, int index, const GeneralHeader& general) {
    ChannelSet channel_set;
    channel_set.index = index;
    channel_set.scan_type_number = int_from_nibbles(block, 32, 1, 2);
    const std::string cs_text = hex_nibbles(block, 32, 3, 2);
    channel_set.channel_set_number = (cs_text == "FF") ? -1 : int_from_hex_text(cs_text, -1);
    if (channel_set.channel_set_number <= 0) {
        channel_set.channel_set_number = uint_item(block, 32, 53, 4);
    }
    const int csse = int_from_nibbles(block, 32, 23, 1);
    channel_set.sample_skew_per_scan = csse < 1 ? 1 : (1 << csse);
    channel_set.start_time_ms = uint_item(block, 32, 5, 4) * 2.0;
    channel_set.end_time_ms = uint_item(block, 32, 9, 4) * 2.0;
    const double duration = std::max(channel_set.end_time_ms - channel_set.start_time_ms, 0.0);
    const int nominal_samples =
        general.sample_interval_ms > 0
            ? static_cast<int>(duration / general.sample_interval_ms) * channel_set.sample_skew_per_scan
            : 0;
    const std::uint8_t imp0 = block[6];
    const std::uint8_t imp1 = block[7];
    const int sign = (imp1 & 0x80) ? -1 : 1;
    channel_set.multiplier_power =
        sign * static_cast<double>(((imp1 & 0x7F) << 8) | imp0) / 1024.0;
    channel_set.channel_type_number = int_from_nibbles(block, 32, 21, 1);
    channel_set.channel_count = int_from_nibbles(block, 32, 17, 4);
    channel_set.sample_interval_ms =
        channel_set.sample_skew_per_scan > 0
            ? general.sample_interval_ms / channel_set.sample_skew_per_scan
            : general.sample_interval_ms;
    channel_set.sample_count = nominal_samples <= 0 ? -1 : nominal_samples;
    return channel_set;
}

ChannelSet parse_channel_set_rev3(const std::uint8_t* block, int index, const GeneralHeader& general) {
    (void)general;
    ChannelSet channel_set;
    channel_set.index = index;
    channel_set.scan_type_number = int_from_nibbles(block, 96, 1, 2);
    channel_set.channel_set_number = uint_item(block, 96, 3, 4);
    channel_set.channel_type_number = uint_item(block, 96, 7, 2);
    channel_set.channel_count = uint_item(block, 96, 41, 6);
    channel_set.sample_skew_per_scan = 1;
    channel_set.sample_count = uint_item(block, 96, 25, 8);
    channel_set.trace_header_extensions = uint_item(block, 96, 55, 2);
    channel_set.sample_interval_ms = uint_item(block, 96, 47, 6) / 1000.0;
    channel_set.start_time_ms = sint_item(block, 96, 9, 8) / 1000.0;
    channel_set.end_time_ms = sint_item(block, 96, 17, 8) / 1000.0;
    if (channel_set.start_time_ms == 0.0 && channel_set.end_time_ms == 0.0) {
        channel_set.end_time_ms = channel_set.sample_interval_ms * channel_set.sample_count;
    }
    channel_set.multiplier_power = float_item(block, 96, 33, 8);
    channel_set.trace_length_bytes =
        sample_byte_count(general.format_code, channel_set.sample_count) + 20 +
        32 * channel_set.trace_header_extensions;
    return channel_set;
}

std::pair<GeneralHeader, int> parse_general_headers(const std::uint8_t* buf, std::size_t buflen, int offset) {
    GeneralHeader general;
    general.offset = offset;
    if (static_cast<std::size_t>(offset + 32) > buflen) {
        throw SegdFormatError("Unexpected end of SEG-D buffer");
    }
    const std::uint8_t* block1 = buf + offset;

    const std::string file_text = hex_nibbles(block1, 32, 1, 4);
    general.file_number = (file_text == "FFFF") ? -1 : int_from_hex_text(file_text, -1);
    general.format_code = demux_format_from_nibbles(block1, 32);
    general.sample_bits = sample_bits(general.format_code);
    if (general.sample_bits == 0 && general.format_code != 200) {
        throw SegdFormatError("Unknown SEG-D sample format");
    }

    const std::string gh_text = hex_nibbles(block1, 32, 23, 1);
    general.general_header_blocks = (gh_text == "F") ? -1 : bcd_int(block1, 32, 23, 1);
    const std::string record_text = hex_nibbles(block1, 32, 52, 3);
    general.record_length_ms =
        (record_text == "FFF") ? -1.0 : static_cast<double>(int_from_hex_text(record_text, 0)) * 0.1024 * 1000.0;
    general.scan_types = int_from_nibbles(block1, 32, 55, 2);
    const std::string cs_text = hex_nibbles(block1, 32, 57, 2);
    general.channel_sets_per_scan_type = (cs_text == "FF") ? -1 : int_from_hex_text(cs_text, -1);
    std::string ec_text = hex_nibbles(block1, 32, 61, 2);
    std::string ex_text = hex_nibbles(block1, 32, 63, 2);

    int cursor = offset + 32;
    if (general.general_header_blocks != 0) {
        const std::uint8_t* block2 = buf + cursor;
        const int revision = uint_item(block2, 32, 21, 4);
        general.revision_major = revision >> 8;
        general.revision_minor = revision & 0xFF;
        if (general.file_number < 0) {
            general.file_number = uint_item(block2, 32, 1, 6);
        }
        if (general.channel_sets_per_scan_type < 0) {
            general.channel_sets_per_scan_type = uint_item(block2, 32, 7, 4);
        }
        if (ec_text == "FF") {
            ec_text = std::to_string(uint_item(block2, 32, 11, general.revision_major == 3 ? 6 : 4));
        }
        if (ex_text == "FF") {
            const int ex_pos = general.revision_major == 3 ? 55 : 15;
            const int ex_count = general.revision_major == 3 ? 6 : 4;
            ex_text = std::to_string(uint_item(block2, 32, ex_pos, ex_count));
        }
        if (general.record_length_ms < 0) {
            const int r_pos = general.revision_major == 3 ? 33 : 29;
            const int r_count = general.revision_major == 3 ? 8 : 6;
            general.record_length_ms = static_cast<double>(uint_item(block2, 32, r_pos, r_count));
        }
        if (general.revision_major == 3) {
            general.extended_general_header_blocks = uint_item(block2, 32, 45, 4);
        }
        cursor += 32;
    }

    int n_general = general.general_header_blocks;
    if (general.revision_major == 3 && n_general < 0) {
        n_general = general.extended_general_header_blocks;
    }
    if (n_general < 1) {
        n_general = 1;
    }
    if (n_general >= 3) {
        if (general.revision_major < 3) {
            general.source_point = uint_item(buf + cursor, 32, 17, 6);
        }
        cursor += 32;
    }
    for (int block = 3; block < n_general; ++block) {
        (void)block;
        cursor += 32;
    }

    general.sample_interval_ms = uint_item(block1, 32, 45, 2) / 16.0;
    general.skew_blocks = int_from_nibbles(block1, 32, 59, 2);
    general.extended_header_blocks = int_from_hex_text(ec_text, 0);
    general.external_header_blocks = int_from_hex_text(ex_text, 0);
    general.manufacturer_code = bcd_int(block1, 32, 33, 2);
    general.manufacturer_serial = bcd_int(block1, 32, 35, 4);
    return {general, cursor};
}

std::pair<std::vector<ChannelSet>, int> parse_channel_sets(
    const std::uint8_t* buf,
    std::size_t buflen,
    int offset,
    const GeneralHeader& general) {
    std::vector<ChannelSet> channel_sets;
    int cursor = offset;
    const int cs_per_scan = std::max(general.channel_sets_per_scan_type, 0);
    const int scan_types = std::max(general.scan_types, 0);
    for (int scan = 0; scan < scan_types; ++scan) {
        for (int cs = 0; cs < cs_per_scan; ++cs) {
            (void)scan;
            (void)cs;
            if (general.revision_major == 3) {
                if (static_cast<std::size_t>(cursor + 96) > buflen) {
                    throw SegdFormatError("Unexpected end of SEG-D buffer");
                }
                channel_sets.push_back(parse_channel_set_rev3(buf + cursor, static_cast<int>(channel_sets.size()), general));
                cursor += 96;
            } else {
                if (static_cast<std::size_t>(cursor + 32) > buflen) {
                    throw SegdFormatError("Unexpected end of SEG-D buffer");
                }
                channel_sets.push_back(parse_channel_set_legacy(buf + cursor, static_cast<int>(channel_sets.size()), general));
                cursor += 32;
            }
        }
        if (general.skew_blocks > 0) {
            cursor += general.skew_blocks * 32;
        }
    }
    return {channel_sets, cursor};
}

std::optional<std::pair<int, int>> read_trace_header_shallow(
    const std::uint8_t* buf,
    std::size_t buflen,
    int offset,
    const GeneralHeader& general,
    const std::vector<ChannelSet>& channel_sets) {
    if (offset == static_cast<int>(buflen)) {
        return std::nullopt;
    }
    if (offset + 20 >= static_cast<int>(buflen)) {
        return std::nullopt;
    }
    const std::uint8_t* block = buf + offset;
    const std::uint8_t* ffff = buf + general.offset;
    if (block[0] != ffff[0] || block[1] != ffff[1]) {
        return std::nullopt;
    }
    const std::string file_text = hex_nibbles(block, 20, 1, 4);
    const int file_number =
        (file_text == "FFFF") ? uint_item(block, 20, 35, 6) : int_from_hex_text(file_text, -1);
    if (general.file_number >= 0 && file_number != general.file_number) {
        return std::nullopt;
    }
    const int scan_type = int_from_nibbles(block, 20, 5, 2);
    const std::string cs_text = hex_nibbles(block, 20, 7, 2);
    const int channel_set_number =
        (cs_text == "FF") ? uint_item(block, 20, 31, 4) : int_from_hex_text(cs_text, -1);
    if (!find_channel_set(channel_sets, scan_type, channel_set_number)) {
        return std::nullopt;
    }
    const int extended_count = uint_item(block, 20, 19, 2);
    if (offset + 20 + 32 * extended_count > static_cast<int>(buflen)) {
        return std::nullopt;
    }
    return std::make_pair(scan_type, channel_set_number);
}

bool check_trace_header(
    const std::uint8_t* buf,
    std::size_t buflen,
    int offset,
    const GeneralHeader& general,
    const std::vector<ChannelSet>& channel_sets) {
    return read_trace_header_shallow(buf, buflen, offset, general, channel_sets).has_value();
}

std::optional<int> find_trace_header(
    const std::uint8_t* buf,
    std::size_t buflen,
    int offset,
    const GeneralHeader& general,
    const std::vector<ChannelSet>& channel_sets) {
    for (int pos = std::max(offset, 0); pos + 19 < static_cast<int>(buflen); ++pos) {
        if (check_trace_header(buf, buflen, pos, general, channel_sets)) {
            return pos;
        }
    }
    return std::nullopt;
}

std::pair<int, int> samples_from_length(int format_code, int samples_bytes) {
    const int bits = sample_bits(format_code);
    if (bits <= 0) {
        return {0, 0};
    }
    const int sample_count = static_cast<int>((static_cast<long long>(samples_bytes) * 8) / bits);
    return {sample_count, sample_byte_count(format_code, sample_count)};
}

std::pair<int, int> resolve_trace_sample_count(
    const std::uint8_t* buf,
    std::size_t buflen,
    int offset,
    int header_len,
    const GeneralHeader& general,
    const ChannelSet& channel_set,
    int extended_samples,
    const std::vector<ChannelSet>& channel_sets) {
    std::vector<int> candidates;
    auto push_unique = [&candidates](int value) {
        if (std::find(candidates.begin(), candidates.end(), value) == candidates.end()) {
            candidates.push_back(value);
        }
    };
    if (channel_set.trace_length_bytes > header_len) {
        push_unique(channel_set.trace_length_bytes);
    }
    if (extended_samples > 0) {
        push_unique(header_len + sample_byte_count(general.format_code, extended_samples));
    }
    if (channel_set.sample_count > 0) {
        push_unique(header_len + sample_byte_count(general.format_code, channel_set.sample_count));
    }
    if (channel_set.end_time_ms > channel_set.start_time_ms && general.sample_interval_ms > 0.0) {
        const int nominal = static_cast<int>(
            (channel_set.end_time_ms - channel_set.start_time_ms) / general.sample_interval_ms);
        for (int sample_count :
             {nominal * channel_set.sample_skew_per_scan + 1,
              nominal * channel_set.sample_skew_per_scan,
              nominal + 1,
              nominal}) {
            if (sample_count > 0) {
                push_unique(header_len + sample_byte_count(general.format_code, sample_count));
            }
        }
    }
    for (int length : candidates) {
        if (length <= header_len || offset + length > static_cast<int>(buflen)) {
            continue;
        }
        if (check_trace_header(buf, buflen, offset + length, general, channel_sets) ||
            offset + length == static_cast<int>(buflen)) {
            return samples_from_length(general.format_code, length - header_len);
        }
    }
    const int min_len = candidates.empty() ? header_len : candidates.back();
    const std::optional<int> next_offset = find_trace_header(buf, buflen, offset + min_len, general, channel_sets);
    const int end = next_offset.value_or(static_cast<int>(buflen));
    return samples_from_length(general.format_code, std::max(0, end - offset - header_len));
}

std::optional<TraceHeader> read_trace_header(
    const std::uint8_t* buf,
    std::size_t buflen,
    int offset,
    const GeneralHeader& general,
    const std::vector<ChannelSet>& channel_sets) {
    if (offset + 20 > static_cast<int>(buflen)) {
        return std::nullopt;
    }
    const std::uint8_t* block = buf + offset;
    const std::uint8_t* ffff = buf + general.offset;
    if (block[0] != ffff[0] || block[1] != ffff[1]) {
        return std::nullopt;
    }
    const std::string file_text = hex_nibbles(block, 20, 1, 4);
    const int file_number =
        (file_text == "FFFF") ? uint_item(block, 20, 35, 6) : int_from_hex_text(file_text, -1);
    if (general.file_number >= 0 && file_number != general.file_number) {
        return std::nullopt;
    }
    const int scan_type = int_from_nibbles(block, 20, 5, 2);
    const std::string cs_text = hex_nibbles(block, 20, 7, 2);
    const int channel_set_number =
        (cs_text == "FF") ? uint_item(block, 20, 31, 4) : int_from_hex_text(cs_text, -1);
    const ChannelSet* channel_set = find_channel_set(channel_sets, scan_type, channel_set_number);
    if (!channel_set || channel_set->channel_count < 1) {
        return std::nullopt;
    }
    const std::string trace_text = hex_nibbles(block, 20, 9, 4);
    int trace_number = (trace_text == "FFFF") ? -1 : int_from_hex_text(trace_text, -1);
    const int extended_count = uint_item(block, 20, 19, 2);
    const int extended_offset = offset + 20;
    const int extended_size = extended_count * 32;
    if (extended_offset + extended_size > static_cast<int>(buflen)) {
        return std::nullopt;
    }
    const std::uint8_t* extended = buf + extended_offset;
    int extended_samples = 0;
    if (extended_count > 0) {
        if (general.revision_major == 3) {
            extended_samples = uint_item(extended, static_cast<std::size_t>(extended_size), 49, 8);
            if (trace_number < 0) {
                trace_number = uint_item(extended, static_cast<std::size_t>(extended_size), 43, 6);
            }
        } else {
            extended_samples = uint_item(extended, static_cast<std::size_t>(extended_size), 15, 6);
        }
    }
    const int header_len = 20 + extended_size;
    const auto [sample_count, byte_count] = resolve_trace_sample_count(
        buf,
        buflen,
        offset,
        header_len,
        general,
        *channel_set,
        extended_samples,
        channel_sets);
    if (sample_count <= 0) {
        return std::nullopt;
    }
    TraceHeader header;
    header.offset = offset;
    header.file_number = file_number;
    header.scan_type_number = scan_type;
    header.channel_set_number = channel_set_number;
    header.trace_number = trace_number;
    header.extended_header_count = extended_count;
    header.sample_count = sample_count;
    header.sample_offset = offset + header_len;
    header.sample_byte_count = byte_count;
    return header;
}

std::vector<Trace> parse_demux_traces(
    const std::uint8_t* buf,
    std::size_t buflen,
    int offset,
    const GeneralHeader& general,
    std::vector<ChannelSet>& channel_sets,
    std::vector<std::string>& warnings) {
    std::vector<Trace> traces;
    std::optional<int> cursor = find_trace_header(buf, buflen, offset, general, channel_sets);
    while (cursor.has_value() && *cursor < static_cast<int>(buflen)) {
        const std::optional<TraceHeader> header =
            read_trace_header(buf, buflen, *cursor, general, channel_sets);
        if (!header) {
            const std::optional<int> next_cursor =
                find_trace_header(buf, buflen, *cursor + 1, general, channel_sets);
            if (!next_cursor) {
                break;
            }
            warnings.push_back(
                "Skipped " + std::to_string(*next_cursor - *cursor) + " bytes before next trace header");
            cursor = next_cursor;
            continue;
        }
        ChannelSet* channel_set = nullptr;
        for (ChannelSet& item : channel_sets) {
            if (item.scan_type_number == header->scan_type_number &&
                item.channel_set_number == header->channel_set_number) {
                channel_set = &item;
                break;
            }
        }
        Trace trace;
        trace.header = *header;
        trace.channel_set = channel_set;
        trace.format_code = general.format_code;
        trace.buffer = buf;
        trace.buffer_size = buflen;
        traces.push_back(trace);
        cursor = find_trace_header(
            buf,
            buflen,
            header->sample_offset + header->sample_byte_count,
            general,
            channel_sets);
    }
    return traces;
}

}  // namespace

SegdFile::SegdFile(std::vector<std::uint8_t> data, std::string source)
    : data_(std::move(data)), source_(std::move(source)) {}

std::vector<std::uint8_t> read_binary_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw SegdFormatError("Cannot open file: " + path);
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        throw SegdFormatError("Cannot read file size: " + path);
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), size);
        if (!input) {
            throw SegdFormatError("Failed to read file: " + path);
        }
    }
    return data;
}

SegdFile SegdFile::open(const std::string& path, bool headers_only) {
    SegdFile file(read_binary_file(path), path);
    file.parse(headers_only);
    return file;
}

SegdFile SegdFile::from_bytes(std::vector<std::uint8_t> data, bool headers_only) {
    SegdFile file(std::move(data));
    file.parse(headers_only);
    return file;
}

void SegdFile::parse(bool headers_only) {
    const std::uint8_t* buf = ptr(data_);
    const std::size_t buflen = len(data_);
    const int header_offset = detect_header_offset(buf, buflen);
    auto [general, offset] = parse_general_headers(buf, buflen, header_offset);
    general_ = general;
    auto [channel_sets, next_offset] = parse_channel_sets(buf, buflen, offset, general_);
    channel_sets_ = std::move(channel_sets);
    offset = next_offset;
    offset += (general_.extended_header_blocks + general_.external_header_blocks) * 32;

    if (general_.format_code == 200) {
        throw UnsupportedFormatError("SEG-B records are recognized but not expanded yet");
    }
    if (!general_.is_demultiplexed()) {
        throw UnsupportedFormatError("Multiplexed SEG-D records are recognized but not expanded yet");
    }
    if (!headers_only) {
        traces_ = parse_demux_traces(buf, buflen, offset, general_, channel_sets_, warnings_);
    }
}

std::vector<float> SegdFile::read_samples(const Trace& trace) const {
    if (!trace.buffer) {
        throw SegdFormatError("Trace buffer is not available");
    }
    const std::uint8_t* start = trace.buffer + trace.header.sample_offset;
    return decode_samples(
        trace.format_code,
        start,
        trace.buffer_size - static_cast<std::size_t>(trace.header.sample_offset),
        trace.header.sample_count,
        trace.channel_set ? trace.channel_set->multiplier_power : 0.0);
}

}  // namespace segdcore
