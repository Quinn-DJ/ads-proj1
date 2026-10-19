# proj1 — 迷你搜索引擎（Mini Search Engine）

一个**语料无关**的倒排索引搜索引擎（数据结构课程项目），主语料为莎士比亚全集，
另附 RFC 文档第二语料验证通用性。

- 题目要求：[problem.md](problem.md)（中文翻译见 [problem_zh.md](problem_zh.md)）
- 设计文档与进度：[plan.md](plan.md)
- 开发笔记（坑与发现，报告素材）：[notes.md](notes.md)
- 主语料：The Complete Works of William Shakespeare（[shakespeare.mit.edu](http://shakespeare.mit.edu/)
  的官方 GitHub 镜像，commit `6b82db8`，见 `data/raw/SOURCE.txt`）
- 第二语料：IETF RFC 1035 / 5321 / 7231 纯文本（`data2/`，通用导入器产物，见 `data2/raw/SOURCE.txt`）

**引擎与语料解耦**：C++ 程序只读 manifest 与行式文本层，`kind/category` 等字段按不透明字符串
处理；"莎士比亚性"全部在数据与生产工具层（`tools/extract.py`），"任意纯文本"由
`tools/import_corpus.py` 导入（manifest 可选第 8 列 `format=plain` 跳过行标记分类）。两个语料
共用同一套引擎与测试设施（见[第二语料与通用导入器](#第二语料与通用导入器)）。

## 环境要求

| 依赖 | 用途 |
|------|------|
| C++17 编译器（clang++ / g++ / MSVC） | 编译 `build/mse` |
| CMake ≥ 3.16（生成器任选：Ninja / Make / Visual Studio） | 配置、构建与测试 |
| Python 3.8+ | 仅 `tools/` 下的语料处理脚本；日常使用**不需要**（语料已随仓库提交） |

## 快速开始

```bash
cmake -S . -B build                          # 配置，产物目录 build/
cmake --build build                          # 编译，产物为 build/mse
ctest --test-dir build --output-on-failure   # 运行单元测试
./build/mse stats
```

> Windows 上不加 `-G` 时默认走 Visual Studio（多配置生成器）：产物是 `build/Release/mse.exe`，
> 测试要写 `ctest --test-dir build -C Release`；想和上面完全一致就加 `-G Ninja`。

> 以下命令都默认在 `proj1/` 目录下执行；路径选项均可用 `--manifest <path>` 覆盖。

## 目录结构

```text
proj1/
├── include/                  # C++ 头文件（接口）
│   ├── corpus.hpp            #   文档存储：manifest + 文本层 → 内存
│   ├── tokenizer.hpp         #   切词：文本行 → 规范化 token 流
│   ├── stopwords.hpp         #   停用词表加载与 O(1) 查询
│   ├── stemmer.hpp           #   Porter 词干化（论文 + 作者 canonical 改进）
│   ├── posting.hpp           #   倒排记录表：docID/tf/positions
│   ├── dict.hpp              #   词典接口：伸展树（主）+ 哈希（基线）
│   ├── index.hpp             #   索引构建/序列化/加载/不变量/ dump
│   │                         #   + B+Tree 词典 sidecar 与 LazyIndexReader
│   ├── bptree.hpp           #   页式 B+Tree（磁盘词典，Bonus 实做）
│   ├── query.hpp             #   查询：word/phrase/boolean + 四个阈值
│   └── count.hpp             #   词频统计：tf/df 表（Task 1）
├── src/                      # C++ 实现（与 include/ 一一对应）
│   ├── corpus.cpp
│   ├── tokenizer.cpp
│   ├── stopwords.cpp
│   ├── stemmer.cpp
│   ├── posting.cpp
│   ├── dict.cpp
│   ├── index.cpp
│   ├── query.cpp
│   ├── count.cpp
│   └── main.cpp              #   CLI 入口
├── tests/                    # 单元测试（ctest）
│   ├── CMakeLists.txt        #   用例清单：新增 test_*.cpp 后登记到 MSE_TESTS
│   ├── test_tokenizer.cpp    #   tokenizer 用例（20 条边界）
│   ├── test_stemmer.cpp      #   Porter 用例（论文示例词 + 收缩词 + 护栏）
│   ├── test_dict.cpp         #   词典用例（行为脚本 + 跨实现互验）
│   ├── test_index.cpp        #   索引用例（黄金样例 + 往返 + 确定性）
│   ├── test_query.cpp        #   查询用例（三形态 + 四阈值 + 停用词/词干化）
│   ├── test_stopwords.cpp    #   停用词表用例（39 词/边界词/错误路径）
│   ├── test_count.cpp        #   词频熵特征 + 推导规则用例（golden4 手算公式）
│   ├── fixtures/golden4/     #   讲义四文档黄金样例（data/manifest + 4 篇文本）
│   └── vendor/catch2/        #   vendored Catch2 v3（仅测试链接；mse 本体零依赖）
├── tools/                    # 语料处理脚本（Python / Bash）
│   ├── fetch_corpus.sh       #   莎士比亚镜像抓取（data/raw/）
│   ├── extract.py            #   莎士比亚 HTML → 文本层 + manifest（marked）
│   └── import_corpus.py      #   通用导入器：任意 .txt 目录 → 语料（plain）
├── reference/                # 词干化参考资料（Porter/Snowball/NLTK；来源与许可见其 README.md）
├── data/
│   ├── raw/                  # 原始 HTML 镜像（1211 个文件，含 SOURCE.txt）
│   ├── text/                 # 行式文本层（195 个 .txt，格式说明见其 README.md）
│   ├── manifest.tsv          # 文档表：docid ↔ 标题/路径/URL
│   └── stopwords.txt         # 停用词表（39 词，由语料统计推导；见文件头注释）
├── data2/                    # 第二语料（RFC 1035/5321/7231，通用导入器产物）
│   ├── raw/                  #   下载的原始 .txt + SOURCE.txt（URL 与 sha256）
│   └── data/                 #   manifest.tsv（format=plain）+ text/ 副本
├── build/                    # CMake 二进制目录 + 生成物（gitignore，删掉即可）
├── CMakeLists.txt            # 构建入口：cmake -S . -B build / ctest
└── problem.md / problem_zh.md / plan.md / notes.md
```

## 构建与测试

```bash
cmake -S . -B build                          # 配置：默认 Release（等价 -O2）+ C++17 + -Wall -Wextra
cmake --build build                          # 编译 build/mse
ctest --test-dir build --output-on-failure   # 运行 tests/ 下的单元测试 + 语料冒烟测试
cmake --build build --target clean           # 清理构建产物（或直接删除 build/）
```

常用开关（配置阶段加在 `cmake -S . -B build` 后面即可）：

| 开关 | 作用 |
|------|------|
| `-G Ninja` | 指定生成器（默认用平台生成器；本机装了 Ninja 时推荐） |
| `-DCMAKE_BUILD_TYPE=Debug` | 调试版（默认 Release） |
| `-DMSE_WERROR=ON` | 零警告验收门：把 `-Wall -Wextra` 的警告当错误 |
| `-DBUILD_TESTING=OFF` | 只编译 `mse`，不编译测试 |
| `-DCMAKE_CXX_STANDARD=20` | 试用更新的语言标准（默认 17） |

说明：

- 必须**源外构建**（`build/` 已在 `.gitignore` 中）；换生成器或换编译器时换个目录即可，
  例如 `cmake -S . -B build-gcc -G Ninja -DCMAKE_CXX_COMPILER=g++`。
- 多配置生成器（Visual Studio）产物在 `build/Release/mse.exe`，运行时 `ctest --test-dir build -C Release`。
- 配置后会在 `build/compile_commands.json` 生成编译数据库，clangd / clang-tidy 可直接使用。
- 新增模块时把接口放 `include/xxx.hpp`、实现放 `src/xxx.cpp`，再把 `src/xxx.cpp` 加进
  `CMakeLists.txt` 里 `mse_core` 的源文件列表（新测试同理，登记到 `tests/CMakeLists.txt` 的 `MSE_TESTS`）。
- `tests/fixtures/golden4/` 是讲义四文档示例的黄金样例。注意其中 `a` 一行与讲义表格
  有一处已知差异（以示例句子为准，见 `notes.md` §3.8）。

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
                  [--top 50] [--stop-words data/stopwords.txt] [--stem]
                  [--keep-headings] [--keep-speakers] [--keep-stage]
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
（候选停用词标记：`df_ratio ≥ 0.75 且 tf_share ≥ 0.1%`，规则见 plan.md §4.2）`, entropy, entropy_norm,
entropy_rate_norm`（文档分布熵：原始 H(t) 比特数、H/log₂(df)、以及长度归一化速率熵——停用词判定
的第三特征，定义与"为什么需要长度归一化"见 notes.md §3.11 与 count.hpp 头注释）。

交叉验证（独立 Python 实现，输出应与 C++ 逐字节一致）：

```bash
python3 tools/check_wordcount.py > build/wordcount_py.csv
diff build/wordcount.csv build/wordcount_py.csv && echo identical
```

去停用词对照（正式索引会用这份表，Task 2）：

```text
$ ./build/mse count --stop-words data/stopwords.txt --top 0
manifest : data/manifest.tsv
documents: 195
tokens   : 556866
distinct : 25666
hapax    : 9753 (tf = 1)
stopwords: data/stopwords.txt (39 words)
removed  : 309760 tokens (35.74%)
csv      : build/wordcount.csv
```

（过滤版对拍同样可用：`python3 tools/check_wordcount.py --stop-words data/stopwords.txt`。）

词干化对照（Porter 1980 + 作者 canonical 实现的三处 `--DEPARTURE--` 改进）：

```text
$ ./build/mse count --stem --top 0
manifest : data/manifest.tsv
documents: 195
tokens   : 866626
distinct : 14947          ← 词干化把 25,705 个词形并成 14,947 个词干
hapax    : 4843 (tf = 1)
stemming : on (Porter 1980)
csv      : build/wordcount.csv
```

对拍用 NLTK 参考实现（`MARTIN_EXTENSIONS` 模式），输出应与 C++ 逐字节一致：

```bash
python3 tools/check_wordcount.py --stem > build/wordcount_stem_py.csv
diff build/wordcount_stem.csv build/wordcount_stem_py.csv && echo identical
```

（熵三列在默认 / `--stop-words` / `--stem` 三种配置下均参与逐字节对拍。）

### `mse stopwords` — 熵驱动的停用词推导

```bash
./build/mse stopwords --derive [--manifest data/manifest.tsv]
                       [--out build/stopwords-derived.txt] [--compare data/stopwords.txt]
                       [--min-df-ratio 0.75] [--min-tf-share 0.001] [--min-entropy-rate 0.93]
                       [--stem] [--keep-headings] [--keep-speakers] [--keep-stage]
```

对原始 token 流（不过滤）计数后应用**三特征规则**：词要同时"宽"（df_ratio ≥ 阈值）、
"重"（tf_share ≥ 阈值）、"平"（entropy_rate_norm ≥ 阈值——按文档长度归一化后的分布熵接近 1，
即以固定"浓度"洒在所有文档里）。默认阈值复现保守的 9 词候选带；放宽 df 门到 0.4 后，
纯统计推导出 39 词，与人工审核的静态表重叠 35 词（差异恰为 4 个古体功能词，见 notes.md §3.11）。

```text
$ ./build/mse stopwords --derive --min-df-ratio 0.4 --min-entropy-rate 0.93 --compare data/stopwords.txt
# Derived stop word list -- generated by `mse stopwords --derive`.
# Rule (entropy-based extension of plan.md 4.2, see count.hpp):
#   df_ratio          >= 0.4
#   tf_share          >= 0.001
#   entropy_rate_norm >= 0.93
# Corpus: data/manifest.tsv (195 documents, 866626 tokens)
# Matched 39 terms (tf descending).
list     : the and i to of a my that in is not me it for with be this but have as thou so what by no all do if are thee on from o which then when hath yet (39 words)
file     : build/stopwords-derived.txt
compare  : data/stopwords.txt (39 words in vocabulary)
overlap  : 35
only in list   : or more their than (4)
only in derived: hath o thee thou (4)
```

输出文件自带出处头注释（规则、阈值、语料；不含时间戳，同语料重跑逐字节一致），
可直接作为 `mse build --stop-words` 的输入。

### `mse build` — 构建倒排索引（Task 2）

```bash
./build/mse build [--manifest data/manifest.tsv] [--index build/index.bin]
                  [--dict splay|hash] [--stop-words data/stopwords.txt] [--stem]
                  [--dict-file build/index.bpt]
```

```text
$ ./build/mse build --stop-words data/stopwords.txt --stem
dictionary : splay  (comparisons=6392724, rotations=5850795, rehashes=0)
documents  : 195
tokens     : 556866
terms      : 14937
postings   : 117704
positions  : 556866
index      : build/index.bin (3685655 bytes)
build time : 0.38 s
invariants : OK
```

独立对拍（重新扫描语料 + 独立解析索引文件，全词表逐项比较 df/tf/docID/positions）：

```bash
python3 tools/brute_force.py --stop-words data/stopwords.txt --stem
```

`--dict-file` 额外写出一个 **B+Tree 词典 sidecar**（term → posting 字节区，见
[磁盘 B+Tree 词典](#磁盘-btree-词典bonus-实做)）。

### `mse dump` — 查看某个词的 posting

```bash
./build/mse dump --index build/index.bin --term osric [--limit 20]
# 磁盘词典路径（不加载词表）：
./build/mse dump --index build/index.bin --dict-file build/index.bpt --term osric
./build/mse dump --index build/index.bin --dict-file build/index.bpt --verify-all
```

```text
index      : build/index.bin  (195 docs, 14937 terms, 556866 tokens)
flags      : stopwords stemmed
term osric: df=1, tf=3
  doc 30 "Hamlet"  tf=3  positions: 17629 17933 18173
```

### `mse query` — 查询（Task 3）

```bash
./build/mse query --index build/index.bin --stop-words data/stopwords.txt \
                  --word osric [--pretty]
./build/mse query ... --phrase "king richard" [--top-x 3]
./build/mse query ... --words "king queen" --min-match 2     # 或 --or
# 阈值接口：--max-df <ratio> / --min-df <n> / --min-match <k> / --top-x <n>
```

```text
$ ./build/mse query --index build/index.bin --stop-words data/stopwords.txt \
                    --word osric --pretty
index      : build/index.bin  (195 docs, 14937 terms)
flags      : stopwords stemmed
query      : word "osric"
terms      : osric
results    : 1 documents
  30	Hamlet
      > Osric, who brings back to him that you attend him in
```

查询与建索引共用同一条管线（按索引头 flags 自动启用停用词/词干化）；短语在"过滤后 token 流"上匹配，停用词会被 note 行提示（如 `--phrase "my lord"` 退化为查 `lord`）。加 `--dict-file` 可让单词查询走磁盘词典路径（输出与全载一致，见下节）。

`--json` 输出供脚本消费；阈值矩阵实验（Task 4）：

```bash
python3 tools/threshold_matrix.py    # → build/threshold_matrix.csv + .svg（15 查询 × 4 参数）
```

### 磁盘 B+Tree 词典（Bonus 实做）

"50 万文件 / 4 亿词"的瓶颈在**词典无法常驻内存**。`build --dict-file` 把词表冻结成一个页式
B+Tree 文件（4KB/页，term → posting 字节区，叶链支持范围/前缀扫描）；查询侧
`LazyIndexReader` 只加载文档表，term 经 B+Tree（O(树高) 页读 + LRU 缓存）定位后直接 seek
对应 posting 字节——**词表与 postings 永不整体加载**：

```text
$ ./build/mse dump --index build/index.bin --dict-file build/index.bpt --term banquo
dict-file  : build/index.bpt  (height 2, reads 0)
term banquo: df=1, tf=27
  doc 33 "Macbeth"  tf=27  positions: 195 662 663 1256 1376 3500 ...
dict reads : 2 page reads, 0 cache hits
index read: 8308 bytes (vs 3685655 for a full load)
```

实测（莎士比亚索引，14 937 词）：sidecar 237KB（6.4%）;冷查询 ~16.5KB I/O（全量加载的
0.45%）;`--verify-all` 全词表双路径对拍 0 差异（已进 ctest）。4 亿词外推：树高 4、单查询 4 页
（见 notes.md §3.13）。`mse query --dict-file`（单词模式）与全载路径输出逐字节一致。

### 规划中的命令

`shell`（交互模式）、`selftest`（其不变量校验已由 `build`/`dump` 内建覆盖）
——按 `plan.md` §14 的里程碑逐步补齐，当前会提示 `not-yet-implemented`。

## 工具脚本（重建语料时才需要）

日常开发不需要运行这些：`data/` 与 `data2/` 的内容已随仓库提交且可复现。

```bash
bash tools/fetch_corpus.sh [--force]   # ① 从 GitHub 镜像下载 HTML 到 data/raw/（需要联网）
python3 tools/extract.py               # ② 生成 data/text/ + data/manifest.tsv
```

- `--force` 用于重新抓取：旧目录会先备份为 `data/raw.bak.<时间戳>`，不会直接删除。
- 抓取结果在 `data/raw/SOURCE.txt` 记录仓库地址、commit、抓取日期与文件数。
- `extract.py` 的输入输出路径均可用 `--raw / --text / --manifest` 覆盖。

## 第二语料与通用导入器

引擎不认识莎士比亚——它只读 manifest + 文本层。任何纯文本目录都能变成可索引语料：

```bash
python3 tools/import_corpus.py --input <txt目录> --root <语料根目录> \
       [--kind text] [--category General] [--format plain]
./build/mse stats  --manifest <根>/data/manifest.tsv
./build/mse build  --manifest <根>/data/manifest.tsv --index build/index.bin --stem
```

仓库自带的演示语料 `data2/`（IETF RFC 1035/5321/7231，`data2/raw/SOURCE.txt` 记录来源与
sha256）就是这样导入的：3 文档 / 9 946 行 / 75 250 token，`stats / count / build / query /
brute_force` 全链路零代码改动跑通，且已进 ctest（`rfc_stats`）。

manifest 的可选第 8 列 `format`（`marked`，缺省 | `plain`）按文档控制行标记约定：

| 值 | 行为 |
|----|------|
| `marked` | 莎士比亚文本层：`[ACT I]` 标题 / `(舞台指示)` / `@说话人` 被分类，默认不参与切词 |
| `plain` | 无标记纯文本：所有非 `#` 行都是正文（`(括号整行)`、`[方括号整行]` 保持可检索） |

在 RFC 语料上推导停用词会得到与莎士比亚**不同**的词表（56 词，含 `rfc/page` 领域样板词），
且 RFC 2119 关键词 `must/may/should` 因速率分布倾斜而**不**被判停用词——见 notes.md §3.12。

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
默认配置下切出 866,626 个正文 token；`--stop-words data/stopwords.txt`（39 词）过滤后
556,866 token（removed 309,760，35.74%）。

## 开发约定

- 头文件放 `include/*.hpp`，实现放 `src/*.cpp`，单元测试放 `tests/`；
- 注释率目标 ≥35%（课程硬性要求 30%，不达标代码不计分）；
- 外部代码（如 Porter 词干化）单独文件标注来源，与自写代码区分；
- `build/` 与临时文件不入库；提交课程作业的 zip 中不要包含 `.git`。
