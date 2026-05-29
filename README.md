# segd2segy

Standalone C++17 command-line tool that merges demultiplexed **SEG-D** files from a folder into a single **SEG-Y** file.

- Output samples are **IEEE 32-bit float** (SEG-Y data sample format **5**), big-endian.
- Optional filters drop service channel sets; trace numbers are renumbered per SEG-D file.

Demultiplexed SEG-D is supported. Multiplexed SEG-D and SEG-B are detected and rejected (same scope as segdreadpy v0.1).

## Requirements

- CMake 3.16+
- C++17 compiler (GCC, Clang, or MSVC)

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Executable: `build/src/segd2segy`

## Usage

```bash
segd2segy -i /path/to/segd_folder -o merged.sgy [options]
```

### Options

| Option | Description |
|--------|-------------|
| `-i`, `--input DIR` | Directory containing `.sgd` / `.segd` files |
| `-o`, `--output FILE` | Output SEG-Y path |
| `--pattern GLOB` | Optional extra filter (default: all `.sgd` and `.segd`) |
| `--sort name\|fileno` | Sort inputs by path or SEG-D file number (default: `name`) |
| `--skip-service` | Export only the **last channel set by number** in each file (e.g. CS 2 if only CS 1–2 exist) |
| `--include-types LIST` | Keep only these channel type codes (e.g. `1,0x10`) |
| `--exclude-types LIST` | Drop these channel type codes |
| `-p`, `--progress` | Text progress bar over SEG-D files (replaces `-v`) |
| `-v`, `--verbose` | Log each file and skipped channel sets |
| `-h`, `--help` | Show help |

### Channel filtering

Filtering applies to **channel set descriptors** (not individual trace headers):

- **`--skip-service`** — per SEG-D file, keeps only the channel set with the **highest** `channel_set_number` present in that file (lower-numbered sets are treated as service).
- **`--include-types`** — if set, only listed channel **types** are kept (overrides `--skip-service`).
- **`--exclude-types`** — drops listed types when no include list is given.

### Trace numbering

| SEG-Y field | Behaviour |
|-------------|-----------|
| `tracl` | Sequential index across the whole output file (1 … N) |
| `tracf` | Renumbered **from 1 within each SEG-D file** for exported traces only (gaps from skipped service sets are not preserved) |
| `fldr` | SEG-D general header file number |

### Examples

Merge all traces from a folder:

```bash
segd2segy -i ./data -o merged_all.sgy -v
```

With a progress bar (one step per SEG-D file):

```bash
segd2segy -i ./data -o merged_all.sgy -p
```

Last channel set only (per file):

```bash
segd2segy -i ./data -o merged_data.sgy --skip-service -v
```

Filter by channel type (legacy type 1 and Rev.3 `0x10`):

```bash
segd2segy -i ./data -o merged.sgy --include-types 1,0x10
```

## Output SEG-Y

- **Text header** — 3200 bytes (EBCDIC)
- **Binary header** — 400 bytes; format code **5** (IEEE float), `dt` and `ns` from the first written trace
- **Traces** — 240-byte trace header + `ns` × 4-byte big-endian IEEE samples

Trace headers populated include `tracl`, `tracr`/`tracf`, `fldr`, `cdp` (channel set number), `ns`, `dt`, and channel set / scan type in unassigned byte positions used by the writer.

## Project layout

```
segd2segy/
  CMakeLists.txt
  LICENSE
  segdcore/              # Qt-free SEG-D reader (headers, decoders, demux parser)
    include/segdcore/
    src/
  src/
    main.cpp             # CLI
    segy_writer.cpp      # SEG-Y writer
  .github/workflows/     # CI build
```

## Related projects

- **[segdreadpy](https://github.com/YOUR_USER/segdreadpy)** — pure-Python SEG-D reader used as the reference for `segdcore` parsing and decoders.

## Limitations

- One demux sample format per run (warning if input files disagree).
- No support for multiplexed SEG-D or SEG-B expansion yet.
- `--skip-service` selects the last channel set by number per file, not by channel type.

## Publish to GitHub

1. Create an empty repository on GitHub (e.g. `segd2segy`, without an initial README).
2. Replace `YOUR_USER` in this README and in any links you add.
3. From the repository root:

```bash
git init -b main
git add .
git commit -m "Initial release of segd2segy."
git remote add origin https://github.com/YOUR_USER/segd2segy.git
git push -u origin main
```

Or with GitHub CLI:

```bash
gh repo create segd2segy --public --source=. --remote=origin --push
```

## License

MIT — see [LICENSE](LICENSE).
