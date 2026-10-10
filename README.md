# proj1 — 迷你搜索引擎（Mini Search Engine）

在莎士比亚全集上实现倒排索引搜索引擎（数据结构课程项目）。

- 题目要求：[problem.md](problem.md)（中文翻译见 [problem_zh.md](problem_zh.md)）
- 设计文档与进度：[plan.md](plan.md)
- 开发笔记（坑与发现，报告素材）：[notes.md](notes.md)
- 语料：The Complete Works of William Shakespeare（[shakespeare.mit.edu](http://shakespeare.mit.edu/)
  的官方 GitHub 镜像，commit `6b82db8`，见 `data/raw/SOURCE.txt`）

## 环境要求

| 依赖 | 用途 |
|------|------|
| C++17 编译器（clang++ / g++） | 编译 `build/mse` |
| `make` | 构建与测试 |
| Python 3.8+ | 仅 `tools/` 下的语料处理脚本；日常使用**不需要**（语料已随仓库提交） |

## 快速开始

```bash
make            # 编译，产物为 build/mse
make test       # 运行单元测试
./build/mse stats
```

> 以下命令都默认在 `proj1/` 目录下执行；路径选项均可用 `--manifest <path>` 覆盖。

## 目录结构

```text
proj1/
├── include/                  # C++ 头文件（接口）
│   ├── corpus.hpp            #   文档存储：manifest + 文本层 → 内存
│   └── tokenizer.hpp         #   切词：文本行 → 规范化 token 流
├── src/                      # C++ 实现（与 include/ 一一对应）
│   ├── corpus.cpp
│   ├── tokenizer.cpp
│   └── main.cpp              #   CLI 入口
├── tests/                    # 单元测试（make test）
├── tools/                    # 语料处理脚本（Python / Bash）
├── data/
│   ├── raw/                  # 原始 HTML 镜像（1211 个文件，含 SOURCE.txt）
│   ├── text/                 # 行式文本层（195 个 .txt，格式说明见其 README.md）
│   └── manifest.tsv          # 文档表：docid ↔ 标题/路径/URL
├── build/                    # 编译产物（gitignore，可 make clean 删除）
├── Makefile
└── problem.md / problem_zh.md / plan.md
```

## 构建与测试

```bash
make            # 编译 build/mse（-Iinclude -std=c++17 -O2 -Wall -Wextra，要求零警告）
make test       # 编译并运行 tests/ 下的单元测试
make clean      # 删除 build/
```

新增模块时把接口放 `include/xxx.hpp`、实现放 `src/xxx.cpp` 即可，Makefile 会自动纳入编译。

## 命令参考

### `mse stats` — 语料统计

```bash
./build/mse stats [--manifest data/manifest.tsv]
```

读取 manifest 与全部文本文件，打印文档数、行数（按类型）、词数、字节数：

```text
manifest : data/manifest.tsv
documents: 195
lines    : 152632
  body   : 114315
  heading: 1099
  stage  : 6251
  speaker: 30967
words    : 930911
bytes    : 4919371
documents by category:
  Comedy: 17
  History: 10
  Poetry: 158
  Tragedy: 10
```

### `mse tokenize` — 查看 token 流（调试/实验用）

```bash
./build/mse tokenize [--doc <id> | --all] [--limit <n>]
                     [--keep-headings] [--keep-speakers] [--keep-stage]
```

| 选项 | 说明 |
|------|------|
| `--doc <id>` | 文档 ID（1–195，默认 1），见 `data/manifest.tsv` |
| `--all` | 不打印样本，对每篇文档输出一行 `docid<TAB>token数`（供脚本用） |
| `--limit <n>` | 打印前 n 个 token（默认 20） |
| `--keep-headings` | 把 `[ACT I]` / `[SCENE ...]` 标题行也切词（默认过滤） |
| `--keep-speakers` | 把 `@BERNARDO` 说话人标签也切词（默认过滤） |
| `--keep-stage` | 把 `(Exit Ghost)` 舞台指示也切词（默认过滤） |

默认只统计台词/诗行正文：

```text
$ ./build/mse tokenize --doc 30 --limit 5
doc#30 "Hamlet" (Tragedy / play)
options: headings=off speakers=off stage=off
tokens : 29671
sample :
  line 4	who's
  line 4	there
  line 6	nay
  line 6	answer
  line 6	me
```

加上 `--keep-speakers` 后，说话人（`@BERNARDO`）也会进入 token 流：

```text
$ ./build/mse tokenize --doc 30 --limit 5 --keep-speakers
doc#30 "Hamlet" (Tragedy / play)
options: headings=off speakers=on stage=off
tokens : 31151
sample :
  line 3	bernardo
  line 4	who's
  line 4	there
  line 5	francisco
  line 6	nay
```

批量统计（如做 Task 1 的词频实验时对比"含/不含说话人"的总量差异）：

```bash
./build/mse tokenize --all | awk -F'\t' '{s+=$2} END {print s}'
```

### `mse count` — 词频统计（Task 1）

```bash
./build/mse count [--manifest data/manifest.tsv] [--csv build/wordcount.csv]
                  [--top 50] [--keep-headings] [--keep-speakers] [--keep-stage]
```

统计每个词的 **tf**（总出现次数）与 **df**（出现在多少篇文档里），写入 CSV，
并在终端打印摘要与 top 表：

```text
$ ./build/mse count --top 5
manifest : data/manifest.tsv
documents: 195
tokens   : 866626
distinct : 25705
hapax    : 9753 (tf = 1)
csv      : build/wordcount.csv
stop-candidates (df_ratio>=0.75 && tf_share>=0.1%): 9
  the and i to of my that in but

top 5 by term frequency:
  rank	term	tf	df	df%	share%
  1	the	28436	177	90.8	3.3
  2	and	25553	189	96.9	2.9
  3	i	21131	154	79.0	2.4
  4	to	20302	187	95.9	2.3
  5	of	16976	175	89.7	2.0
```

CSV 列：`rank, term, tf, df, df_ratio`（=df/文档数）`, tf_share`（=tf/总词数）`, stop_candidate`
（候选停用词标记：`df_ratio ≥ 0.75 且 tf_share ≥ 0.1%`，规则见 plan.md §4.2）。

交叉验证（独立 Python 实现，输出应与 C++ 逐字节一致）：

```bash
python3 tools/check_wordcount.py > build/wordcount_py.csv
diff build/wordcount.csv build/wordcount_py.csv && echo identical
```

### 规划中的命令

`build`（建倒排索引）、`dump`、`query`、`shell`、`selftest`
——按 `plan.md` §14 的里程碑逐步补齐，当前会提示 `not-yet-implemented`。

## 工具脚本（重建语料时才需要）

日常开发不需要运行这些：`data/` 的内容已随仓库提交且可复现。

```bash
bash tools/fetch_corpus.sh [--force]   # ① 从 GitHub 镜像下载 HTML 到 data/raw/（需要联网）
python3 tools/extract.py               # ② 生成 data/text/ + data/manifest.tsv
```

- `--force` 用于重新抓取：旧目录会先备份为 `data/raw.bak.<时间戳>`，不会直接删除。
- 抓取结果在 `data/raw/SOURCE.txt` 记录仓库地址、commit、抓取日期与文件数。
- `extract.py` 的输入输出路径均可用 `--raw / --text / --manifest` 覆盖。

## 数据格式速查

`data/manifest.tsv`（制表符分隔，第 1 行是表头）：

```text
docid	kind	category	title	text	source	url
30	play	Tragedy	Hamlet	data/text/0030_hamlet.txt	data/raw/hamlet/full.html	http://shakespeare.mit.edu/hamlet/full.html
```

`data/text/*.txt`（每行一个单元，行首标记区分类型，详见 `data/text/README.md`）：

| 标记 | 含义 | 默认 |
|------|------|------|
| `[ACT I]` / `[SCENE ...]` | 标题行 | 过滤 |
| `(Exit Ghost)` | 舞台指示（含从诗句拆出的 `[Aside]` 等） | 过滤 |
| `@BERNARDO` | 说话人标签 | 过滤 |
| 其他 | 台词 / 诗行 / 散文正文 | 参与检索 |
| `#` 开头 | 文件头注释（docID、标题、来源） | 跳过 |

语料规模：195 个文档（37 部剧 + 154 首十四行诗 + 4 首长诗），152,632 行，930,911 词；
默认配置下切出 866,626 个正文 token。

## 开发约定

- 头文件放 `include/*.hpp`，实现放 `src/*.cpp`，单元测试放 `tests/`；
- 注释率目标 ≥35%（课程硬性要求 30%，不达标代码不计分）；
- 外部代码（如 Porter 词干化）单独文件标注来源，与自写代码区分；
- `build/` 与临时文件不入库；提交课程作业的 zip 中不要包含 `.git`。
