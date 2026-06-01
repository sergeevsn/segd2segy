# segd2segy

Command-line tool (C++17) that merges demultiplexed **SEG-D** files from a folder into a single **SEG-Y** file.

- Demultiplexed SEG-D is supported; multiplexed SEG-D and SEG-B are rejected with an error.
- Output samples are **IEEE 32-bit float**; binary header **format code 5**; sample byte order is native (no endian conversion).
- Optional channel-set filters; trace numbers are renumbered within each SEG-D file.
- Input `.sgd` / `.segd` files are processed in ascending path order (typical zero-padded Field Record names).

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
| `--pattern GLOB` | Optional filename filter (default: all `.sgd` and `.segd`) |
| `--skip-service` | Export only the channel set with the **highest** `channel_set_number` in each file |
| `--skip-errors` | Skip files that fail to open or read; print a warning and continue |
| `--include-types LIST` | Keep only these channel type codes (e.g. `1,0x10`) |
| `--exclude-types LIST` | Drop these channel type codes |
| `-p`, `--progress` | Progress bar over SEG-D files (replaces `-v`) |
| `-v`, `--verbose` | Log each file and skipped channel sets |
| `-h`, `--help` | Show help |

### Channel filtering

Filtering applies to **channel set descriptors** (not individual trace headers):

- **`--skip-service`** — per SEG-D file, keeps the channel set with the largest `channel_set_number`.
- **`--include-types`** — if set, only listed channel types are exported (overrides `--skip-service`).
- **`--exclude-types`** — drops listed types when no include list is given.

### Trace numbering

| SEG-Y field | Behaviour |
|-------------|-----------|
| `tracl` | Sequential index across the output file (1 … N) |
| `tracf` | Renumbered from 1 within each SEG-D file for exported traces |
| `fldr` | SEG-D general header file number |

### Output SEG-Y

- Text header — 3200 bytes (EBCDIC).
- Binary header — 400 bytes; **format 5**, `dt` and `ns` from the first written trace.
- Trace — 240-byte header + `ns` × 4-byte IEEE float samples (native endian).

Trace headers include `tracl`, `tracr`/`tracf`, `fldr`, `cdp` (channel set number), `ns`, `dt`, and channel set / scan type in writer-specific byte positions.

### Examples

Merge all traces:

```bash
segd2segy -i ./data -o merged_all.sgy -v
```

With progress bar:

```bash
segd2segy -i ./data -o merged_all.sgy -p
```

Skip corrupted SEG-D files (e.g. during a large merge):

```bash
segd2segy -i ./data -o merged.sgy -p --skip-errors
```

Last channel set only (per file):

```bash
segd2segy -i ./data -o merged_data.sgy --skip-service -v
```

Filter by channel type:

```bash
segd2segy -i ./data -o merged.sgy --include-types 1,0x10
```
