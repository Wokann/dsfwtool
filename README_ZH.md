# dsfwtool v1.0

[English](README.md)

`dsfwtool` 是 Nintendo DS 固件命令行工具。它可显示固件布局、将指定组件导出到
命名路径、由头部和组件文件创建固件，并单独处理压缩流或加密流。支持 256 KiB
及其更大整数倍容量的固件。

## 构建环境

从源码构建需要 C99 编译器和 GNU Make。随仓库提供的 Makefile 已用 GCC/MinGW
验证；Clang 也可使用。Windows 下请在已将 `make` 和 C 编译器加入 `PATH` 的终端
中构建，例如 MSYS2 的 MinGW 终端。Linux、macOS 则先安装平台对应的 C 开发工具和
GNU Make。

## 编译与安装

在仓库根目录执行：

```text
make dsfwtool
```

Windows 下生成 `release/dsfwtool.exe`，类 Unix 系统下生成
`release/dsfwtool`。可用下列命令确认构建结果：

```text
release/dsfwtool.exe --version    # Windows
./release/dsfwtool --version      # Linux/macOS
```

项目没有单独的安装器，也没有 `make install` 目标；编译出的文件可直接运行。若要
在任意目录直接输入 `dsfwtool`，请将该可执行文件复制到已经位于用户 `PATH` 的目录。
例如在类 Unix 系统中：

```text
install -Dm755 release/dsfwtool "$HOME/.local/bin/dsfwtool"
```

随后确认 `$HOME/.local/bin` 已在 `PATH` 中。Windows 下可将
`release/dsfwtool.exe` 复制到自选目录，将该目录加入用户的 `Path` 环境变量，然后
重新打开终端。

下文统一以 `dsfwtool` 表示命令；如果尚未加入 `PATH`，请改用
`release/dsfwtool.exe` 或 `./release/dsfwtool`。

## 路径与输出

所有输出参数都是文件路径。路径可包含相对或绝对父目录；父目录不存在时，工具会
自动逐级创建。输入路径必须已经存在。

## 组件与布局

主固件头通过 `-h` 导出为 0x180 字节文件。它记录五个主组件的起始位置，也包含
P1/P2 加密所需的参数。P1、P2 的内层是 LZ10/P12 流，外层再进行固件加密；
P3、P4、P5 直接使用 P345 双静态 Huffman 格式，不带加密层。

FlashMe 固件默认在“逻辑容量减 0x980”的位置放置第二个 0x180 字节头，通过 `-fh`
表示：256 KiB 时为 `0x3F680`，512 KiB 时为 `0x7F680`。它的 FP1、FP2 是未加密的
P12 流。七个组件在物理 ROM 地址中可以交错排列，因此创建时会按统一的物理地址顺序
重新安排它们，再把各自的新起始位置写回主头和 FlashMe 头。每个组件及次级头本身都
可通过局部的 `--offset` 固定到指定物理地址。

部分 FlashMe 转储缺少最后的 0x200 字节设置扇区，物理文件长度会是
`0x...FE00`；其逻辑容量仍是下一个 256 KiB 边界。创建时总是写出完整逻辑容量；
可用 `-s` 选择更大的 256 KiB 整数倍容量。

## 命令总览

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
  -p1 [-encrypt | -comp -encrypt] [--offset OFFSET] FILE
  -p2 [-encrypt | -comp -encrypt] [--offset OFFSET] FILE
  -p3 [-comp] [--offset OFFSET] FILE -p4 [-comp] [--offset OFFSET] FILE
  -p5 [-comp] [--offset OFFSET] FILE
  [-fh [--offset OFFSET] FLASH_HEADER.bin
   -fp1 [-comp] [--offset OFFSET] FILE -fp2 [-comp] [--offset OFFSET] FILE]
  [-b BASE.bin] [-s 256K|512K|1M|auto] [--fill 00|FF]
  [头部修改参数]

dsfwtool -p1|-p2|-p3|-p4|-p5|-fp1|-fp2
  (-comp|-uncomp|-crypt|-decrypt) INPUT.bin [-h HEADER.bin] -o OUTPUT.bin
```

`-i` 只读取布局信息。`-x` 至少需要指定一个输出文件；不带修饰词时，组件保持
镜像内的原始形式。P1/P2 的 `-uncomp` 只能出现在 `-decrypt` 后面；若解密 P1
或 P2，必须在同一条 `-x` 命令中输出 `-h`，因为头部提供密钥材料。

`-c` 始终需要 `-h` 与全部 P1 至 P5。只要指定任何 FlashMe 文件，就必须同时
指定 `-fh`、`-fp1`、`-fp2`。P1/P2 不带修饰词时输入的是已加密 P12 流；
`-encrypt` 输入已压缩但未加密的 P12 流；`-comp -encrypt` 输入明文并连续压缩、
加密。P3/P4/P5 仅可用 `-comp` 输入明文。独立 P1/P2 操作中，`-crypt` 是
`-encrypt` 的同义写法。

在 `-c` 中，`--offset OFFSET` 表示物理 ROM 字节地址；它必须放在该组件的转换修饰词
之后、文件名之前。P1/P2/P3/P4/P5 和 FP1/FP2 必须满足各自头中编码的起始对齐，`-fh`
只需完整落在输出镜像内。所有碰撞判断均使用压缩和/或加密后的最终大小。主头保护
`0x000000`--`0x00017F`；普通固件保护末尾 `0x600` 字节 Wi-Fi 与用户设置区；FlashMe
保护其 0x180 字节次级头及仅末尾 `0x200` 字节用户设置区，因此旧版 FlashMe 可以占用
前方 Wi-Fi 设置区域。工具始终依据指定后的物理地址重写主头中的 P1--P5 起始字段，
以及 FlashMe 头中的 FP1/FP2 起始字段。为兼容通常的 FlashMe 布局，请使用
`-fh --offset 0x3F680`（256 KiB）或 `0x7F680`（512 KiB），或省略该选项以自动选择
对应默认位置。

`-b BASE.bin` 仅能和 `-c` 一起使用。工具会依据基础固件头的用户设置指针，读取其
对应的 0x600 字节主机专属尾区（Wi-Fi 接入点设置和两份用户设置），并迁移到新镜像
的末尾。主头以及存在时的 FlashMe 次级头都会改为指向新的末尾用户设置区。在 FlashMe
模式下，基础尾区会先于组件写入；因此显式放置的组件可以覆盖其前方 0x400 字节 Wi-Fi
区域，而末尾 0x200 字节用户设置区始终受保护。`-b` 与 `-s` 可以同时使用，因此可在
256 KiB 与 512 KiB 容量之间重建并保留可读取的尾区数据。若基础转储缺少最后的用户
设置扇区，工具会迁移其实际存在的字节，缺失部分保持为所选填充值。固件头的用户设置
指针是按 8 字节计的 16 位值，末尾迁移最多只能表示 512 KiB 容量，因此 `-b` 会拒绝
更大的目标容量。

`-p1`、`-p2` 选择 P12；`-p3`、`-p4`、`-p5` 选择 P345；`-fp1`、`-fp2` 选择
未加密 P12。加密与解密仅适用于主 P1/P2，且需要 `-h`。独立 P1/P2 操作可以是
单一步骤、`-decrypt -uncomp`，或 `-comp -crypt`；P345、FP1、FP2 则必须且只能
选择 `-comp` 或 `-uncomp` 之一。

P1/P2 解密只接受完整的 8 字节对齐密文，并在处理完所有分组后仅导出有效 P12
流。P1/P2 加密可接受任意非空输入：已经 8 字节对齐的输入会原样进入加密；否则
加密层会先补零至 4 字节边界；若还需一个字才能达到 8 字节边界，则通过完整 ID 的
KEY1 计算该尾字。已对齐输入中的显式尾字会保留。具体规则见
[尾字派生说明](docs/compression-reconstruction.md#key1-派生尾字与加密块对齐)。

## 查看固件信息

```text
dsfwtool -i firmware.bin
dsfwtool -i firmware.bin -o reports/layout.txt
```

`-i` 不导出组件。它会显示或写出主组件偏移、有效压缩长度、解码后的对齐值，并在
存在时显示已识别的 FlashMe 次级布局。

## 导出组件

不带修饰词时，组件按镜像内原始形式导出：

```text
dsfwtool -x firmware.bin -h unpack/header.bin \
  -p1 unpack/p1.encrypted -p2 unpack/p2.encrypted \
  -p3 unpack/p3.p345 -p4 unpack/p4.p345 -p5 unpack/p5.p345
```

导出明文时：

```text
dsfwtool -x firmware.bin -h unpack/header.bin \
  -p1 -decrypt -uncomp unpack/p1.plain \
  -p2 -decrypt -uncomp unpack/p2.plain \
  -p3 -uncomp unpack/p3.plain \
  -p4 -uncomp unpack/p4.plain \
  -p5 -uncomp unpack/p5.plain
```

P1/P2 的 `-uncomp` 只能紧跟在 `-decrypt` 后面。若 `-x` 中请求解密 P1 或 P2，
必须同时导出 `-h`，因为该头部提供密钥材料。`-x` 可以只导出需要的组件。

对于 FlashMe 镜像，可按需加入次级组件：

```text
dsfwtool -x flashme.bin -fh unpack/flash-header.bin \
  -fp1 unpack/fp1.p12 -fp2 -uncomp unpack/fp2.plain
```

FP1、FP2 不支持加密参数。

## 创建固件

创建操作始终需要主头和全部五个主组件：

```text
dsfwtool -c rebuilt.bin -h unpack/header.bin \
  -p1 unpack/p1.encrypted -p2 unpack/p2.encrypted \
  -p3 unpack/p3.p345 -p4 unpack/p4.p345 -p5 unpack/p5.p345
```

未使用的输出空间默认填充 `FF`，可用 `--fill 00` 或 `--fill FF` 指定。省略 `-s`
或使用 `-s auto` 时，工具选择能容纳所有组件的最小 256 KiB 整数倍；`-s 256K`、
`-s 512K`、`-s 1M` 可显式选择容量。当重压缩后的 FlashMe 流无法装入原有布局时，
应显式选择更大的 `-s`。

需要在改变容量时保留主机末尾的 Wi-Fi 与用户设置，可加入基础固件：

```text
dsfwtool -c rebuilt-512.bin -b original.bin -s 512K -h unpack/header.bin \
  -p1 unpack/p1.encrypted -p2 unpack/p2.encrypted \
  -p3 unpack/p3.p345 -p4 unpack/p4.p345 -p5 unpack/p5.p345
```

P3、P4、P5 的导出文件只保存有效 P345 位流。组装镜像时，工具会从每个此类位流的
有效末尾写入 `00`，直至下一个 8 字节边界；它不会覆盖后续组件、FlashMe 次级头或
受保护的设置尾区。该填充不属于组件文件，也不受 `--fill` 影响。P1、P2 的 8 字节填充则属于加密层本身：
先补零到 4 字节边界，若距离 8 字节对齐还差一个字，则由头部完整 ID 通过 KEY1
计算该尾字，再将填充和有效流一起加密。已按 8 字节对齐的输入会保留其显式尾字。
具体规则见[尾字派生说明](docs/compression-reconstruction.md#key1-派生尾字与加密块对齐)。

主组件的输入模式如下：

| 组件 | 不带修饰词 | `-encrypt` | `-comp -encrypt` |
| --- | --- | --- | --- |
| P1/P2 | 已加密的 P12 流 | 已压缩、未加密的 P12 流 | 明文先压缩，再加密 |
| P3/P4/P5 | P345 流 | 不可用 | 仅使用 `-comp` 输入明文 |

例如，全部由明文重新构建主组件：

```text
dsfwtool -c rebuilt.bin -s 512K -h unpack/header.bin \
  -p1 -comp -encrypt unpack/p1.plain \
  -p2 -comp -encrypt unpack/p2.plain \
  -p3 -comp unpack/p3.plain \
  -p4 -comp unpack/p4.plain \
  -p5 -comp unpack/p5.plain
```

创建 FlashMe 输出时，三个次级文件必须同时提供：

```text
dsfwtool -c flashme-rebuilt.bin -s 512K \
  -h unpack/header.bin \
  -p1 unpack/p1.encrypted -p2 unpack/p2.encrypted \
  -p3 unpack/p3.p345 -p4 unpack/p4.p345 -p5 unpack/p5.p345 \
  -fh unpack/flash-header.bin \
  -fp1 -comp unpack/fp1.plain -fp2 -comp unpack/fp2.plain
```

FP1/FP2 不带修饰词时输入的是已有未加密 P12 流，带 `-comp` 时输入明文。
FlashMe 的兼容性以解压后的数据和最终布局为准，不要求压缩字节与已有流逐字节
一致。

## 单独处理压缩或加密流

先选择组件类型。P1/P2 自动选择 P12 编解码器，只有加密或解密时需要 `-h`：

```text
dsfwtool -p1 -uncomp p1.p12 -o output/p1.plain
dsfwtool -p1 -decrypt p1.encrypted -h header.bin -o output/p1.p12
dsfwtool -p2 -decrypt -uncomp p2.encrypted -h header.bin -o output/p2.plain
dsfwtool -p1 -comp p1.plain -o output/p1.p12
dsfwtool -p1 -comp -crypt p1.plain -h header.bin -o output/p1.encrypted
dsfwtool -p3 -uncomp p3.p345 -o output/p3.plain
dsfwtool -p3 -comp p3.plain -o output/p3.p345
dsfwtool -fp1 -uncomp fp1.p12 -o output/fp1.plain
dsfwtool -fp1 -comp fp1.plain -o output/fp1.p12
```

`-p3`、`-p4`、`-p5` 自动选择 P345；`-fp1`、`-fp2` 自动选择未加密 P12。
P345 与 FlashMe 流会拒绝加密参数。

## 创建时修改头部

以下参数会在 P1/P2 加密之前修改所提供的主头。创建 FlashMe 时，相同修改也会
应用到 `-fh`：

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

`--timestamp` 也接受 `YYYY-MM-DDTHH:MM`。通用 `--set-*` 的偏移相对于导出的
0x180 字节头文件。工具会依据最终布局写入 ROM 组件起始位置；若头中声明了 Wi-Fi
配置校验区，也会重新计算其校验和。普通固件还会重新计算三组主组件 CRC16；
FlashMe 创建则保留主头和次级头中提供的组件 CRC16 字段。

P12、P345 的详细位流结构与编码规则见
[压缩结构说明](docs/compression-reconstruction.md)。
