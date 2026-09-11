# DS Firmware Compression Structures and Encoding Rules

This document describes the complete encoding algorithm used to reproduce
official compressed bytes. The wire format defines what a decoder accepts;
the inferred matching and tree-building rules determine which particular byte
sequence the encoder emits. Those rules describe a reconstruction of the
encoding behavior, rather than the original compressor's source code.

## P1/P2: LZ77 (LZ10)

### Stream structure

P1/P2 use an LZ77 stream with grouped flags. The four-byte header starts with
`0x10`, followed by the decompressed length stored as a 24-bit little-endian integer.

Each group begins with a flag byte describing up to eight tokens, read from the
most significant bit to the least significant bit:

- A zero bit denotes one literal byte.
- A one bit denotes a two-byte length/distance pair.

For match bytes `b1, b2`:

```text
length   = (b1 >> 4) + 3
distance = ((b1 & 0x0F) << 8 | b2) + 1
```

The format represents lengths of 3–18 bytes and distances of 1–4096 bytes.
Copy length bytes sequentially from distance bytes behind the current output
position. Source and destination may overlap. Decoding stops when the output
reaches the length declared in the header.

### Inferred encoding rules

Search for the longest match in a history window of up to 4096 bytes, limiting
the match to 18 bytes. Exclude distance 1 from encoding candidates, using
distances of 2–4096 instead. This is an encoder choice, not a limitation of the
distance field. Prefer the nearest candidate when longest match lengths tie.

Emit a back-reference immediately when a match of at least three bytes exists;
otherwise emit a literal. Add every consumed input byte to the history window
before processing the next position. Set unused flag bits in the last group to
zero. The last token needed to produce the declared output length determines
the effective end of the stream; encryption block padding is handled outside it.

### Encoder execution

The encoder maintains a circular history window and 256 candidate lists keyed
by the first byte. Each list links occurrences from oldest to newest. Before
inserting a byte into a full window, remove the expired occurrence from its
list; append the new occurrence to the tail of the corresponding list.

At position p, visit only the list for input[p]. Check the first three bytes,
then extend matching candidates up to min(18, remaining input). Ignore distance
1. Replace the selected candidate whenever its length is greater than or equal
to the best length: because traversal proceeds toward newer occurrences, this
implements the nearest-distance tie rule. Candidate order is an implementation
mechanism for this rule; it is not an additional field in the compressed stream.

Reserve a flag byte, then encode up to eight tokens. For a match of length L
and distance D, set the corresponding flag bit and write:

```text
b1 = ((L - 3) << 4) | ((D - 1) >> 8)
b2 = (D - 1) & 0xFF
```

Advance by L and insert all L consumed bytes into the history. For a literal,
leave the flag bit clear, write input[p], advance by one, and insert that byte.
Write the completed flag byte into its reserved position. No lookahead or
Huffman coding is applied to P1/P2.

### KEY1-derived tail word and encrypted-block alignment

The effective LZ stream ends at its last required token. To reproduce the
official encrypted component, construct its plaintext block padding separately
before applying KEY1. A four-byte tail word, when required, is derived from the
complete firmware identifier at header offset `0x08` and the fixed ARM7 BIOS
KEY1 table. The fixed identifier prefix `4D 41 43` participates in this derivation
along with the fourth byte.

Read the identifier as a little-endian 32-bit integer and initialize KEY1 with
`level = 2` and `modulo = 0x0C`. Let K be the completed expanded key table, indexed
as 32-bit words. Copy K[0] and K[1] into a separate eight-byte block, in that order,
with each word stored little-endian. Encrypt that block using K and retain the
first four output bytes:

```text
id = read_le32(header + 0x08)
K = KEY1_init(id, level=2, modulo=0x0C)
B = LE32(K[0]) || LE32(K[1])
R = KEY1_encrypt_block(K, B)
tail = R[0:4]
```

Here `||` means byte concatenation and `R[0:4]` selects bytes 0 through 3.
K must be fully expanded before the extra block encryption. Encrypt a copy of
its first two words, leaving the key table unchanged for component encryption.
This is a deterministic 64-bit result derived from the identifier, with its
first 32 bits retained in file order; no random seed or per-ID lookup table is
needed. It is distinct from encrypting an all-zero block with the completed K.

| Identifier bytes | Derived eight bytes, in file order | Tail word bytes |
| --- | --- | --- |
| `4D 41 43 50` (`MACP`) | `81 A6 5C B3 99 E1 0E D8` | `81 A6 5C B3` |
| `4D 41 43 67` (`MACg`) | `02 9D 99 57 6A 4E 90 2B` | `02 9D 99 57` |
| `4D 41 43 68` (`MACh`) | `AD 89 93 D2 2C 20 76 06` | `AD 89 93 D2` |
| `4D 41 43 69` (`MACi`) | `5E 25 1E 4B F1 5A ED D4` | `5E 25 1E 4B` |
| `4D 41 43 C2` (`MAC\xC2`) | `A8 85 A7 8F EA 14 E6 01` | `A8 85 A7 8F` (predicted) |

For example, MACP produces K[0] = `0xFB0FC3DF` and K[1] = `0x7B73F359`.
The extra encryption therefore takes `DF C3 0F FB 59 F3 73 7B` as its input
and yields `81 A6 5C B3 99 E1 0E D8`.
The MAC\xC2 tail remains a prediction: the available iQue v1 P1 and P2 streams
do not require the extra word, so neither component exposes it.

Let E be the effective compressed byte count and define:

```text
A4 = 4 * ceil(E / 4)
A8 = 8 * ceil(E / 8)
plaintext[0:E] = compressed_stream
plaintext[E:A4] = zero bytes
if A4 < A8:
    plaintext[A4:A8] = tail
ciphertext = KEY1_encrypt_blocks(K, plaintext[0:A8])
```

An already eight-byte-aligned stream needs no padding. For `E mod 8` equal to
1, 2, 3 or 4, append zeros to the four-byte boundary, then the derived word.
For residues 5, 6 or 7, only zero bytes are needed. P1 and P2 share the derived
word when they use the same identifier; their compressed contents and lengths
determine whether that word is present, not its value.

The tail and zero bytes are encrypted together with the effective stream.
After decryption of the complete ciphertext blocks, an LZ parser can identify
E and discard the padding; decompression does not consume it. Any further gap
to the next firmware component belongs to image layout, outside these encrypted
blocks. The numeric rule reconstructs the observed tail; whether the original
packer deliberately supplied it or inherited it from a working buffer is not
established by the rule itself.

## P3/P4/P5: LZ Matching with Two Static Huffman Streams

### Stream structure

P3/P4/P5 split LZ tokens into two bitstreams, each with its own static Huffman tree:

| Stream | Symbol | Meaning |
| --- | --- | --- |
| Primary | `0x000–0x0FF` | Literal byte |
| Primary | `0x100–0x1FF` | Match length minus 3, plus `0x100` |
| Distance | `0x000–0x7FF` | Backward distance minus 1 |

The header occupies 12 bytes:

| Offset | Contents |
| --- | --- |
| `0x00–0x03` | Little-endian descriptor: high-bit flag `0x80000000`; remaining portion stores four times the component span rounded up to a four-byte boundary |
| `0x04` | Flag byte `0x80` |
| `0x05–0x07` | Big-endian 24-bit decompressed length |
| `0x08–0x0B` | Big-endian distance-stream offset, relative to the component start |

The primary stream begins at `0x0C`; the distance stream begins at the declared
offset. Each stream contains its tree followed immediately by encoded data.
Bits are stored most significant bit first within each byte.

Trees use preorder serialization:

- Write 1 for an internal node, then recursively write its left and right subtrees.
- Write 0 for a leaf, followed by its symbol: nine bits for the primary tree or
  eleven bits for the distance tree.

To decode a symbol, start at the root and follow the left branch for 0 or the
right branch for 1 until reaching a leaf. A primary literal is output directly.
A primary length symbol consumes one distance symbol from the other stream,
then performs the corresponding overlapping copy.

Zero alignment bits between the end of the primary data and the distance-stream
offset place the second stream on a four-byte boundary. The distance data ends
when the declared decompressed length has been produced; the last effective byte
retains its trailing zero bits. The aligned span described by the header and the
last effective byte are distinct boundaries. The descriptor alone does not
determine the effective compressed length.

### Inferred LZ encoding rules

Use a 2048-byte window, distances of 1–2048 bytes, and match lengths of 3–258 bytes.
Apply the 258-byte limit before comparing candidates. Prefer the farthest
candidate when lengths tie.

When the current position has a match, examine the next input position as well.
Let their longest match lengths be `L0` and `L1`:

- If `L1 > L0 + 1`, emit the current byte as a literal and commit the match at
  the next position.
- Otherwise commit the current match immediately.
- If the current position has no match, emit a literal.

The delay lasts only one position. A position reached through this delay commits
its match without applying another lookahead decision.

### Inferred Huffman construction rules

After LZ parsing, count primary and distance symbols separately. Use those
frequencies as leaf weights and build the two trees independently:

1. Collect nonzero-frequency leaves in ascending symbol order.
2. Build a binary min-heap bottom-up by sifting from the last non-leaf heap
   position toward the root.
3. Remove the root as the new parent's left child. Replace it with the last
   heap entry, shrink the heap, and sift down.
4. Use the resulting root as the right child. Create a parent with the sum of
   the two weights and replace that root directly.
5. Sift the parent down and repeat until only the tree root remains.

All sift operations use the same rules: choose the left child when the two
children have equal weights; stop when the current node's weight is less than
or equal to the selected child's weight. Move downward only for a strictly
greater weight.

Tie selection, child orientation, and direct replacement of the second minimum
jointly determine the exact tree. Other tie rules can produce a Huffman tree
with the same weighted cost but different symbol codes. Preserve these
deterministic choices, serialize the tree in preorder, and encode the previously
determined LZ tokens using their corresponding tree paths.

### Encoder execution: two passes over the same input

Static Huffman codes require frequencies before data can be encoded. The encoder
therefore scans the input twice using the same deterministic LZ decision
function. The first pass counts symbols; the second pass repeats the decisions
and writes their Huffman codes. It does not need to store the whole token list.
Both passes start at position zero with the delayed-match flag cleared.

For each search at p, visit distances in ascending order from 1 through
min(p, 2048). Compare at most min(258, input length - p) bytes, allowing overlap
between the source and destination ranges. Update the best candidate on equal
lengths as well as greater lengths, so the last equal candidate is the farthest.

The decision function carries one boolean state, commit_next:

```text
(L0, D0) = longest_match(p)
if commit_next:
    commit_next = false
    emit the current match
else if a current match exists:
    (L1, D1) = longest_match(p + 1)
    if a next match exists and L1 > L0 + 1:
        emit literal input[p]
        commit_next = true
    else:
        emit match (L0, D0)
else:
    emit literal input[p]
```

A literal advances p by one; a match advances it by its length. For example,
L0 = 5 and L1 = 6 select the current match, while L0 = 5 and L1 = 7 emit a literal
and then commit the next match. After that one-position delay, the next match
is committed even if a further lookahead could find a longer match.

During the counting pass, a literal x increments primary_frequency[x].
A match (L, D) increments primary_frequency[256 + L - 3] and
distance_frequency[D - 1]. Lengths are counted as symbols, not expanded into
the frequencies of the copied bytes. Each match contributes exactly one symbol
to each stream; literals contribute nothing to the distance stream.

For an empty symbol population, the encoder supplies symbols 0 and 1 with
weight 1. For a single used symbol, it supplies one otherwise unused symbol
with weight 1, choosing 1 if the used symbol is 0, and 0 otherwise. These are
encoder edge-case rules for constructing a branching tree; the auxiliary
symbols are not added to the actual token sequence.

### Turning the trees into exact bytes

Number heap positions from 1. The children of position k are 2k and 2k + 1.
The essential sift operation is:

```text
value = heap[k]
while 2*k <= count:
    child = 2*k
    if child < count and heap[child+1].weight < heap[child].weight:
        child += 1
    if value.weight <= heap[child].weight:
        break
    heap[k] = heap[child]
    k = child
heap[k] = value
```

The strict child comparison and non-strict stopping comparison have different
roles. The former keeps the left child on equal weight; the latter prevents
the current node from moving past an equal-weight child. Initial leaf ordering,
bottom-up heap construction, and replacing the second minimum directly must
all be retained: Huffman optimality alone does not prescribe these choices.

Walk each completed tree to build a table of (code length, code bits) for every
symbol. Append 0 on a left edge and 1 on a right edge. Serialize the tree first,
then append the second pass's codes immediately after it without byte-aligning
the boundary between the tree and data.

Let B0 and B1 be the total bit counts of each tree plus its encoded data.
Define align4(n) = 4 * ceil(n / 4):

```text
S0 = align4(ceil(B0 / 8))
S1 = align4(ceil(B1 / 8))
distance_offset = 12 + S0
aligned_span = 12 + S0 + S1
effective_end = distance_offset + ceil(B1 / 8)
```

Zero-fill the in-memory stream areas to their aligned sizes, assemble the
12-byte header, and append the primary and distance streams in that order. The
first three header bytes contain the low 24 bits of aligned_span * 4 in
little-endian order, followed by 0x80. Write the decompressed length and
distance_offset in their specified big-endian fields. The descriptor keeps the
aligned span, but the stored component ends at effective_end: bytes after that
position are only final 0–3 byte construction alignment, not component data.
This distinction lets a firmware place the next component at its independently
aligned start without inventing a padded P3/P4/P5 payload.

The exact byte sequence follows from the entire chain: LZ choices determine
symbols and frequencies; deterministic heap operations determine trees and code
paths; preorder tree serialization and MSB-first bit packing determine the two
streams; alignment determines their offsets and header values. Matching only
decompressed data or total encoded length leaves these byte-level decisions
undetermined.

### FlashMe P345 variant (`-comp -flashme`)

FlashMe P3/P4/P5 streams use the same decompression format, but their encoder
does not make the retail encoder's LZ and Huffman choices. The alternate rule
was reconstructed by CTurt's CFW-Suite; its reference implementation is
[`compression.c`](https://github.com/CTurt/CFW-Suite/blob/f2f2ca1e6a31e7c32edd02682a539a224ec015b7/guiTool/source/compression.c).
`dsfwtool -p3|-p4|-p5 -comp -flashme` selects that rule explicitly.

- Search distances from nearest to farthest in the same 2048-byte window, make
  no lazy look-ahead decision, and keep the nearest candidate on an equal full
  match length.
- Compare the full remaining input when choosing a candidate, then cap only the
  emitted match token at 258 bytes.
- Build each Huffman tree by repeatedly scanning the active leaf/parent array
  in its existing order. Strict comparisons preserve the earlier order for
  equal weights; the two selected nodes become the left and right children.
- Word-align the primary stream for the declared distance offset, but retain
  only the final consumed byte of the distance stream. The firmware packer
  supplies physical zero alignment after the effective component end.

This mode applies only to primary P3/P4/P5 compression. P1/P2 continue to use
the separately reconstructed retail P12 compressor and do not recognise
`-flashme`.

---

# DS 固件压缩结构与编码规则

本文描述用于还原官方压缩字节的完整编码算法。格式规定解码器能够接受的
数据结构，而推断出的匹配选择与构树规则决定编码器最终输出哪一组具体字节。
这些规则是对编码行为的重建，并非官方压缩器的原始源码。

## P1/P2：LZ77（LZ10）

### 压缩结构

P1/P2 使用带分组标志位的 LZ77 压缩流。流头共 4 字节：首字节为
`0x10`，随后 3 字节以小端序记录解压后长度。

流头后按组编码，每组以一个标志字节开头，最多描述 8 个 token，
从最高位到最低位依次读取：

- 标志位为 0：读取一个字节，直接输出该字面量。
- 标志位为 1：读取两个字节，解析匹配长度和回溯距离。

对于匹配字节 `b1, b2`：

```text
length   = (b1 >> 4) + 3
distance = ((b1 & 0x0F) << 8 | b2) + 1
```

因此格式可以表达 3–18 字节的匹配长度和 1–4096 字节的距离。
从当前输出位置向前回溯 distance 字节，逐字节复制 length 字节；
源区间与目标区间可以重叠。累计输出达到头部声明长度时停止。

### 推断出的压缩规则

在最多 4096 字节的历史窗口内寻找最长匹配，长度上限为 18。
编码候选排除距离 1，即使用 2–4096 字节的回溯距离；这是编码选择，
并非距离字段无法表示 1。相同最长长度时选择最近的候选。

存在至少 3 字节的匹配时立即输出回引用，否则输出一个字面量。
每次输出后，将对应的全部明文字节纳入历史窗口，继续处理下一位置。
末组不足 8 个 token 时，剩余标志位置零。压缩流的有效终点由解压长度
对应的最后一个 token 决定，后续加密块对齐属于外层处理。

### 编码器执行流程

编码器维护一个环形历史窗口，以及按首字节分组的 256 条候选链表。
每条链表将出现位置按从旧到新的顺序连接。窗口已满时，先从对应链表中
移除过期位置，再将新位置追加到其首字节对应的链表尾部。

在位置 p，只遍历 input[p] 对应的候选链。先检查前三个字节，再将候选匹配
延伸至 min(18, 剩余输入长度)，排除距离 1。候选长度大于或等于当前最佳
长度时都更新选择：由于遍历顺序从旧到新，最终自然选择等长候选中最近的
位置。链表顺序是实现这一选择规则的机制，不是压缩流中的额外字段。

预留一个标志字节，然后编码最多八个 token。对于长度 L、距离 D 的匹配，
设置对应标志位，并写入：

```text
b1 = ((L - 3) << 4) | ((D - 1) >> 8)
b2 = (D - 1) & 0xFF
```

随后前进 L 字节，将这 L 个已消耗字节全部加入历史窗口。对于字面量，
保持对应标志位为零，写入 input[p]，前进一个字节并更新窗口。
一组处理完毕后，将标志字节写回预留位置。P1/P2 不进行向前观察或 Huffman 编码。

### KEY1 派生尾字与加密块对齐

LZ 有效流在最后一个必要 token 处结束。还原官方加密组件时，需要在应用
KEY1 前单独构造明文填充区。其中按需出现的四字节尾字，由固件头 `0x08`
处的完整标识符以及 ARM7 BIOS 固定 KEY1 表派生。标识符的固定前缀
`4D 41 43` 与第四个字节共同参与计算。

将标识符按小端序读取为 32 位整数，以 `level = 2`、`modulo = 0x0C`
初始化 KEY1。设 K 为完成扩展后的密钥表，以 32 位字索引。将 K[0] 和
K[1] 依次以小端序写入一个独立的八字节块，使用 K 加密该块，取输出的
前四字节：

```text
id = read_le32(header + 0x08)
K = KEY1_init(id, level=2, modulo=0x0C)
B = LE32(K[0]) || LE32(K[1])
R = KEY1_encrypt_block(K, B)
tail = R[0:4]
```

其中 `||` 表示字节拼接，`R[0:4]` 表示第 0 至第 3 字节。
必须先完成整个密钥表的扩展，再执行这次额外加密；应对表头两个字的副本
进行加密，保持密钥表不变，以便随后加密组件。这是由标识符确定的 64 位
结果，按文件顺序保留前 32 位，不需要随机种子或按 ID 查表。
它与使用完成后的 K 加密全零块是不同的运算。

| 标识符字节 | 派生的八字节结果，按文件顺序 | 尾字字节 |
| --- | --- | --- |
| `4D 41 43 50`（`MACP`） | `81 A6 5C B3 99 E1 0E D8` | `81 A6 5C B3` |
| `4D 41 43 67`（`MACg`） | `02 9D 99 57 6A 4E 90 2B` | `02 9D 99 57` |
| `4D 41 43 68`（`MACh`） | `AD 89 93 D2 2C 20 76 06` | `AD 89 93 D2` |
| `4D 41 43 69`（`MACi`） | `5E 25 1E 4B F1 5A ED D4` | `5E 25 1E 4B` |
| `4D 41 43 C2`（`MAC\xC2`） | `A8 85 A7 8F EA 14 E6 01` | `A8 85 A7 8F`（预测） |

例如，MACP 对应的 K[0] 为 `0xFB0FC3DF`，K[1] 为 `0x7B73F359`。
因此额外加密的输入为 `DF C3 0F FB 59 F3 73 7B`，输出为
`81 A6 5C B3 99 E1 0E D8`。
MAC\xC2 的尾字仍属于预测值：现有 iQue v1 的 P1、P2 压缩流均不需要
额外四字节尾字，因此这两个组件没有暴露该值。

设有效压缩字节数为 E，填充和加密过程为：

```text
A4 = 4 * ceil(E / 4)
A8 = 8 * ceil(E / 8)
plaintext[0:E] = compressed_stream
plaintext[E:A4] = 零字节
如果 A4 < A8：
    plaintext[A4:A8] = tail
ciphertext = KEY1_encrypt_blocks(K, plaintext[0:A8])
```

已经按八字节对齐时不添加填充。`E mod 8` 为 1、2、3、4 时，先补零到
四字节边界，再追加派生尾字；余数为 5、6、7 时，只需补零。
相同标识符的 P1、P2 共用一个派生尾字；组件内容和长度决定是否出现
这个尾字，不决定尾字的数值。

尾字和零填充与有效流一起加密。解密完整密文块后，LZ 解析器可确定 E，
再裁去填充；解压过程不消耗这些填充字节。到下一固件组件之间如果还有
空隙，属于这些加密块之外的镜像布局。上述数值规则可以重建已观察到的
尾字；官方打包器是有意提供这个值，还是从工作缓冲区带入，不能仅凭
这一数值规则确定。

## P3/P4/P5：LZ 匹配与双流静态 Huffman 编码

### 压缩结构

P3/P4/P5 将 LZ token 拆成两条分别使用静态 Huffman 树的位流：

| 位流 | 符号 | 含义 |
| --- | --- | --- |
| 主符号流 | `0x000–0x0FF` | 字面量字节 |
| 主符号流 | `0x100–0x1FF` | 匹配长度减 3，再加 `0x100` |
| 距离流 | `0x000–0x7FF` | 回溯距离减 1 |

流头共 12 字节：

| 偏移 | 内容 |
| --- | --- |
| `0x00–0x03` | 小端描述字：高位标志为 `0x80000000`，其余部分记录按 4 字节对齐后的组件跨度乘 4 |
| `0x04` | 标志字节 `0x80` |
| `0x05–0x07` | 大端的 24 位解压后长度 |
| `0x08–0x0B` | 大端的距离流起始偏移，相对组件起点 |

主符号流从 `0x0C` 开始，距离流从头部指定的偏移开始。
每条位流先保存自己的树，然后紧接该树编码的数据；位按字节内从高到低排列。

树以前序顺序序列化：

- 内部节点写入 1，再递归写入左子树和右子树。
- 叶子节点写入 0，再写入符号值；主符号树使用 9 位，距离树使用 11 位。

读取数据时，从根节点开始，每个 0 选择左分支，每个 1 选择右分支，
直到到达叶子。主流解出字面量时直接输出；解出长度符号时，
再从距离流读取一个距离符号，按 length 和 distance 执行重叠复制。

主流末尾至距离流起点之间包含零位对齐，使距离流按 4 字节边界开始。
距离流在累计输出达到解压长度时结束，最后一个有效字节保留尾部零位。
头部描述的对齐跨度与最后一个有效字节的位置需分别处理；
描述字本身不能替代有效压缩边界。

### 推断出的 LZ 编码规则

使用 2048 字节窗口，回溯距离为 1–2048，匹配长度为 3–258。
所有候选先受 258 字节长度上限约束，再比较长度；等长时选择最远距离。

在当前位置存在匹配时，向后看一个字节，分别记两个位置的最长匹配为
`L0` 和 `L1`：

- 若 `L1 > L0 + 1`，先输出当前位置的字面量，下一位置直接提交匹配。
- 否则立即提交当前位置的匹配。
- 当前位置没有匹配时，直接输出字面量。

延迟只持续一个位置；已经因延迟进入的下一位置不继续做延迟决策。

### 推断出的 Huffman 构树规则

完成 LZ 分词后，分别统计主符号和距离符号的出现次数，作为叶子的权重。
两棵树独立使用以下过程：

1. 按符号值升序收集非零权重的叶子。
2. 从最后一个非叶堆位置向根进行下沉，建立二叉最小堆。
3. 取出堆顶作为新父节点的左孩子，以堆尾填补堆顶，缩小堆并下沉。
4. 此时的堆顶作为右孩子；生成权重为两孩子之和的父节点，直接替换堆顶。
5. 对父节点进行下沉，重复直到只剩根节点。

每次下沉采用相同规则：两个孩子等权时选左孩子；当前节点权重小于或等于
所选孩子时停止，只有严格大于时才继续下沉。

等权选择、左右孩子顺序、直接替换第二个最小节点这三项共同决定树的具体形状。
不同的等权规则仍可能产生总代价相同的 Huffman 树，但会改变符号对应的位串。
因此编码流程需要保持这些确定性规则，再按前序顺序写出树，并用对应路径编码
已经确定的 LZ token。

### 编码器执行流程：对同一输入进行两遍扫描

静态 Huffman 编码必须先得到符号频率，因此编码器使用同一个确定性的 LZ
决策函数扫描输入两遍。第一遍统计频率，第二遍重现相同决策并写入相应的
Huffman 码，无需保存完整 token 列表。两遍扫描都从位置零开始，并清除
延迟匹配标志。

每次在位置 p 搜索时，按从近到远的顺序遍历 1 至 min(p, 2048) 的距离，
最多比较 min(258, 输入长度 - p) 字节，允许匹配源区间与目标区间重叠。
遇到等长候选也更新最佳结果，所以最后保留的等长候选具有最远距离。

决策函数维护一个布尔状态 commit_next：

```text
(L0, D0) = longest_match(p)
如果 commit_next 已设置：
    清除 commit_next
    输出当前位置的匹配
否则，如果当前位置存在匹配：
    (L1, D1) = longest_match(p + 1)
    如果下一位置存在匹配，且 L1 > L0 + 1：
        输出字面量 input[p]
        设置 commit_next
    否则：
        输出匹配 (L0, D0)
否则：
    输出字面量 input[p]
```

字面量使 p 前进一字节，匹配使 p 前进相应的匹配长度。例如 L0 = 5、
L1 = 6 时选择当前位置的匹配；L0 = 5、L1 = 7 时先输出字面量，再提交
下一位置的匹配。发生这一字节的延迟后，即使继续向前观察还能找到更长匹配，
也直接提交下一位置的匹配。

统计频率时，字面量 x 增加 primary_frequency[x]；匹配 (L, D) 分别增加
primary_frequency[256 + L - 3] 和 distance_frequency[D - 1]。
匹配长度作为一个符号计数，不展开成所复制明文字节的频率。
每次匹配恰好向两条流各贡献一个符号，字面量不向距离流贡献符号。

某一符号集合为空时，编码器补入权重均为 1 的符号 0 和 1；只有一个
有效符号时，补入一个权重为 1 的未使用符号：原符号为 0 时选 1，
否则选 0。这是编码器为边界输入构造分支树的规则，辅助符号不会加入实际
token 序列。

### 从树到确定的输出字节

堆下标从 1 开始，位置 k 的两个孩子为 2k 和 2k + 1。
下沉操作的核心为：

```text
value = heap[k]
while 2*k <= count:
    child = 2*k
    if child < count and heap[child+1].weight < heap[child].weight:
        child += 1
    if value.weight <= heap[child].weight:
        break
    heap[k] = heap[child]
    k = child
heap[k] = value
```

选择孩子时的严格小于与停止下沉时的小于等于作用不同：前者使等权时保留
左孩子，后者阻止当前节点越过等权孩子。初始叶子的排列、自底向上的建堆
顺序，以及直接替换第二个最小节点的操作都需要保留；Huffman 最优性本身
并未规定这些选择。

遍历完成的树，为每个符号建立“码长、码字”的查找表：向左追加 0，
向右追加 1。先写出树，再直接追加第二遍扫描产生的符号码字；
树与数据的交界处不进行字节对齐。

设 B0、B1 分别为两条流的“树加编码数据”总位数，
align4(n) = 4 * ceil(n / 4)，则：

```text
S0 = align4(ceil(B0 / 8))
S1 = align4(ceil(B1 / 8))
distance_offset = 12 + S0
aligned_span = 12 + S0 + S1
effective_end = distance_offset + ceil(B1 / 8)
```

在内存中将每条流以零补到对应的对齐长度，生成 12 字节头部，再依次连接
主流和距离流。头部前三字节以小端顺序保存 aligned_span * 4 的低 24 位，
第四字节写入 0x80；解压长度和 distance_offset 按各自规定的大端字段写入。
描述字仍记录 aligned_span，但实际写入的组件在 effective_end 结束；其后的
0–3 个字节仅是构造过程中的末尾对齐，并非组件数据。这样，固件可按下一组件
各自的起始对齐放置数据，而无需为 P3/P4/P5 虚构额外的填充负载。

完整的字节确定过程是：LZ 选择决定符号及频率，确定性的堆操作决定树形和
码字路径，前序树序列化与高位优先的位打包决定两条流，对齐决定偏移与头部值。
仅确定解压内容或总编码长度，还不足以确定这些逐字节的编码选择。

### FlashMe P345 变体（`-comp -flashme`）

FlashMe 的 P3/P4/P5 与官方固件共用相同的解压格式，但编码时的 LZ 与 Huffman
选择规则不同。该替代规则由 CTurt 的 CFW-Suite 复现；参考实现为
[`compression.c`](https://github.com/CTurt/CFW-Suite/blob/f2f2ca1e6a31e7c32edd02682a539a224ec015b7/guiTool/source/compression.c)。
`dsfwtool -p3|-p4|-p5 -comp -flashme` 会显式选择此规则。

- 在相同的 2048 字节窗口中从近到远搜索；不做 lazy 前瞻；完整匹配长度相等时保留
  最近的候选。
- 选择候选时比较到输入的有效末尾，仅在写出 token 时才把匹配长度截断到 258 字节。
- 每轮都按既有数组顺序扫描活动的叶节点和父节点来构建 Huffman 树。严格比较会让
  等权节点保留较早顺序；选出的两个节点依次成为左、右孩子。
- 主流仍按 4 字节对齐以确定距离流偏移；距离流只保留最后实际消耗的字节。固件组装器
  在组件有效末尾之后补写物理零对齐。

该模式只适用于主 P3/P4/P5 的压缩。P1/P2 仍使用单独复现的官方 P12 压缩器，且不识别
`-flashme`。
