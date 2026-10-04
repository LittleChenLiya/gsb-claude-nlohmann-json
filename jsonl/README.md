# jsonl_decoder — 增量式 JSONL（JSON Lines / NDJSON）解码器

`jsonl_decoder.hpp` 是一个独立的头文件模块，复用本仓库现有的
`nlohmann::json` 解析器，从**任意小的字节块**中逐条解码 JSONL 记录，
**不需要把整份文件读入内存**。

## 文件

| 文件 | 说明 |
| --- | --- |
| `jsonl/jsonl_decoder.hpp` | 解码器模块（仅头文件，依赖 `nlohmann/json.hpp`） |
| `jsonl/test_jsonl.cpp`    | 验收测试（断言式，无需测试框架） |
| `jsonl/demo.cpp`          | 短分块演示（每块 3 字节） |

## 构建与运行（仓库根目录下）

```sh
g++ -std=c++17 -I single_include -I jsonl jsonl/test_jsonl.cpp -o jsonl/test_jsonl
./jsonl/test_jsonl        # 输出: all jsonl decoder tests passed

g++ -std=c++17 -I single_include -I jsonl jsonl/demo.cpp -o jsonl/demo
./jsonl/demo
```

## 用法

```cpp
#include "jsonl_decoder.hpp"

nlohmann::jsonl::decoder dec(max_record_bytes);   // 单条最大字节数
for (/* 每个到达的字节块 */)
    for (auto& rec : dec.feed(data, size))        // 返回本块新完成的记录
        handle(rec);
for (auto& rec : dec.finish())                    // 冲刷最后一条无换行记录
    handle(rec);
```

## 语义约定

- **记录边界**：只有物理换行字节 `0x0A`（LF）结束一条记录。JSON 字符串
  里的转义换行是两个字节（`\` 和 `n`），永远不会被当作边界——即使
  反斜杠和 `n` 落在不同的块里。
- **CRLF**：LF 前的 CR 会被剥离，不进入解析器。
- **分块 UTF-8**：解码器按字节缓冲整条记录后再交给 JSON 解析器，多字节
  字符可以被块边界任意切开（测试覆盖所有切分位置）。
- **最后一条无换行记录**：`finish()` 时冲刷。
- **空白行策略**：空行和仅含 JSON 空白（空格/制表符/CR）的行被**跳过**，
  不计入记录号。
- **单条最大字节数**：按**字节**（UTF-8 码元）计，不是字符数——例如
  `中` 算 3 字节。行尾 LF 不计入，CRLF 的 CR 计入。超限立即报错。
- **EOF 半字符**：`finish()` 时若缓冲末尾是不完整的 UTF-8 序列，报错并
  指向不完整的起始字节；非法（而非截断）的字节序列交由 JSON 解析器报告。

## 错误报告与失败锁存

所有解码错误抛出 `nlohmann::jsonl::error`（派生自
`std::runtime_error`），携带：

- `record()`：1 起始的记录号（空白行不计）；
- `byte_offset()`：全局 0 起始字节位置——
  - JSON 非法 → 出错记录的**首字节**位置；
  - 超限 → 触发超限的那个字节的位置；
  - UTF-8 截断 → 不完整起始字节的位置。

一旦出错，解码器进入**锁存失败态**：之后的 `feed()`/`finish()` 一律
重新抛出同一个错误，**不会偷偷恢复**、不会再产出任何记录。已经交付
（按值返回）的记录是普通 `json` 值，始终有效，与后续失败无关。

## 验收场景对应

- 两条含中文和转义换行的 JSON 按每块 3 字节送入、逐条输出：
  `test_acceptance_two_records_3byte_chunks` + `demo.cpp` 场景 1。
- 第二条坏 JSON：第一条仍保留，错误指向第二条（记录号 2、全局字节
  位置 = 第二条首字节）：`test_bad_second_record` + `demo.cpp` 场景 2。
- 上限按字节而非字符：`test_limit_counts_bytes_not_chars`。
- 空白行策略：`test_blank_lines_skipped`。
- EOF 半字符：`test_eof_partial_utf8`。
- 超限及失败锁存：`test_over_limit_latches`。
- CRLF / 末条无换行 / 全切分位置：`test_crlf`、
  `test_final_record_without_newline`、`test_chunked_utf8_all_splits`。
