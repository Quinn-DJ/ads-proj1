# proj1 编程方案：Mini Search Engine（迷你搜索引擎）

> 依据：`problem.md`（8-1 Roll Your Own Mini Search Engine，20 分）与 class03 讲义《倒排索引》及其配套材料（Building an Inverted Index、Compression、Inverted Index Construction、The Google File System）。
>
> 本文是动手写代码前的总体设计：每个评分项对应到哪个模块、数据结构怎么选、测试怎么做、报告素材从哪来。

---

## 0. 速览：关键决策

| 决策点 | 结论 | 理由 |
|--------|------|------|
| 实现语言 | C++17（备选 C11） | 课程提交目录为 `*.c / *.h`，需可编译可执行；C++ 的字符串/IO 更省事 |
| 语料 | shakespeare.mit.edu 全集，抓取到本地缓存 | problem 指定；抓取一次后离线可复现 |
| 存储层次 | 原始 HTML → 行式文本（`data/text/`）+ `manifest.tsv` → 索引二进制 | 每层可独立检查、可再生成；C++ 程序不碰 HTML |
| 文档粒度 | 默认 195 个文档：37 部剧 + 154 首十四行诗 + 4 首长诗 | 每篇 = 一个完整的作品单元，贴合"返回文档 ID"；另提供 scene 级 manifest（约 1080 篇）做统计实验（代码不变） |
| 词典结构 | 自实现哈希表（链地址法）+ 构建后生成字典序数组 | 平均 $O(1)$ 查找；有序数组支持二分/前缀与稳定输出，纯哈希做不了有序遍历 |
| posting 内容 | docID + tf + positions | phrase 查询和"打印命中句"必须有位置信息 |
| 索引存储 | 自定义二进制文件 + `dump` 子命令 | 体积小、可复现；`dump` 保证可调试 |
| 查询能力 | 单词、短语、（可选）多词 AND | 对应任务 (3)，且为任务 (4) 的阈值实验留出空间 |
| 阈值参数 | `--max-df`、`--min-df`、`--min-match`、`--top-x` | 覆盖讲义里"查询侧截断"和"文档侧截断"两类思路 |
| 测试 | 讲义 4 文档黄金样例 + 独立 Python 对拍 + 不变量检查 + 阈值矩阵 | 直接对应 Testing 的 2+2+3 分 |
| 注释 | 目标注释率 ≥ 35%（课程硬性要求 30%，留余量） | 注释率不达标代码部分不计分 |

---

## 1. 评分拆解与覆盖表

| 评分项 | 分值 | 由谁覆盖 | 验证方式 | 报告位置 |
|--------|------|----------|----------|----------|
| Word counting 程序 | 1 pt | `count` 子命令（§4） | 与对拍脚本的计数结果一致 | Chapter 2 / 3 |
| Index generation 程序 | 5 pts | `build` 子命令 + 词典/posting 结构（§6） | 黄金样例 + 随机对拍 100% 一致 | Chapter 2 / 3 |
| Query processing 程序 | 3 pts | `query` 子命令（§8） | 单词/短语用例 + 边界用例 | Chapter 2 / 3 |
| 倒排索引正确性测试 | 2 pts | 对拍脚本 + 不变量检查（§10） | 测试表（pass/corrected/pending） | Chapter 3 |
| 查询阈值测试 | 2 pts | 阈值矩阵实验（§9） | 参数扫描 CSV + 分析 | Chapter 3 |
| 分析与评论 | 3 pts | 复杂度、对比、局限、改进（§9/§11/§12） | — | Chapter 3 / 4 |
| Bonus：50 万文件 / 4 亿词 | +2 pts | 规模分析（§11） | 估算 + 迁移方案 | Chapter 4 |
| Documentation | 共 4 pts | Chapter 1/2 + 整体风格（§12） | 对照课程报告规范 | 全文 |

硬性约束：

- 停用词表和词干化函数可以从网上下载，但必须在 References 里注明来源；**其余部分（计数、索引、查询）必须自己写**，不要引入现成搜索库。
- 源码至少 30% 的行有注释，否则代码部分不计分。
- 提交文档不能出现任何组员身份信息（姓名、学号、昵称、组号、文件属性），按课程要求打匿名 zip。

---

## 2. 总体架构

一条流水线，四个子命令共用同一套规范化规则：

```text
data/raw/*.html
   │  tools/fetch_corpus.sh   （一次性抓取，已完成：commit 6b82db8）
   │  tools/extract.py        （HTML → 行式文本 + manifest，已完成）
   ▼
data/text/*.txt + data/manifest.tsv      （195 个文档）
   │  tokenizer：按行解析标记（标题/说话人/舞台指示可开关）+ 切词 + 小写化
   ▼
token 流 ──► count 子命令：词频/文档频率统计 ──► wordcount.csv ──► 停用词判定
   │
   │  stop filter（去掉停用词）
   ▼
stemmer（Porter）──► 规范化词形
   │
   ▼
dictionary（哈希表：term → posting head）
   │           每个 posting 追加 (docID, tf, positions)
   ▼
index 序列化 ──► build/index.bin
   │
   ▼
query 子命令：单词 / 短语 /（多词）→ docID 列表；阈值参数调节结果集
```

CLI 统一入口（二进制在 `build/mse`，下文简写为 `./mse`）：

```text
./mse stats  --manifest data/manifest.tsv      # 语料统计（已实现，用于和 Python 预处理数字对拍）
./mse count  --manifest data/manifest.tsv --csv build/wordcount.csv [--top 200]
./mse build  --manifest data/manifest.tsv --index build/index.bin
./mse dump   --index build/index.bin --term love [--limit 20]
./mse query  --index build/index.bin --word love
./mse query  --index build/index.bin --phrase "my lord" --pretty
./mse query  --index build/index.bin --word king --max-df 0.5 --min-df 3 --min-match 1 --top 10
./mse shell  --index build/index.bin          # 交互模式，便于演示
./mse selftest --index build/index.bin        # 不变量检查 + 黄金样例
```

模块划分（每个模块一个 `.h/.cpp` 对，方便分工与写测试）：

| 模块 | 职责 | 对应任务 |
|------|------|----------|
| `corpus` | 读 manifest、读 `data/text/` 文本、解析行标记（HTML 解析已在 `tools/extract.py` 一次性完成） | 前置 |
| `tokenizer` | 切词、小写化、位置编号 | (1)(2) |
| `stopwords` | 加载停用词表（含来源注释）、过滤 | (1)(2) |
| `stemmer` | Porter 词干化（外部实现，标注来源） | (2) |
| `dict` | 自实现哈希表：term → posting head | (2) |
| `posting` | 动态数组：docID/tf/positions 追加与合并 | (2) |
| `index` | 构建、序列化、反序列化、dump | (2) |
| `query` | 单词/短语/多词查询、阈值过滤 | (3)(4) |
| `main` | CLI 解析、子命令分发 | — |

---

## 3. 语料获取与预处理

### 3.1 抓取

- `tools/fetch_corpus.sh`：`git clone` shakespeare.mit.edu 的官方 GitHub 镜像（`TheMITTech/shakespeare`），把整站 HTML 同步到 `data/raw/`，并把仓库 URL、commit、抓取日期、文件数写入 `data/raw/SOURCE.txt`，保证语料可复现。
- 抓取一次后本地离线可复现；重跑脚本加 `--force` 即可更新镜像（旧目录保留为备份，不直接删除）。
- `tools/extract.py`：把 HTML 转成行式文本层 `data/text/*.txt`（195 个文档；152 632 行、930 911 词），并生成 `data/manifest.tsv`：`docid / kind / category / title / text / source / url`（docID 从 1 开始，按站点索引页顺序：喜剧 17 → 历史 10 → 悲剧 10 → 十四行诗 154 → 长诗 4）。docID 稳定是测试可复现的前提。
- **站点结构已确认**（commit `6b82db8`，共 1211 个 HTML）：37 部剧各一个文件夹，含 `index.html`（作品页）、`full.html`（整篇台词）和分场页 `<work>.<act>.<scene>.html`（共 922 个）；诗歌都在 `Poetry/`：`sonnets.html` 是 154 首十四行诗的索引、每首有独立页 `sonnet.I–CLIV.html`，另有 `VenusAndAdonis.html`、`RapeOfLucrece.html`、`LoversComplaint.html`、`elegy.html` 各一篇。
- 根目录的 `index.html` 是唯一权威的文档清单（首页四栏链接 37 剧 + 5 项诗歌）；大写目录 `Comedy/`、`History/`、`Tragedy/`（共 50 个文件）是旧版残留、与首页无链接，不参与建索引。`Poetry/sonnets`、`Poetry/various` 是纯文本 Moby 版本，也不使用。

### 3.2 文档粒度（可参数化）

- 默认（推荐）：**文档 = 一个完整的作品单元** —— 37 部剧各取 `full.html`；154 首十四行诗各取 `sonnet.<roman>.html`（每首本身就是完整的诗）；`VenusAndAdonis`、`RapeOfLucrece`、`LoversComplaint`、`elegy` 各一篇。共 **195 个文档**，docID 从 1 开始、按 `index.html` 的分类顺序分配。
- 可选：scene 级 manifest —— 分场页 922 个 + 154 首十四行诗 + 4 首长诗 ≈ **1080 个文档**，只换 manifest、代码不变，用于任务 (1)(4) 的统计实验（195 个文档上 df 比值仍然偏粗）。
- 敏感性选项（报告素材）：把 154 首十四行诗合并为一篇 → 42 个文档，对比两种粒度对词频/停用词统计和阈值实验的影响。

### 3.3 HTML → 文本层规则（已实现，写进报告 Chapter 2）

`tools/extract.py` 把每个文档转成"一行一个单元"的文本（`data/text/<docid>_<slug>.txt`），头部 `#` 注释记录 docID/标题/来源/URL：

| 文本行 | 来源标记 | 索引默认 |
|--------|----------|----------|
| `[ACT I]` / `[SCENE I. ...]` | `<h1>`–`<h3>` 标题 | 过滤（`--keep-headings` 保留） |
| `(Exit Ghost)` | 斜体 `<i>` 舞台指示 | 过滤（`--keep-stage` 保留） |
| `@BERNARDO` | 粗体 `<b>` 说话人标签 | 过滤（`--keep-speakers` 保留） |
| `Who's there?` | 台词行锚点 `<A NAME=a.s.l>`、诗歌 `<BR>` 行、散文段落 | 正文，参与检索 |

解析要点（全部经过全量验证，0 警告）：解码实体、合并空白；丢弃 `<head>`、顶部导航表格（`td.play`/`td.nav`）和 `<a href>` 链接；诗歌行的 `<BR>` 逐行拆分；散文段落折叠为一行；诗句中夹带的方括号舞台提示（`[Aside]`、`[Sings]`、`[To ...]`，共 610 处）拆成独立的 `(...)` 舞台行，避免 `aside`/人名等混进正文词表；`2henryiv` 源码里的转换残渣 `<h3>None</h3>` 丢弃（1 处）；`elegy.html` 里表格版诗文的行号单元格（纯数字）丢弃；该文件 `</TITLE` 标签残缺，靠 `<body>` 恢复（脚本里有注释说明）。

衍生决策：`<A NAME=act.scene.line>` 的行号锚点（如 `1.1.1`）本轮没有写进文本层——`--pretty` 句子定位靠重新切词即可；如果后面想要精确到"行"的引用，扩展 `extract.py` 重跑即可（文本层是派生物，随时可再生成）。

### 3.4 切词规则（tokenizer spec）

- token = 连续 ASCII 字母，允许内部撇号（`don't`、`'tis` 掐头留身），连字符视为分隔（`to-day` → `to`, `day`）。
- 输入是文本层：`#` 行跳过；`[`/`(`/`@` 行按上表开关处理；其余行是正文。全部小写化；纯数字忽略。
- 位置编号：每个文档内从 0 开始递增，记录在 posting 里（§6）。
- 明确写出并测试的边界情形：撇号变体（`'` / `’`）、空行、`--` 破折号、全部大写单词（`KING`）。

---

## 4. Task (1)：词频统计与"noisy word"判定

### 4.1 计数程序（1 pt）

`mse count`（已实现）：输出 `build/wordcount.csv`，列 `rank, term, tf, df, df_ratio, tf_share, stop_candidate`（tf=总出现次数，df=出现的文档数，stop_candidate 按 §4.2 的规则自动标注）。终端打印摘要（tokens / distinct / hapax / 候选数与候选列表）和 top n 表（`--top`，默认 50，`--top 0` 静默）。首跑结果：866 626 个正文 token、25 705 个不同词、9 753 个 hapax（tf=1）、9 个候选停用词（the and i to of my that in but）；与独立 Python 实现逐字节一致（`tools/check_wordcount.py`）。去停用词/词干化后的对照版本在 stopword / stemmer 模块完成后补跑。

### 4.2 "线画在哪里"——判定方法

分两步，最终结论写进报告（这是题目里明确问的问题）：

1. **数据驱动候选**：把词按 (df_ratio, tf_share) 排序。候选停用词 = `df_ratio ≥ 0.75 且 tf_share ≥ 0.1%` 的词；同时输出第 50–150 名的"边界区"词表（这一区的取舍才是需要解释的）。
2. **对照 + 人工审阅**：下载一份标准英语停用词表（注明来源）做对照；对边界词逐个人工判断：
   - 保留例：`thou, thee, hath, shall` —— 高频但有文体/语义价值，删除会让莎士比亚检索失真；
   - 删除例：`a, the, of, in` 及本语料特有的排版噪声词。
3. **敏感性检查**：用两种停用词表（保守版/激进版）各建一次索引，对比索引大小和若干查询结果，用数据支撑"线"的位置。

### 4.3 产出

- `data/stopwords.txt`：最终列表，文件头注释写清来源（参考表 URL + 访问日期）与自定义增删理由。
- `build/wordcount.csv` + top 表的截图/摘录、rank–frequency（Zipf）图（可选，`tools/plot_zipf.py`）→ 报告 Chapter 3/4 素材。

---

## 5. 词干化（Task (2) 前置）

- 采用 Porter Stemmer：下载公开的参考实现（Martin Porter 的算法说明与代码），文件头写明来源，References 列出；或按论文自实现（工作量更大，二选一，默认前者）。
- 规范化顺序：**小写化 → 停用词过滤 → 词干化**（先滤后干更快，且避免 `is/are/being` 类词先干后不可滤）。
- 单元测试向量：≥ 20 组（覆盖论文里的 step 1a/1b/1c/2–5 与不规则例子），存 `tests/fixtures/stem_vectors.tsv`。
- 词典和查询必须用同一函数，否则查不到词——测试里放一条专门的后置检查。

---

## 6. 倒排索引构建（核心，5 pts）

### 6.1 数据结构选型（报告 Chapter 2 规格素材）

| 结构 | 查找 | 有序/前缀 | 空间 | 结论 |
|------|------|-----------|------|------|
| 哈希表（链地址） | 平均 $O(1)$ | ✗ | 中 | **主词典** |
| Trie | $O(\text{len})$ | ✓ | 大 | 不做（除非冲加分） |
| 有序数组 + 二分 | $O(\log n)$ | ✓ | 小 | 构建结束后生成副本，用于 dump/前缀与稳定输出 |

本语料规模估算：约 90 万个 token、约 2–3 万个不同词形、去掉停用词后约 40–50 万条 posting entry，峰值内存几 MB 级别，全部驻留内存没有压力（与讲义里 5GB 语料的例子形成对照，报告里写清"为什么两者结论不同"）。

### 6.2 posting 结构

```text
term → [ { docID:uint32, tf:uint32, positions:uint32[tf] } ... ]  // docID 严格递增
```

- docID 从 1 开始、文档内递增追加；
- positions 是"过滤后 token 流"内的偏移，用于短语匹配（§8 说明其语义和局限）；
- 同一文档内重复出现的词只更新最后一条 posting（tf+1、追加 position），不新建节点。

### 6.3 构建算法（伪代码，报告直接可用）

```text
for each doc D in manifest:
    tokens ← tokenize(strip_html(read(D)))
    for i, t in tokens:
        if is_stopword(t): continue
        s ← stem(t)
        if s not in dict: dict.insert(s)      // 哈希查找
        dict[s].append(D.id, tf, i)
serialize(index) → index.bin
```

- 构建期同时统计：token 总数、不同词形数、停用词占比、posting entry 数、索引文件大小、构建耗时 → 报告 Chapter 3/4 表格。
- 可选压缩（进阶）：docID 差分 + varint/Elias 编码，实测索引体积变化，对应讲义《Compression》一节（做了就是 Chapter 4 的加分分析素材）。

---

## 7. 索引持久化格式（v1 规格，M2 前可微调）

```text
[header]   magic "MSE1", version, doc_count, term_count, token_count
[doc 表]   每条：docID:u32, title, url, token_count:u32
[term 表]  按字典序：term, df:u32, tf_total:u32, posting_offset:u64, posting_len:u32
[postings] 每个 term：df 条 { docID:u32, tf:u32, positions:u32[tf] }
```

- 字符串用长度前缀；全部小端定长，读写用同一份 spec 文档（放 README）。
- 必须提供 `mse dump`，把任意 term 的 posting 还原成可读表格（调试 + 报告截图用它）。
- 验收检查：反序列化后再 dump 一遍，与构建期内存内容逐项一致。

---

## 8. 查询程序（Task (3)，3 pts）

### 8.1 查询语义

- **单词**：输入 → 同一规范化管线（小写、停用词检查、词干化）→ 哈希查词典 → 输出升序 docID 列表（`docID: 标题`）。
- **短语**：把短语切成过滤后词序列 `w1..wk`，取 df 最小的词做候选，逐个做 posting 交集，再在候选文档内做位置验证：`pos(w_{i+1}) = pos(w_i) + 1`。
- **多词（可选）**：`--and` / `--or`，为阈值实验提供基础。
- **停用词输入**：查询词若在停用词表内，明确提示"该词未建索引"，并返回空集（文档里记录此行为；短语内部的停用词被忽略，短语在"过滤后 token 流"上匹配——这正是讲义提醒的 `to be or not to be` 会失真的原因，作为已知局限写进报告并配测试用例）。

### 8.2 输出

- 默认：`docID: 标题` 每行一条 + 总数；`--json` 供测试脚本消费。
- `--pretty`：利用 positions 定位命中的过滤后 token，回到原文还原所在句子并加高亮（讲义里"打印并高亮句子"的增强，做展示时很直观）。
- 复杂度：单词 $O(1) + O(df)$；短语 $O(\sum df + \text{候选数} \times k)$，写进 Chapter 4。

---

## 9. 查询阈值实验（Task (4)）

### 9.1 四个阈值参数

| 参数 | 含义 | 取值示例 |
|------|------|----------|
| `--max-df r` | 忽略 df_ratio > r 的查询词（太常见、区分度低） | 0.2 / 0.5 / 1.0(off) |
| `--min-df n` | 忽略 df < n 的查询词（太罕见、多为噪声） | 1 / 2 / 5 |
| `--min-match k` | 多词查询至少命中 k 个词（k=n 即 AND，k=1 即 OR） | 1 / 2 / 全部 |
| `--top-x n` | 结果按分数截断前 n 条（分数=命中词数或 Σtf） | 5 / 10 / 全部 |

前两个是"查询侧截断"（对应讲义里按词频排序、只取部分查询词），后两个是"文档侧截断"（对应 top docs）。参数全部走 CLI，默认关，方便对照组。

### 9.2 实验设计

- 查询集（约 12 条，分五类，先用对拍脚本确认期望再固化）：
  - 高频词：`love`、`king`、`good`；
  - 中频词：`sword`、`moon`、`letter`；
  - 低频/专名：`wherefore`、`osric`（待验证）；
  - 短语：`my lord`、`thou art`、`to be or not to be`；
  - 停用词：`the`（应提示未建索引）。
- 对每条查询扫描参数组合（可全量小网格），记录：返回文档数、与基线（全部阈值关闭）的 Jaccard 相似度、命中词数分布、查询耗时。
- 产出 `build/threshold_matrix.csv` + 曲线图（返回数 vs 阈值）→ 报告里回答"阈值如何影响结果"。
- 可选加分：对 2–3 条查询人工标注 top-10 相关性，用讲义里的 precision/recall 算出阈值前后的变化（直接复用 class03 §5 的方法论）。

---

## 10. 测试与验证（Testing 2+2+3 pts）

三层测试，全部可一键跑：`tests/run_all.sh`。

1. **单元测试**
   - tokenizer：撇号、连字符、大小写、空输入；
   - stemmer：20+ 组向量；
   - dict：插入/查找/冲突（构造哈希冲突用例）；
   - posting：正序追加、tf/positions 一致性。
2. **黄金样例（讲义 4 文档例子）**
   - 把 `Gold silver truck / Shipment of gold damaged in a fire / ...` 四篇存为 fixture；
   - 期望 posting 表直接来自讲义表格：全词版本（`--no-stopwords`）+ 去停用词版本（`a/of/in` 被剔除）各一套；
   - 构建索引后 dump，与期望逐项比对。这是报告 Chapter 3 最有说服力的一张表。
3. **全量对拍**
   - `tools/brute_force.py`：独立实现同一套规范化规则，直接扫描原始文本得到 docID 集合（独立实现才有对拍价值）；
   - 随机抽 200 个词 + 30 条短语，比较索引查询结果与扫描结果，要求 **100% 一致**；
   - 不变量检查（`mse selftest`）：posting 内 docID 严格递增且合法；tf = position 数 = 该文档实际出现次数；词典中无停用词；同一条查询的 stem 链路一致。
   - 词频阶段已提前落地同款对拍：`tools/check_wordcount.py` 与 `mse count` 的输出逐字节比较（默认与 `--keep-speakers` 两个变体都已 100% 一致）。
4. **边界用例**：空查询、纯停用词、不存在的词、单字符、带标点、全大写、`don't`、`to-day`。
5. **性能测试**：全量构建时间/峰值内存/索引体积；1000 次随机查询平均延迟（macOS 与 Linux 各跑一次更稳）。

测试记录统一写 `tests/tests.md`，每行格式按课程要求：目的 / 预期结果 / 实际行为 / 可能原因 / 状态（pass / corrected / pending），直接搬进报告 Chapter 3。

---

## 11. Bonus：50 万文件、4 亿不同词（+2 pts）

结论先行（报告里按这个结构展开）：

1. **现方案会怎样**：词典 400M 个 term、posting 数亿条，内存版哈希表直接不可行。粗算：term 字符串+元数据按每个 ~30B 就是 12GB 级，还没算 postings。
2. **为什么以及怎么改**（引用讲义数字）：
   - 外部排序构建：4 亿条 `<termID, docID, tf>` 三元组、40MB 内存 → 100 个 run、7 趟归并、约 20 小时，临时空间约文本的 2 倍（直接引用《Building an Inverted Index》的估算，注明来源）；
   - 块式构建（BlockIndex）：内存写满即落盘，最后与主索引合并；
   - 分布式：按词切（查询只碰少数机器，但热门词热点）或按文档切（负载均衡，但每次查询全量广播），底层参考 GFS；
   - 压缩：docID 差分 + Elias/变长编码，top-x 截断长 posting；
   - 查询侧：rare-term-first，避开超长 posting。
3. **我们程序的临界点**：给出"可用内存 M 下能撑多大语料"的估算公式与表格（M=64MB / 1GB / 16GB 分别对应多大规模），说明边界在哪里。

---

## 12. 文档产出与报告映射

| 报告部分 | 分值 | 直接来源 |
|----------|------|----------|
| Chapter 1 Introduction | 1 pt | §3 语料与预处理 + problem 背景 |
| Chapter 2 Data Structure / Algorithm Specification | 2 pts | §6 数据结构规格与伪代码、§5 词干化、§8 查询算法 |
| Chapter 3 Testing Results | — | §10 测试表 + §9 阈值矩阵 + §4 词频表 |
| Chapter 4 Analysis and Comments | — | §9 阈值分析、§10 性能数据、§11 Bonus、复杂度与改进（含压缩实验） |
| 整体风格 | 1 pt | 统一术语表（中英对照）、图表编号、References 格式 |

预备图表：索引构建设计图（可复用讲义 `index-construction.svg` 重绘）、索引结构示意、Zipf 词频图、阈值-结果数曲线、黄金样例对照表。

References 预留条目：Porter stemmer（论文/代码地址）、停用词表来源、MIT Shakespeare 语料 URL + 访问日期、Manning《Introduction to Information Retrieval》、课程讲义。

---

## 13. 工程约定与仓库结构

```text
proj1/
├── problem.md / problem_zh.md / plan.md / README.md / notes.md
│                                       # README=用法；notes=开发笔记（坑与发现）
├── Makefile                            # make / make test / make clean
├── data/
│   ├── raw/                            # 抓取到的 html（不进 zip 提交）
│   ├── text/                           # extract.py 生成的行式文本层（195 个 .txt）
│   ├── manifest.tsv
│   └── stopwords.txt                   # 头部注释写来源与理由
├── build/                              # index.bin、csv、测试输出（gitignore）
├── include/{corpus,tokenizer,stopwords,stemmer,dict,posting,index,query}.hpp
├── src/{main,corpus,tokenizer,stopwords,stemmer,dict,posting,index,query}.cpp
├── tools/{fetch_corpus.sh,extract.py,brute_force.py,comment_ratio.py,plot_*.py}
└── tests/
    ├── fixtures/golden4/               # 讲义四文档 + 期望 posting
    ├── fixtures/stem_vectors.tsv
    ├── tests.md                        # 用例表（报告 Chapter 3 底稿）
    └── run_all.sh
```

- 编译：`-std=c++17 -O2 -Wall -Wextra`，要求零警告；`make test` 一键跑全部测试。
- 注释规范：每个函数头 4 行（目的/参数/返回/复杂度）+ 关键逻辑行内注释；`tools/comment_ratio.py` 统计非空行注释率，超过 35% 才允许提交。
- 外部代码（Porter stemmer）单独文件 + 文件头注明来源，与自写代码区分清楚。
- 提交前匿名检查：删除 `.git`、`.DS_Store`、`.vscode`，报告转 PDF，文件名按课程格式（如 `wc_2026G03_P1.pdf`），文档内不出现任何身份信息。

---

## 14. 里程碑与验收门

按 2 周排（组内 4 人可按模块并行），按实际截止日缩放：

| 阶段 | 内容 | 验收门（Gate） |
|------|------|----------------|
| M0（D1–D2） | 抓取 ✅ + 文本层/manifest ✅ + tokenizer + `count` 程序 | `wordcount.csv` 产出，top 200 合理 |
| M1（D3–D5） | 停用词 v1 + stemmer 接入 + 索引构建（内存版）+ dump | 黄金样例全词版通过 |
| M2（D6–D8） | 索引序列化 + 单词/短语查询 | 随机对拍 200 词 100% 一致 |
| M3（D9–D11） | 阈值模块 + 阈值矩阵 + 性能数据 | `threshold_matrix.csv` + 图 |
| M4（D12–D14） | 报告四章 + 注释率检查 + 匿名打包 | 评审清单逐项过一遍 |

进度（2026-10-10）：语料抓取（1211 个 HTML，commit `6b82db8`）、文本层 + manifest（195 个文档、152 632 行、930 911 词、0 警告）、C++ 存储层（`src/corpus` + `mse stats`）、tokenizer（`src/tokenizer` + 20 条单元测试；逐文档对拍 100% 一致）与 word count（`src/count` + `mse count`：25 705 个不同词、9 753 个 hapax、9 个候选停用词；CSV 与独立 Python 实现逐字节一致）已完成；下一步是停用词表定案（`data/stopwords.txt`）与 Porter stemmer。

---

## 15. 风险与对策

| 风险 | 对策 |
|------|------|
| 抓取失败/站点结构变化 | 重试 + 本地缓存 + sha256；M0 阶段先人工确认页面结构 |
| HTML 解析边界（实体、斜体、说话人） | 先 dump 几页样本文本人工抽查，再做差分测试 |
| 停用词删过头（thou/hath 等） | 保守版/激进版双表 + 查询敏感性对比（§4.2） |
| 短语含停用词语义失真 | 明确定义为"过滤后 token 流上的短语匹配"，文档化 + 专门测试用例 |
| 词典/查询规范化不一致导致查空 | 共用同一函数 + `selftest` 专查 |
| 注释率不达标 | 函数头强制模板 + 脚本把关，目标 35% |
| 报告与代码不同步 | 每个模块完成即写对应 Chapter 段落，避免最后补 |

---

## 16. 待确认的决策（默认值已选好，可直接开工）

1. 报告语言：默认中文（讲义与课程规范均为中文），如课程要求英文再切换；
2. 组内分工：默认按 §14 的四个模块并行（抓取/计数、索引、查询/阈值、测试/报告）；
3. scene 级文档粒度：默认只做作品级提交，scene 级留作统计实验；
4. 目标平台：macOS + Linux（纯标准库，无第三方依赖）。

---

## 17. Done 的定义（提交前逐项打勾）

- [ ] `make && make test` 一条命令通过，零警告
- [ ] 黄金样例两套期望全部一致；随机对拍 100%；`selftest` 全部不变量通过
- [ ] 单词/短语/边界用例全部符合测试表；阈值矩阵 CSV 与图完成
- [ ] 注释率 ≥ 35%，外部代码来源标注清楚
- [ ] 报告 Chapter 1–4 完成，References 齐全，术语与图表风格统一
- [ ] 匿名检查：无个人信息、无 `.git`/`.DS_Store`，文件名符合课程格式，报告为 PDF
