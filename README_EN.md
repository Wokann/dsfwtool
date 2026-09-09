# dsfwtool v1.0

[简体中文](README.md)

`dsfwtool` is a command-line utility for Nintendo DS firmware. It displays a
firmware layout, exports selected components to named paths, creates a
firmware image from a header and component files, and processes individual
compressed or encrypted streams. It accepts firmware capacities of 256 KiB
and larger multiples of 256 KiB.

## Requirements

Building from source requires a C99 compiler and GNU Make. The supplied
Makefile has been tested with GCC/MinGW; Clang is also suitable. On Windows,
use a shell with `make` and a C compiler on `PATH` (for example an MSYS2 MinGW
shell). On Linux and macOS, install the platform's C development tools and
GNU Make first.

## Build and install

Build from the repository root:

```text
make dsfwtool
```

The Windows executable is `release/dsfwtool.exe`; Unix-like builds produce
`release/dsfwtool`. Verify the build with:

```text
release/dsfwtool.exe --version    # Windows
./release/dsfwtool --version      # Linux/macOS
```

There is no separate installer or `make install` target. The executable may
be run in place. To make it available as `dsfwtool` from any directory, copy
it to a directory already listed in the user's `PATH`. For example, on a
Unix-like system:

```text
install -Dm755 release/dsfwtool "$HOME/.local/bin/dsfwtool"
```

Ensure that `$HOME/.local/bin` is in `PATH`. On Windows, copy
`release/dsfwtool.exe` to a user-selected directory and add that directory to
the User `Path` environment variable, then open a new terminal.

The examples below use `dsfwtool` as the command name. Substitute
`release/dsfwtool.exe` or `./release/dsfwtool` when it has not been added to
`PATH`.

## Paths and output

Every output argument is a file path. It may include relative or absolute
parent directories; missing parents are created automatically. Input paths
must already exist.

## Firmware components and layout

The primary firmware header is exported as a 0x180-byte `-h` file. It records
the five primary component starts and the parameters needed for P1/P2
encryption. P1 and P2 use an LZ10/P12 stream followed by the firmware
encryption layer. P3, P4, and P5 use the P345 dual-static-Huffman format
directly, with no encryption layer.

FlashMe firmware has a second 0x180-byte header (`-fh`) in the trailer at
logical capacity minus 0x980. Its FP1 and FP2 components are unencrypted P12
streams. The seven components can be interleaved in physical ROM order, so
the builder relocates them as one ordered set and writes their resulting
starts back to the primary and FlashMe headers separately.

Some FlashMe dumps omit the final 0x200-byte settings sector and have a
physical length ending in `0x...FE00`. Their logical capacity is still the
next 256 KiB boundary. Creation always writes the complete logical capacity;
use `-s` to select a larger 256 KiB multiple.

## Command reference

```text
dsfwtool --help
dsfwtool --version

dsfwtool -i FIRMWARE.bin [-o REPORT.txt]

dsfwtool -x FIRMWARE.bin
  -h HEADER.bin
  -p1 [-decrypt [-uncomp]] FILE
  -p2 [-decrypt [-uncomp]] FILE
  -p3 [-uncomp] FILE  -p4 [-uncomp] FILE  -p5 [-uncomp] FILE
  [-fh FLASH_HEADER.bin -fp1 [-uncomp] FILE -fp2 [-uncomp] FILE]

dsfwtool -c OUTPUT.bin -h HEADER.bin
  -p1 [-encrypt | -comp -encrypt] FILE
  -p2 [-encrypt | -comp -encrypt] FILE
  -p3 [-comp] FILE -p4 [-comp] FILE -p5 [-comp] FILE
  [-fh FLASH_HEADER.bin -fp1 [-comp] FILE -fp2 [-comp] FILE]
  [-s 256K|512K|1M|auto] [--fill 00|FF]
  [header-edit options]

dsfwtool -p1|-p2|-p3|-p4|-p5|-fp1|-fp2
  (-comp|-uncomp|-crypt|-decrypt) INPUT.bin [-h HEADER.bin] -o OUTPUT.bin
```

`-i` reads layout information only. `-x` requires at least one named output.
With no modifier, an extracted component retains its on-image representation.
P1/P2 `-uncomp` is valid only after `-decrypt`; if either P1 or P2 is
decrypted during `-x`, `-h` must be exported in the same command because it
provides the key material.

`-c` always requires `-h` and all of P1 through P5. If any FlashMe artifact
is supplied, `-fh`, `-fp1`, and `-fp2` must all be supplied. P1/P2 with no
modifier are already encrypted P12 streams. `-encrypt` accepts an
already-compressed, unencrypted P12 stream; `-comp -encrypt` accepts plaintext
and performs both stages. P3/P4/P5 use only `-comp` for plaintext. `-crypt`
is accepted as an alias of `-encrypt` for an independent P1/P2 operation.

`-p1` and `-p2` select P12. `-p3`, `-p4`, and `-p5` select P345. `-fp1` and
`-fp2` select unencrypted P12. Encryption and decryption require `-h` and are
valid only for primary P1/P2. An independent P1/P2 operation may be a single
stage, `-decrypt -uncomp`, or `-comp -crypt`; P345 and FP1/FP2 require exactly
one of `-comp` or `-uncomp`.

P1/P2 decryption accepts only complete, 8-byte-aligned ciphertext and exports
only the effective P12 stream after processing its blocks. P1/P2 encryption
accepts any non-empty input: an already aligned input is transformed unchanged,
whereas a partial final block is internally completed before encryption. It is
zero-filled to a four-byte boundary and, if another word is needed to reach
eight-byte alignment, that word is derived from the header's full ID using KEY1.
Explicit tail bytes in an already aligned P12 input are preserved. See the
[tail derivation](docs/compression-reconstruction.md#key1-derived-tail-word-and-encrypted-block-alignment)
for the encoding rule.

## Examples

### Inspect a firmware image

```text
dsfwtool -i firmware.bin
dsfwtool -i firmware.bin -o reports/layout.txt
```

The report includes component offsets, effective compressed sizes, decoded
alignment values, and a recognised FlashMe secondary layout when present.

### Export raw components and rebuild from them

```text
dsfwtool -x firmware.bin -h unpack/header.bin \
  -p1 unpack/p1.encrypted -p2 unpack/p2.encrypted \
  -p3 unpack/p3.p345 -p4 unpack/p4.p345 -p5 unpack/p5.p345

dsfwtool -c rebuilt.bin -h unpack/header.bin \
  -p1 unpack/p1.encrypted -p2 unpack/p2.encrypted \
  -p3 unpack/p3.p345 -p4 unpack/p4.p345 -p5 unpack/p5.p345
```

Unused output bytes are `FF` by default; use `--fill 00` or `--fill FF` to
select the fill byte. Omitting `-s`, or using `-s auto`, selects the smallest
256 KiB multiple that fits every component. `-s 256K`, `-s 512K`, and `-s 1M`
select a capacity explicitly. Choose a larger explicit `-s` if recompressed
FlashMe streams no longer fit their original layout.

Exported P3, P4, and P5 files contain only their effective P345 bitstreams.
While assembling an image, packing writes `00` from each such stream through
its next 8-byte boundary, without overwriting a following component or the
FlashMe trailer header. This padding is not part of the component file and is
not controlled by `--fill`. P1/P2 8-byte padding instead belongs to their
encryption layer.

### Export plaintext components

```text
dsfwtool -x firmware.bin -h unpack/header.bin \
  -p1 -decrypt -uncomp unpack/p1.plain \
  -p2 -decrypt -uncomp unpack/p2.plain \
  -p3 -uncomp unpack/p3.plain \
  -p4 -uncomp unpack/p4.plain \
  -p5 -uncomp unpack/p5.plain
```

`-x` may name only the components needed for the task. For a FlashMe image,
selected secondary artifacts use `-fh`, `-fp1`, and `-fp2`:

```text
dsfwtool -x flashme.bin -fh unpack/flash-header.bin \
  -fp1 unpack/fp1.p12 -fp2 -uncomp unpack/fp2.plain
```

### Work on an independent P12 or P345 stream

```text
dsfwtool -p1 -decrypt p1.encrypted -h header.bin -o work/p1.p12
dsfwtool -p1 -uncomp work/p1.p12 -o work/p1.plain
dsfwtool -p1 -comp -crypt work/p1.plain -h header.bin -o output/p1.encrypted

dsfwtool -p3 -uncomp p3.p345 -o work/p3.plain
dsfwtool -p3 -comp work/p3.plain -o output/p3.p345
```

The combined P1/P2 forms avoid a named intermediate file:

```text
dsfwtool -p2 -decrypt -uncomp p2.encrypted -h header.bin -o work/p2.plain
dsfwtool -p2 -comp -crypt work/p2.plain -h header.bin -o output/p2.encrypted
```

### Build a firmware image from plaintext components

```text
dsfwtool -c rebuilt.bin -s 512K -h unpack/header.bin \
  -p1 -comp -encrypt unpack/p1.plain \
  -p2 -comp -encrypt unpack/p2.plain \
  -p3 -comp unpack/p3.plain \
  -p4 -comp unpack/p4.plain \
  -p5 -comp unpack/p5.plain
```

### Build a FlashMe image

```text
dsfwtool -c flashme-rebuilt.bin -s 512K \
  -h unpack/header.bin \
  -p1 unpack/p1.encrypted -p2 unpack/p2.encrypted \
  -p3 unpack/p3.p345 -p4 unpack/p4.p345 -p5 unpack/p5.p345 \
  -fh unpack/flash-header.bin \
  -fp1 -comp unpack/fp1.plain -fp2 -comp unpack/fp2.plain
```

For FP1/FP2, no modifier accepts an existing unencrypted P12 stream and
`-comp` accepts plaintext. FlashMe compatibility is determined by the
decompressed data and resulting layout; its compressed bytes need not match a
previous stream.

## Header edits during creation

The following options modify the supplied primary header before P1/P2
encryption. For FlashMe creation, the same edits are also applied to `-fh`.

```text
--identifier ABCD
--console-type VALUE
--timestamp YYMMDDHHMM
--shift-amounts VALUE
--part1-ramaddr VALUE
--part2-ramaddr VALUE
--settings-offset VALUE
--set-u8 OFFSET VALUE
--set-u16 OFFSET VALUE
--set-u32 OFFSET VALUE
```

`VALUE` and `OFFSET` accept the numeric syntax recognised by the tool.
`--timestamp` also accepts `YYYY-MM-DDTHH:MM`. Generic `--set-*` offsets are
relative to the exported 0x180-byte header. The tool writes component ROM
starts from the final layout and recalculates the declared Wi-Fi configuration
checksum. For a normal firmware it also derives the three primary component
CRC16 values. FlashMe creation preserves the component CRC16 fields supplied
by its primary and secondary headers.

For detailed P12 and P345 bitstream structures and encoding rules, see
[Compression Structures and Encoding Rules](docs/compression-reconstruction.md).
