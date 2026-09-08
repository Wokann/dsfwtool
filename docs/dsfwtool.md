# dsfwtool v1.0

## English

`dsfwtool` is a file-oriented Nintendo DS firmware utility.  It reads a
firmware image without extracting it, exports selected components as explicit
files, rebuilds an image from explicit files, and operates on individual
compressed or encrypted streams.  It accepts firmware capacities of 256 KiB
and larger multiples of 256 KiB.

Build the executable with:

```text
make dsfwtool
```

The Windows build is written to `release/dsfwtool.exe`.

### Component and layout model

The primary firmware header is exported as a 0x180-byte `-h` file.  It records
the five primary component starts and the parameters needed for P1/P2
encryption.  P1 and P2 use an LZ10/P12 stream followed by the firmware
encryption layer.  P3, P4, and P5 use the P345 dual-static-Huffman format
directly, with no encryption layer.

FlashMe firmware has a second 0x180-byte header (`-fh`) in the trailer at
logical-capacity minus 0x980.  Its FP1 and FP2 components are unencrypted P12
streams.  The seven components can be interleaved in physical ROM order, so
the builder relocates them as one ordered set and writes the resulting starts
back to the primary and FlashMe headers separately.

Some FlashMe dumps omit the final 0x200-byte settings sector and therefore
have a physical length ending in `0x...FE00`.  Their logical capacity is still
the next 256 KiB boundary.  An automatic rebuild with `-b` retains that short
physical length; an explicit `-s` writes the complete logical capacity.

### View a layout

```text
dsfwtool -i firmware.bin
dsfwtool -i firmware.bin -o layout.txt
```

`-i` does not export components.  It prints or writes primary offsets,
effective compressed lengths, decoded alignment values, and a recognised
FlashMe secondary layout when present.

### Extract explicit component files

With no modifier, a component is exported in its on-image form:

```text
dsfwtool -x firmware.bin -h header.bin \
  -p1 p1.encrypted -p2 p2.encrypted \
  -p3 p3.p345 -p4 p4.p345 -p5 p5.p345
```

To export plaintext instead:

```text
dsfwtool -x firmware.bin -h header.bin \
  -p1 -decrypt -uncomp p1.plain \
  -p2 -decrypt -uncomp p2.plain \
  -p3 -uncomp p3.plain -p4 -uncomp p4.plain -p5 -uncomp p5.plain
```

`-uncomp` for P1/P2 is valid only immediately after `-decrypt`.  Any P1/P2
decryption request in `-x` must also export `-h`, because the header defines
the key material.  `-x` may export only the components that are needed.

For a FlashMe image, add any selected secondary artifacts:

```text
dsfwtool -x flashme.bin -fh flash-header.bin \
  -fp1 fp1.p12 -fp2 -uncomp fp2.plain
```

FP1 and FP2 have no encryption option.

### Create a firmware image

Creation always requires the primary header and all five primary components:

```text
dsfwtool -c rebuilt.bin -b firmware.bin -h header.bin \
  -p1 p1.encrypted -p2 p2.encrypted \
  -p3 p3.p345 -p4 p4.p345 -p5 p5.p345
```

`-b` copies every non-component byte from the base image.  Without `-b`,
unused output bytes are `FF` by default; use `--fill 00` or `--fill FF` to
choose the fill byte.  `-s 256K`, `-s 512K`, `-s 1M`, or `-s auto` selects the
logical output capacity.  If recompressed FlashMe streams no longer fit their
original layout, choose a larger explicit `-s` value.

The input mode for each primary component is explicit:

| Component | No modifier | `-encrypt` | `-comp -encrypt` |
| --- | --- | --- | --- |
| P1/P2 | encrypted P12 stream | unencrypted, already-compressed P12 stream | plaintext to compress, then encrypt |
| P3/P4/P5 | P345 stream | not valid | plaintext with `-comp` only |

For example, rebuild all primary components from plaintext:

```text
dsfwtool -c rebuilt.bin -b firmware.bin -s 512K -h header.bin \
  -p1 -comp -encrypt p1.plain -p2 -comp -encrypt p2.plain \
  -p3 -comp p3.plain -p4 -comp p4.plain -p5 -comp p5.plain
```

To create FlashMe output, provide all three secondary files together:

```text
dsfwtool -c flashme-rebuilt.bin -b flashme.bin -s 512K \
  -h header.bin \
  -p1 p1.encrypted -p2 p2.encrypted \
  -p3 p3.p345 -p4 p4.p345 -p5 p5.p345 \
  -fh flash-header.bin -fp1 -comp fp1.plain -fp2 -comp fp2.plain
```

For FP1/FP2, no modifier means an existing unencrypted P12 stream and `-comp`
means plaintext.  FlashMe streams made by community tools can use different,
but valid, LZ choices; therefore a converted FlashMe stream is judged by its
decompressed plaintext and layout rather than by requiring its compressed
bytes to match a previous stream.

### Independent stream operations

Select the component type first.  P1/P2 select the P12 codec and need `-h`
only for encryption or decryption:

```text
dsfwtool -p1 -uncomp p1.p12 -o p1.plain
dsfwtool -p1 -decrypt p1.encrypted -h header.bin -o p1.p12
dsfwtool -p2 -decrypt -uncomp p2.encrypted -h header.bin -o p2.plain
dsfwtool -p1 -comp p1.plain -o p1.p12
dsfwtool -p1 -comp -crypt p1.plain -h header.bin -o p1.encrypted
dsfwtool -p3 -uncomp p3.p345 -o p3.plain
dsfwtool -p3 -comp p3.plain -o p3.p345
dsfwtool -fp1 -uncomp fp1.p12 -o fp1.plain
dsfwtool -fp1 -comp fp1.plain -o fp1.p12
```

`-p3`, `-p4`, and `-p5` automatically select P345.  `-fp1` and `-fp2`
automatically select unencrypted P12.  Encryption flags are rejected for
P345 and FlashMe streams.

### Header edits during creation

The following options modify the supplied primary header before P1/P2
encryption.  For FlashMe creation, the same edits are also applied to `-fh`.

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

`--timestamp` also accepts `YYYY-MM-DDTHH:MM`.  Generic `--set-*` offsets are
relative to the 0x180-byte exported header.  The tool derives ROM component
starts from the final layout and recalculates the header Wi-Fi configuration
checksum when that field is declared.  For a normal firmware it also derives
the three primary component CRC16 values.  FlashMe creation preserves the
component CRC16 fields supplied by its primary and secondary headers.

For the detailed P12 and P345 bitstream rules, see
[`compression-reconstruction.md`](compression-reconstruction.md).

---

## 中文

`dsfwtool` 是面向文件的 Nintendo DS 固件工具。它可以在不拆出整个目录的
情况下读取固件结构、按显式文件导出组件、由显式文件重新生成固件，以及单独
处理压缩流或加密流。支持 256 KiB 及其更大整数倍容量的固件。

构建命令：

```text
make dsfwtool
```

Windows 下生成的可执行文件为 `release/dsfwtool.exe`。

### 组件与布局模型

主固件头通过 `-h` 导出为 0x180 字节文件。它记录五个主组件的起始位置，
也包含 P1/P2 加密所需的参数。P1、P2 的内层是 LZ10/P12 流，外层再进行
固件加密；P3、P4、P5 直接使用 P345 双静态 Huffman 格式，不带加密层。

FlashMe 固件在“逻辑容量减 0x980”的尾部位置有第二个 0x180 字节头，
通过 `-fh` 表示。它的 FP1、FP2 是未加密的 P12 流。七个组件在物理 ROM
地址中可以交错排列，因此创建时会按统一的物理地址顺序重新安排它们，再把
各自的新起始位置分别写回主头和 FlashMe 头。

部分 FlashMe 转储缺少最后的 0x200 字节设置扇区，物理文件长度会是
`0x...FE00`；其逻辑容量仍是下一个 256 KiB 边界。使用 `-b` 自动创建时会
保留这个较短的物理长度；显式指定 `-s` 则写出完整逻辑容量。

### 查看布局

```text
dsfwtool -i firmware.bin
dsfwtool -i firmware.bin -o layout.txt
```

`-i` 不导出组件。它会显示或写出主组件偏移、有效压缩长度、解码后的对齐值，
并在存在时显示已识别的 FlashMe 次级布局。

### 导出显式组件文件

不带修饰词时，组件按镜像内原始形式导出：

```text
dsfwtool -x firmware.bin -h header.bin \
  -p1 p1.encrypted -p2 p2.encrypted \
  -p3 p3.p345 -p4 p4.p345 -p5 p5.p345
```

导出明文时：

```text
dsfwtool -x firmware.bin -h header.bin \
  -p1 -decrypt -uncomp p1.plain \
  -p2 -decrypt -uncomp p2.plain \
  -p3 -uncomp p3.plain -p4 -uncomp p4.plain -p5 -uncomp p5.plain
```

P1/P2 的 `-uncomp` 只能紧跟在 `-decrypt` 后面。若 `-x` 中请求解密 P1
或 P2，必须同时导出 `-h`，因为该头部提供密钥材料。`-x` 可以只导出需要的
组件。

对于 FlashMe 镜像，可按需加入次级组件：

```text
dsfwtool -x flashme.bin -fh flash-header.bin \
  -fp1 fp1.p12 -fp2 -uncomp fp2.plain
```

FP1、FP2 不支持加密参数。

### 创建固件镜像

创建操作始终需要主头和全部五个主组件：

```text
dsfwtool -c rebuilt.bin -b firmware.bin -h header.bin \
  -p1 p1.encrypted -p2 p2.encrypted \
  -p3 p3.p345 -p4 p4.p345 -p5 p5.p345
```

`-b` 会保留基准镜像内所有非组件字节。不使用 `-b` 时，未使用的输出空间默认
填充 `FF`，可用 `--fill 00` 或 `--fill FF` 指定。`-s 256K`、`-s 512K`、
`-s 1M` 或 `-s auto` 用于选择逻辑输出容量。当重压缩后的 FlashMe 流无法装入
原有布局时，应显式选择更大的 `-s`。

主组件的输入模式如下：

| 组件 | 不带修饰词 | `-encrypt` | `-comp -encrypt` |
| --- | --- | --- | --- |
| P1/P2 | 已加密的 P12 流 | 已压缩、未加密的 P12 流 | 明文先压缩，再加密 |
| P3/P4/P5 | P345 流 | 不可用 | 仅使用 `-comp` 输入明文 |

例如，全部由明文重新构建主组件：

```text
dsfwtool -c rebuilt.bin -b firmware.bin -s 512K -h header.bin \
  -p1 -comp -encrypt p1.plain -p2 -comp -encrypt p2.plain \
  -p3 -comp p3.plain -p4 -comp p4.plain -p5 -comp p5.plain
```

创建 FlashMe 输出时，三个次级文件必须同时提供：

```text
dsfwtool -c flashme-rebuilt.bin -b flashme.bin -s 512K \
  -h header.bin \
  -p1 p1.encrypted -p2 p2.encrypted \
  -p3 p3.p345 -p4 p4.p345 -p5 p5.p345 \
  -fh flash-header.bin -fp1 -comp fp1.plain -fp2 -comp fp2.plain
```

FP1/FP2 不带修饰词时输入的是已有未加密 P12 流，带 `-comp` 时输入明文。
FlashMe 流可能由不同民间工具生成；这些工具可以采用不同但同样有效的 LZ
匹配选择。因此，转换后的 FlashMe 流应以“再次解压得到的明文和布局一致”为
准，而不要求压缩字节与原先流逐字节一致。

### 单独处理压缩或加密流

先选择组件类型。P1/P2 自动选择 P12 编解码器，只有加密或解密时需要 `-h`：

```text
dsfwtool -p1 -uncomp p1.p12 -o p1.plain
dsfwtool -p1 -decrypt p1.encrypted -h header.bin -o p1.p12
dsfwtool -p2 -decrypt -uncomp p2.encrypted -h header.bin -o p2.plain
dsfwtool -p1 -comp p1.plain -o p1.p12
dsfwtool -p1 -comp -crypt p1.plain -h header.bin -o p1.encrypted
dsfwtool -p3 -uncomp p3.p345 -o p3.plain
dsfwtool -p3 -comp p3.plain -o p3.p345
dsfwtool -fp1 -uncomp fp1.p12 -o fp1.plain
dsfwtool -fp1 -comp fp1.plain -o fp1.p12
```

`-p3`、`-p4`、`-p5` 自动选择 P345；`-fp1`、`-fp2` 自动选择未加密 P12。
P345 与 FlashMe 流会拒绝加密参数。

### 创建时修改头部

以下参数会在 P1/P2 加密之前修改所提供的主头。创建 FlashMe 时，相同修改也
会应用到 `-fh`：

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

`--timestamp` 也接受 `YYYY-MM-DDTHH:MM`。通用 `--set-*` 的偏移相对于
导出的 0x180 字节头文件。工具会依据最终布局写入 ROM 组件起始位置；若头中
声明了 Wi-Fi 配置校验区，也会重新计算其校验和。普通固件还会重新计算三组主
组件 CRC16；FlashMe 创建则保留主头和次级头中提供的组件 CRC16 字段。

P12、P345 的详细位流结构与编码规则见
[`compression-reconstruction.md`](compression-reconstruction.md)。
