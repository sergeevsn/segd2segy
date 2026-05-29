#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "segy_writer.hpp"
#include "segdcore/channel_types.hpp"
#include "segdcore/exception.hpp"
#include "segdcore/reader.hpp"

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path input_dir;
    fs::path output_file;
    std::string pattern;  // empty = all .sgd and .segd files
    std::string sort_mode = "name";
    bool skip_service = false;
    segdcore::ChannelFilter channel_filter;
    bool verbose = false;
};

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " -i INPUT_DIR -o OUTPUT.sgy [options]\n\n"
        << "Options:\n"
        << "  -i, --input DIR          Folder with SEG-D files (.sgd / .segd)\n"
        << "  -o, --output FILE        Output SEG-Y file\n"
        << "  --pattern GLOB           Extra filename filter (default: all .sgd and .segd)\n"
        << "  --sort MODE              Sort input files: name | fileno (default: name)\n"
        << "  --skip-service           Export only channel set 6 (all other channel sets are service)\n"
        << "  --include-types LIST     Comma-separated channel type codes to keep\n"
        << "  --exclude-types LIST     Comma-separated channel type codes to drop\n"
        << "  -v, --verbose            Print progress details\n"
        << "  -h, --help               Show this help\n";
}

std::optional<std::string> arg_value(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        return std::nullopt;
    }
    return std::string(argv[++index]);
}

Options parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "-i" || arg == "--input") {
            const auto value = arg_value(i, argc, argv);
            if (!value) {
                throw std::invalid_argument("Missing value for " + arg);
            }
            options.input_dir = *value;
        } else if (arg == "-o" || arg == "--output") {
            const auto value = arg_value(i, argc, argv);
            if (!value) {
                throw std::invalid_argument("Missing value for " + arg);
            }
            options.output_file = *value;
        } else if (arg == "--pattern") {
            const auto value = arg_value(i, argc, argv);
            if (!value) {
                throw std::invalid_argument("Missing value for --pattern");
            }
            options.pattern = *value;
        } else if (arg == "--sort") {
            const auto value = arg_value(i, argc, argv);
            if (!value) {
                throw std::invalid_argument("Missing value for --sort");
            }
            options.sort_mode = *value;
        } else if (arg == "--skip-service") {
            options.skip_service = true;
        } else if (arg == "--include-types") {
            const auto value = arg_value(i, argc, argv);
            if (!value) {
                throw std::invalid_argument("Missing value for --include-types");
            }
            options.channel_filter.include_types = segdcore::parse_channel_type_list(*value);
        } else if (arg == "--exclude-types") {
            const auto value = arg_value(i, argc, argv);
            if (!value) {
                throw std::invalid_argument("Missing value for --exclude-types");
            }
            options.channel_filter.exclude_types = segdcore::parse_channel_type_list(*value);
        } else if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
        } else {
            throw std::invalid_argument("Unknown argument: " + arg);
        }
    }

    if (options.input_dir.empty() || options.output_file.empty()) {
        throw std::invalid_argument("Both --input and --output are required");
    }
    options.channel_filter.skip_service = options.skip_service;
    return options;
}

std::string path_extension_lower(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

bool matches_pattern(const fs::path& path, const std::string& pattern) {
    if (pattern.empty() || pattern == "*" || pattern == "*.*") {
        return true;
    }
    if (pattern.rfind("*.", 0) == 0) {
        std::string suffix = pattern.substr(1);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) { return std::tolower(c); });
        return path_extension_lower(path) == suffix;
    }
    return path.filename().string() == pattern;
}

bool is_segd_extension(const fs::path& path) {
    const std::string ext = path_extension_lower(path);
    return ext == ".sgd" || ext == ".segd";
}

std::vector<fs::path> collect_input_files(const Options& options) {
    if (!fs::is_directory(options.input_dir)) {
        throw std::runtime_error("Input path is not a directory: " + options.input_dir.string());
    }

    std::vector<fs::path> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(options.input_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const fs::path& path = entry.path();
        if (!is_segd_extension(path)) {
            continue;
        }
        if (!matches_pattern(path, options.pattern)) {
            continue;
        }
        files.push_back(path);
    }

    if (files.empty()) {
        throw std::runtime_error("No SEG-D files found in " + options.input_dir.string());
    }

    if (options.sort_mode == "fileno") {
        std::stable_sort(files.begin(), files.end(), [](const fs::path& left, const fs::path& right) {
            try {
                const segdcore::SegdFile l = segdcore::SegdFile::open(left.string(), true);
                const segdcore::SegdFile r = segdcore::SegdFile::open(right.string(), true);
                if (l.general().file_number != r.general().file_number) {
                    return l.general().file_number < r.general().file_number;
                }
            } catch (...) {
            }
            return left.filename().string() < right.filename().string();
        });
    } else {
        std::sort(files.begin(), files.end());
    }
    return files;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_args(argc, argv);
        const std::vector<fs::path> files = collect_input_files(options);

        std::optional<int> reference_ns;
        std::optional<int> reference_dt_us;
        std::optional<int> reference_format;

        segd2segy::SegyWriter writer(options.output_file.string());
        writer.write_text_header("Merged SEG-Y created by segd2segy from SEG-D folder " + options.input_dir.string());

        int global_tracl = 0;
        int files_written = 0;
        int traces_written = 0;
        int traces_skipped = 0;

        for (const fs::path& path : files) {
            segdcore::SegdFile segd = segdcore::SegdFile::open(path.string(), false);
            const auto& general = segd.general();

            if (!reference_format.has_value()) {
                reference_format = general.format_code;
                reference_ns = -1;
                reference_dt_us = static_cast<int>(general.sample_interval_ms * 1000.0);
            } else if (reference_format.value() != general.format_code) {
                std::cerr << "Warning: format code mismatch in " << path << '\n';
            }

            if (options.verbose) {
                std::cout << "Reading " << path.filename().string() << " (file_number=" << general.file_number
                          << ", traces=" << segd.traces().size() << ")\n";
            }

            int file_trace_number = 0;

            for (const segdcore::Trace& trace : segd.traces()) {
                if (!trace.channel_set) {
                    ++traces_skipped;
                    continue;
                }
                const segdcore::ChannelSet& channel_set = *trace.channel_set;
                if (!options.channel_filter.include_channel_set(
                        channel_set.channel_set_number,
                        channel_set.channel_type_number,
                        general.revision_major)) {
                    ++traces_skipped;
                    if (options.verbose) {
                        std::cout << "  skip CS" << channel_set.channel_set_number << " type "
                                  << channel_set.channel_type_number << " ("
                                  << segdcore::channel_type_name(
                                         channel_set.channel_type_number, general.revision_major)
                                  << ")\n";
                    }
                    continue;
                }

                const std::vector<float> samples = segd.read_samples(trace);
                if (!reference_ns.has_value() || reference_ns.value() < 0) {
                    reference_ns = static_cast<int>(samples.size());
                } else if (static_cast<int>(samples.size()) != reference_ns.value()) {
                    std::cerr << "Warning: sample count mismatch in " << path << " trace "
                              << trace.header.trace_number << '\n';
                }

                if (!writer.trace_count()) {
                    writer.write_binary_header(
                        reference_dt_us.value_or(static_cast<int>(general.sample_interval_ms * 1000.0)),
                        reference_ns.value_or(static_cast<int>(samples.size())),
                        5);
                }

                segd2segy::SegyTraceMeta meta;
                meta.tracl = ++global_tracl;
                meta.fldr = general.file_number >= 0 ? general.file_number : files_written + 1;
                meta.tracf = ++file_trace_number;
                meta.cdp = channel_set.channel_set_number;
                meta.ns = static_cast<int>(samples.size());
                meta.dt_us = static_cast<int>(channel_set.sample_interval_ms * 1000.0);
                meta.channel_set_number = channel_set.channel_set_number;
                meta.scan_type_number = channel_set.scan_type_number;
                writer.write_trace(meta, samples);
                ++traces_written;
            }
            ++files_written;
        }

        if (traces_written == 0) {
            throw std::runtime_error("No traces were written; check channel filters and input files");
        }

        std::cout << "Wrote " << traces_written << " traces from " << files_written << " SEG-D files to "
                  << options.output_file << '\n';
        if (traces_skipped > 0) {
            std::cout << "Skipped " << traces_skipped << " traces (channel filter)\n";
        }
        return 0;
    } catch (const segdcore::SegdError& error) {
        std::cerr << "SEG-D error: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
