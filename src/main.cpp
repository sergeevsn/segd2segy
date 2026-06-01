#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define SEGD2SEGY_ISATTY _isatty
#define SEGD2SEGY_FILENO _fileno
#else
#include <unistd.h>
#define SEGD2SEGY_ISATTY isatty
#define SEGD2SEGY_FILENO fileno
#endif

#include "segy_writer.hpp"
#include "segdcore/channel_types.hpp"
#include "segdcore/exception.hpp"
#include "segdcore/reader.hpp"
#include "segdcore/utils.hpp"

namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path input_dir;
    fs::path output_file;
    std::string pattern;  // empty = all .sgd and .segd files
    bool skip_service = false;
    segdcore::ChannelFilter channel_filter;
    bool verbose = false;
    bool progress = false;
    bool skip_errors = false;
};

void print_usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " -i INPUT_DIR -o OUTPUT.sgy [options]\n\n"
        << "Options:\n"
        << "  -i, --input DIR          Folder with SEG-D files (.sgd / .segd)\n"
        << "  -o, --output FILE        Output SEG-Y file\n"
        << "  --pattern GLOB           Extra filename filter (default: all .sgd and .segd)\n"
        << "  --skip-service           Export only the last channel set by number in each file\n"
        << "  --skip-errors            Skip SEG-D files that fail to open or read (warn and continue)\n"
        << "  --include-types LIST     Comma-separated channel type codes to keep\n"
        << "  --exclude-types LIST     Comma-separated channel type codes to drop\n"
        << "  -p, --progress           Text progress bar over SEG-D files (replaces -v)\n"
        << "  -v, --verbose            Print per-file and per-channel-set details\n"
        << "  --probe FILE             Read one SEG-D file and print header diagnostics\n"
        << "  -h, --help               Show this help\n";
}

int probe_segd_file(const fs::path& path) {
    if (!fs::is_regular_file(path)) {
        std::cerr << "Not a file: " << path << '\n';
        return 1;
    }

    std::cout << "path: " << fs::absolute(path) << '\n';
    std::cout << "size: " << fs::file_size(path) << " bytes\n";

    std::array<std::uint8_t, 32> prefix{};
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            std::cerr << "Cannot open file for read\n";
            return 1;
        }
        input.read(reinterpret_cast<char*>(prefix.data()), static_cast<std::streamsize>(prefix.size()));
        const std::streamsize got = input.gcount();
        std::cout << "read first " << got << " bytes\n";
    }

    std::cout << "hex:";
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        std::cout << ' ' << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned>(prefix[i]);
    }
    std::cout << std::dec << '\n';

    const std::string format_nibbles = segdcore::hex_nibbles(prefix.data(), prefix.size(), 5, 4);
    const int format_code = segdcore::demux_format_from_nibbles(prefix.data(), prefix.size());
    std::cout << "format nibbles (offset 0): '" << format_nibbles << "' -> code " << format_code << '\n';

    try {
        const segdcore::SegdFile segd = segdcore::SegdFile::open(path.string(), false);
        const auto& general = segd.general();
        std::cout << "SegdFile::open: OK\n";
        std::cout << "  file_number=" << general.file_number << " format_code=" << general.format_code
                  << " revision=" << general.revision_major << '.' << general.revision_minor
                  << " traces=" << segd.traces().size() << " channel_sets=" << segd.channel_sets().size()
                  << '\n';
        return 0;
    } catch (const segdcore::SegdError& error) {
        std::cerr << "SegdFile::open failed: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "SegdFile::open failed: " << error.what() << '\n';
        return 2;
    }
}

std::optional<std::string> arg_value(int& index, int argc, char** argv) {
    if (index + 1 >= argc) {
        return std::nullopt;
    }
    return std::string(argv[++index]);
}

std::optional<fs::path> parse_probe_arg(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (arg == "--probe") {
            int j = i;
            const auto value = arg_value(j, argc, argv);
            if (!value) {
                throw std::invalid_argument("Missing value for --probe");
            }
            return fs::path(*value);
        }
    }
    return std::nullopt;
}

Options parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--probe") {
            ++i;
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
        } else if (arg == "--skip-service") {
            options.skip_service = true;
        } else if (arg == "--skip-errors") {
            options.skip_errors = true;
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
        } else if (arg == "-p" || arg == "--progress") {
            options.progress = true;
            options.verbose = false;
        } else if (arg == "-v" || arg == "--verbose") {
            if (!options.progress) {
                options.verbose = true;
            }
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
    if (fs::is_regular_file(options.input_dir)) {
        if (!is_segd_extension(options.input_dir)) {
            throw std::runtime_error("Input file is not a SEG-D file (.sgd / .segd): " +
                                     options.input_dir.string());
        }
        if (!matches_pattern(options.input_dir, options.pattern)) {
            throw std::runtime_error("Input file does not match --pattern: " + options.input_dir.string());
        }
        return {options.input_dir};
    }
    if (!fs::is_directory(options.input_dir)) {
        throw std::runtime_error("Input path is not a directory or SEG-D file: " + options.input_dir.string());
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

    // Field Record order follows zero-padded filenames (lexicographic sort on full path).
    std::sort(files.begin(), files.end());
    return files;
}

class FileProgressBar {
public:
    FileProgressBar(int total_files, bool enabled)
        : total_files_(std::max(total_files, 1)),
          enabled_(enabled && total_files > 0),
          interactive_(enabled_ && SEGD2SEGY_ISATTY(SEGD2SEGY_FILENO(stderr))) {}

    void update(int current_file, const std::string& filename) {
        if (!enabled_) {
            return;
        }

        const int percent = (current_file * 100) / total_files_;
        const int filled = (current_file * kBarWidth) / total_files_;

        std::ostringstream line;
        if (interactive_) {
            line << '\r' << kPrefix << '[';
            for (int i = 0; i < kBarWidth; ++i) {
                line << (i < filled ? '=' : (i == filled ? '>' : ' '));
            }
            line << "] " << std::setw(3) << percent << "% (" << current_file << '/' << total_files_ << ") "
                 << filename;
            std::cerr << line.str() << std::string(kPadExtra, ' ');
        } else {
            line << kPrefix << '[' << current_file << '/' << total_files_ << "] " << std::setw(3) << percent
                 << "% " << filename;
            std::cerr << line.str() << '\n';
        }
        std::cerr.flush();
    }

    void finish() {
        if (enabled_ && interactive_) {
            std::cerr << '\n';
        }
    }

private:
    static constexpr int kBarWidth = 40;
    static constexpr int kPadExtra = 8;
    static constexpr const char* kPrefix = "SEG-D ";

    int total_files_;
    bool enabled_;
    bool interactive_;
};

}  // namespace

int main(int argc, char** argv) {
    try {
        if (const std::optional<fs::path> probe = parse_probe_arg(argc, argv)) {
            return probe_segd_file(*probe);
        }

        Options options = parse_args(argc, argv);
        const std::vector<fs::path> files = collect_input_files(options);

        std::optional<int> reference_ns;
        std::optional<int> reference_dt_us;
        std::optional<int> reference_format;

        segd2segy::SegyWriter writer(options.output_file.string());
        writer.write_text_header("Merged SEG-Y created by segd2segy from SEG-D folder " + options.input_dir.string());

        int global_tracl = 0;
        int files_written = 0;
        int files_skipped = 0;
        int traces_written = 0;
        int traces_skipped = 0;

        FileProgressBar progress_bar(static_cast<int>(files.size()), options.progress);
        const bool progress_interactive =
            options.progress && SEGD2SEGY_ISATTY(SEGD2SEGY_FILENO(stderr));

        int file_index = 0;
        for (const fs::path& path : files) {
            progress_bar.update(file_index + 1, path.filename().string());

            try {
            segdcore::SegdFile segd;
            try {
                segd = segdcore::SegdFile::open(path.string(), false);
            } catch (const segdcore::SegdError& error) {
                throw segdcore::SegdFormatError(path.string() + ": " + error.what());
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string("Failed to read ") + path.string() + ": " + error.what());
            }
            const auto& general = segd.general();

            if (!reference_format.has_value()) {
                reference_format = general.format_code;
                reference_ns = -1;
                reference_dt_us = static_cast<int>(general.sample_interval_ms * 1000.0);
            } else if (reference_format.value() != general.format_code) {
                std::cerr << "Warning: format code mismatch in " << path << '\n';
            }

            options.channel_filter.begin_file(segd.channel_sets());

            if (options.verbose) {
                std::cout << "Reading " << path.filename().string() << " (file_number=" << general.file_number
                          << ", traces=" << segd.traces().size() << ")";
                if (options.skip_service) {
                    const int keep_cs = options.channel_filter.keep_channel_set_number();
                    if (keep_cs >= 0) {
                        std::cout << ", keep CS" << keep_cs;
                    } else {
                        std::cout << ", keep CS (none)";
                    }
                }
                std::cout << '\n';
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
            } catch (const std::exception& error) {
                if (!options.skip_errors) {
                    throw;
                }
                if (progress_interactive) {
                    std::cerr << '\n';
                }
                std::cerr << "Warning: skipping " << path << ": " << error.what() << '\n';
                ++files_skipped;
            }
            ++file_index;
        }
        progress_bar.finish();

        if (traces_written == 0) {
            throw std::runtime_error("No traces were written; check channel filters and input files");
        }

        std::cout << "Wrote " << traces_written << " traces from " << files_written << " SEG-D files to "
                  << options.output_file << '\n';
        if (files_skipped > 0) {
            std::cout << "Skipped " << files_skipped << " SEG-D file(s) (--skip-errors)\n";
        }
        if (traces_skipped > 0) {
            std::cout << "Skipped " << traces_skipped << " traces (channel filter)\n";
        }
        return 0;
    } catch (const segdcore::SegdError& error) {
        std::cerr << '\n' << "SEG-D error: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
