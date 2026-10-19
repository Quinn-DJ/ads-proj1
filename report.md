# 迷你搜索引擎项目报告

## 基于莎士比亚全集的带词干化倒排索引

> **草稿 v0.1**（2026-10-19）
>
> 说明：本文件为报告初稿。第 1–3 章与第 4 章主体已完成；Bonus 章节仍在开发中，相关小节标注为“分析草案”，待实验完成后补入最终数字。提交前需按课程要求转为 PDF、补齐图表编号并做匿名检查。

---

## 摘要

本项目在《莎士比亚全集》（The Complete Works of William Shakespeare）上实现了一个迷你搜索引擎。系统以 C++17 编写，提供词频统计、停用词推导、Porter 词干化、倒排索引构建、索引持久化以及单词/短语/布尔查询能力。语料共 195 个文档、152,632 行、930,911 个空白分隔词；默认配置下切分出 866,626 个正文 token，词型 25,705 个。通过自有统计推导出 39 个停用词后，索引保留 556,866 个 token（去除 35.74%），词干化后词表降至 14,937 个词干，倒排索引含 117,704 条 posting、556,866 个位置，二进制文件约 3.5 MB。

正确性方面，系统采用三层验证：Catch2 单元测试（28 个测试用例、246 条断言）、讲义四文档黄金样例、以及独立实现的 Python 对拍（词频 CSV 逐字节一致、全词表 brute-force 对拍 14,937 个词干全项一致）。查询侧实现四个可调阈值，并用 15 条查询 × 4 个参数共 201 次测量量化了阈值对结果集的影响。

**关键词**：倒排索引；停用词；Porter 词干化；伸展树；查询阈值；莎士比亚语料

---

## 第 1 章 引言

### 1.1 项目背景与目标

本课程项目要求实现一个能对莎士比亚全集进行查询的迷你搜索引擎。题目包含四项任务：

1. 对语料做词频统计，并识别停用词（noisy words），说明“有趣的词”与“噪声词”之间的界线画在哪里；
2. 在语料上建立带词干化的倒排索引，且不包含第 (1) 步识别出的停用词；
3. 在倒排索引之上实现查询程序，接受单词或短语，返回包含该词的文档 ID；
4. 运行测试，展示查询阈值如何影响结果。

评分标准将编程分为词频统计（1 分）、索引生成（5 分）、查询处理（3 分），测试分为索引正确性（2 分）、查询阈值（2 分）与分析评论（3 分），另有 Bonus：若拥有 500,000 个文件与 400,000,000 个不同单词，程序是否仍能工作（+2 分）。文档部分要求第 1 章（1 分）、第 2 章（2 分）以及完整报告的整体风格（1 分）。

本报告对应上述要求组织为四章：第 1 章介绍语料与系统全貌；第 2 章给出数据结构与算法规格；第 3 章给出测试结果与实验数据；第 4 章给出分析、局限与 Bonus 讨论。

### 1.2 语料与规模

语料来自 MIT 的 Shakespeare 站点（<http://shakespeare.mit.edu/>）。为便于复现，项目使用该站点的官方 GitHub 镜像 `TheMITTech/shakespeare`，固定 commit `6b82db852c7322dd33e95db347f0ddfc812c6409`，抓取日期 2026-10-10，共 1,211 个 HTML 文件。

文档集合严格以站点首页 `index.html` 的四栏分类为准，共 **195 个文档**：

- 37 部戏剧（每部取完整的 `full.html`）；
- 154 首十四行诗（每首一个独立页面）；
- 4 首长诗（*Venus and Adonis*、*The Rape of Lucrece*、*A Lover's Complaint*、*Funeral Elegy*）。

按首页顺序分配稳定的 docID（1–195）：喜剧 17 部、历史剧 10 部、悲剧 10 部、诗歌 158 篇。仓库中还保留了站点的大写旧目录（`Comedy/`、`History/`、`Tragedy/`）和 Moby 纯文本版本，但它们与首页没有链接关系，不参与建索引——这是“以站点自身索引为唯一权威”的清洗原则。

语料的规模统计如下（`mse stats`）：

| 指标 | 数值 |
|---|---:|
| 文档数 | 195 |
| 总行数 | 152,632 |
| 正文行 | 114,315 |
| 标题行 | 1,099 |
| 舞台指示行 | 6,251 |
| 说话人行 | 30,967 |
| 空白分隔词数 | 930,911 |
| 文本字节数 | 4,919,371 |

默认配置（只统计正文行）切分出 **866,626 个 token**，其中不同词型 **25,705** 个，hapax（只出现一次的词）**9,753** 个。需要说明的是，930,911 是文本层的空白分隔字段数，而 866,626 是经过分词规则（标点、数字、标记行过滤）后的 token 数，两者口径不同，不能直接比较。

### 1.3 系统总体设计

系统采用三层可再生成的数据流：

```text
data/raw/*.html                     原始 HTML（1,211 个文件，固定 commit）
        │  tools/extract.py
        ▼
data/text/*.txt + data/manifest.tsv 行式文本层 + 文档表（195 个文档）
        │  mse build
        ▼
build/index.bin                     倒排索引二进制（可加载、可 dump）
```

C++ 程序只读取文本层，不接触 HTML；HTML 的解析、清洗与文档切分在 Python 预处理阶段一次性完成。这样每层都可以独立检查、独立重跑，任何统计数字都能从上一层重新生成。

CLI 程序 `mse` 提供六个子命令：

| 子命令 | 作用 | 对应任务 |
|---|---|---|
| `stats` | 语料统计 | 前置/对拍 |
| `tokenize` | 查看 token 流 | 调试/实验 |
| `count` | 词频 tf/df 统计，输出 CSV | Task 1 |
| `build` | 构建并序列化倒排索引 | Task 2 |
| `dump` | 打印某个词的 posting | 调试/报告 |
| `query` | 单词、短语、布尔查询与阈值 | Task 3/4 |

模块划分与代码组织如下：

| 模块 | 职责 |
|---|---|
| `corpus` | 读 manifest、读文本层、解析行标记 |
| `tokenizer` | 切词、小写化、位置编号 |
| `stopwords` | 加载停用词表，O(1) 查询 |
| `stemmer` | Porter 词干化与收缩词预处理 |
| `posting` | posting 与 positions 的动态数组 |
| `dict` | 伸展树词典（主）+ 哈希词典（基线） |
| `index` | 构建、序列化、加载、不变量校验、dump |
| `query` | 三种查询模式与四个阈值 |
| `count` | tf/df 统计与 CSV 输出 |
| `main` | CLI 解析与子命令分发 |

### 1.4 完成情况

截至 2026-10-19：

| 评分项 | 状态 | 说明 |
|---|---|---|
| 词频统计程序（1 分） | ✅ | `mse count`，四种变体均与独立 Python 实现逐字节一致 |
| 索引生成程序（5 分） | ✅ | `mse build`，splay/hash 输出逐字节一致，brute-force 对拍通过 |
| 查询程序（3 分） | ✅ | 单词/短语/布尔、四阈值、`--pretty`、`--json` |
| 索引正确性测试（2 分） | ✅ | 单元测试 + 黄金样例 + 全量对拍 |
| 查询阈值测试（2 分） | ✅ | 201 条测量的阈值矩阵 |
| 分析评论（3 分） | ✅ | 见第 3、4 章 |
| Bonus（+2 分） | 🚧 开发中 | 第 4.7 节为分析草案 |
| 文档（4 分） | 🚧 初稿 | 本文件 |

### 1.5 报告组织

第 2 章给出各模块的规格、数据结构选择与算法伪代码；第 3 章给出测试策略、测试结果与实验数据；第 4 章讨论停用词界线、Zipf 分布、词典对比、压缩机会、阈值语义、局限性与 Bonus 规模分析；附录给出构建命令、数据格式与评分对照表。

---

## 第 2 章 数据结构与算法规格

### 2.1 分层架构与模块边界

系统遵循“一个阶段只做一件事”的原则。分词器（`tokenizer`）只负责把文本变成规范化 token，既不移除停用词也不做词干化；停用词过滤（`stopwords`）与词干化（`stemmer`）是独立阶段；计数（`count`）、建索引（`index`）和查询（`query`）共用同一条规范化管线。这样任何一个数字出错，都能定位到唯一阶段，而不是在一条混合管线里猜测。

建索引与查询必须使用完全一致的规范化规则。索引文件头部记录了两个标志位：

```cpp
constexpr std::uint32_t kIndexFlagStopWords = 1u << 0;
constexpr std::uint32_t kIndexFlagStemmed   = 1u << 1;
```

查询程序读取索引头，按标志位决定是否过滤停用词、是否调用 Porter 词干化。这避免了“建索引时词干化、查询时未词干化”导致查空的一类常见错误。

### 2.2 语料获取与预处理

#### 2.2.1 原始语料与文档集合

`tools/fetch_corpus.sh` 通过 `git clone --depth 1` 获取镜像，并用 `git archive` 导出到 `data/raw/`，同时把仓库地址、commit、抓取日期和 HTML 文件数写入 `data/raw/SOURCE.txt`。抓取是一次性的，之后所有实验都可离线复现。

文档集合由 `tools/extract.py` 从首页解析得到：戏剧取 `full.html`，十四行诗按 `sonnet.<roman>.html` 的罗马数字顺序展开（并校验恰好 154 首），长诗取各自的页面。docID 按首页出现顺序分配，从 1 开始连续编号，`Corpus::load` 会校验编号必须是 `1..N` 的稠密序列。

#### 2.2.2 行式文本层

每个文档被转换为一个 `data/text/<docid>_<slug>.txt`，一行一个单元，行首标记区分类型：

| 标记 | 含义 | 默认是否参与检索 |
|---|---|---|
| `[ACT I]` / `[SCENE I. ...]` | 标题行（H1–H3） | 否（`--keep-headings` 可开） |
| `(Exit Ghost)` | 舞台指示（`<i>` 及从诗句拆出的方括号提示） | 否（`--keep-stage` 可开） |
| `@BERNARDO` | 说话人标签（`<b>`） | 否（`--keep-speakers` 可开） |
| 其他 | 台词、诗行、散文正文 | 是 |
| `#` 开头 | 文件头注释（docID、标题、来源、URL） | 跳过 |

行标记的判定规则是“行首与行尾同时匹配”：只有当一行以 `[` 开头且以 `]` 结尾时才视为标题行，以 `(` 开头且以 `)` 结尾时才视为舞台行。这条规则在开发中被证明非常关键：语料里存在大量“以 `[` 开头、但其实是台词”的行，例如 `[To HELENA] The best wishes that can be forged in`；如果只按行首判断，会损失 4,340 个 token。对拍脚本最初就踩了这个坑，修正后 C++ 与 Python 才重新一致。

#### 2.2.3 清洗中的边界情况

预处理阶段处理了若干源数据问题，均有测试记录：

- `elegy.html` 的 `</TITLE` 缺少 `>`，导致 HTMLParser 永远不发出 `</head>`；提取器把 `<body>` 的开始作为“头部结束”的兜底信号。
- `elegy` 的诗文藏在表格中，每 5 行有一个纯数字行号单元格；提取器只丢弃导航表格（`td.play` / `td.nav`）和纯数字单元格，保留诗文表格。
- Moby 版把内嵌舞台提示写成方括号，共 610 处。提取器把行首的 `[...]` 片段重定位为独立舞台行 `(...)`，避免 `aside`、`sings`、收信人名混入正文词表。修复后正文 token 从 867,475 降到 866,626，净减 849，与 B 类片段的统计完全吻合。
- `2henryiv` 中有 `<h3>None</h3>` 转换残渣；提取器丢弃它，不臆造 `ACT I`。
- 全语料非 ASCII 字符数为 0（撇号统一为 `'`，破折号为 `--`），因此 tokenizer 可按字节处理，不需要 Unicode 归一化。这一前提在报告中明确记录；换语料时必须重新检查。

### 2.3 分词与规范化

`tokenizer` 的规则如下（`plan.md §3.4`）：

- token 是连续的 ASCII 字母；数字、连字符、标点都是分隔符：`to-day` → `to`、`day`；
- 撇号只保留在 token 内部（两个字母之间）：`don't` → `don't`，`'Tis` → `tis`，`friends'` → `friends`；
- 所有 token 小写化：`KING.` → `king`；
- 分词器不删除停用词、不做词干化；
- 每个文档内 position 从 0 开始递增，记录在 posting 中。

边界用例包括撇号（内部、行首、行尾、连续）、连字符、数字（`1660s` → `s` 是已知边缘行为）、纯标点、空输入、全大写。

### 2.4 任务 (1)：词频统计与停用词判定

#### 2.4.1 tf 与 df 的定义

对每个规范化后的词项 `t`：

- **tf(t)**：`t` 在全部文档中的总出现次数；
- **df(t)**：包含 `t` 至少一次的文档数；
- **df_ratio(t) = df(t) / 文档总数**；
- **tf_share(t) = tf(t) / 总 token 数**。

`countWords()` 按文档遍历。为了在 O(1) 时间内更新 df，每个词项记录 `lastDoc`：只有当前文档 ID 与 `lastDoc` 不同时才把 df 加一。词干化阶段使用缓存，相同的词形只调用一次 `stem()`。

`mse count` 输出 CSV 列：`rank, term, tf, df, df_ratio, tf_share, stop_candidate`。其中 `stop_candidate` 是**粗筛标记**，规则为 `df_ratio ≥ 0.75 且 tf_share ≥ 0.1%`；它只是给人工分析提供候选，不是最终停用词表。

#### 2.4.2 候选带的确定

如果直接用 0.75 的粗筛线，只能得到 9 个词（`the and i to of my that in but`）。这条线过于保守：154 首短诗会显著稀释 df——很多功能词只出现在戏剧对白中，而短诗里没有，因此 df_ratio 被拉低。为了不漏掉结构性功能词，最终分析把候选带放宽到：

```text
tf_share ≥ 0.1%（≥ 867 次）
且 df_ratio ≥ 0.40（≥ 78/195 篇）
```

得到 **48 个候选词**。这条规则的两个条件分工明确：`df_ratio` 管“广度”（词是否遍布整个语料），`tf_share` 管“强度”（词是否足够高频）。在本语料上，df 条件是决定性条件：只按 df_ratio ≥ 0.75 筛出 9 个词；只按 tf_share ≥ 0.1% 筛出 125 个词；两者的交集仍是这 9 个词。tf_share 条件作为通用护栏保留，防止在超大语料上仅凭 df 选入过于罕见的词。

阈值敏感性如下（`tf_share ≥ 0.1%` 固定，逐步放宽 df 线）：

| df_ratio 阈值 | 候选数 | 相对上一档新增的词 |
|---:|---:|---|
| ≥ 0.90 | 4 | the and to in |
| ≥ 0.75 | 9 | + i of my that but |
| ≥ 0.70 | 14 | + a is not for with |
| ≥ 0.65 | 16 | + so love |
| ≥ 0.60 | 21 | + me be thou thy thee |
| ≥ 0.50 | 31 | + it this as by all do on from which when |
| ≥ 0.40 | 48 | + his have what no if are o or more then their hath than yet mine time doth |

这张表就是“线画在哪里”的直接依据：阈值每放宽一档，新增词从明显的高频功能词逐渐过渡到内容词和古语词，0.40 之后新增项已经开始包含 `love`、`time` 这类有检索价值的词，因此把界线定在 0.40。

#### 2.4.3 从候选到最终停用词表

对 48 个候选词做人工边界判定，只保留**结构性功能词**：冠词、介词、连词、代词、助动词、wh-词。最终得到 **39 个词**，写入 `data/stopwords.txt`。判定中刻意排除两类词：

- **内容词**：`love`（tf 2,224 / df 127，df_ratio 65.1%）、`time`（df_ratio 40.0%）——它们在文学检索中恰恰是有意义的查询词；
- **古语功能词**：`thou thee thy hath doth o mine`——它们在莎士比亚文体中有区分度，留作“激进版”对照。

线下方但保持可检索的代表词：`you`（38.5%）、`he`（33.3%）、`we`（26.7%）。它们的 df 被短诗稀释，是文档粒度效应的直接证据。

39 词停用表覆盖正文 **309,760 / 866,626 = 35.74%** 的 token；过滤后保留 556,866 个 token，词型从 25,705 降到 25,666（不词干化时）。报告第 3.5 节给出完整结果，第 4.1 节讨论这条线的合理性。

### 2.5 词干化

#### 2.5.1 收缩词预处理

分词后的 token 可能带内部撇号（`who's`、`o'er`、`marry'd`）。词干化前先做收缩词规范化：

1. 按最长优先剥离一个收缩后缀：`n't`、`'ll`、`'re`、`'ve`、`'s`、`'d`、`'t`、`'m`；
2. 去掉剩余的所有撇号（省略式，如 `o'er` → `oer`）。

文档化的不规则结果：`can't → ca`、`won't → wo`（`n't` 整段剥离遇到英语不规则拼写）；`was → wa`、`this → thi` 是 Porter 论文 step 1a 中无条件的 `S` 规则造成的，保留为论文行为。

#### 2.5.2 Porter 算法

实现严格按 Porter 1980 的规则表：step 1a（复数）、1b（`-ed` / `-ing`）、1c（词尾 `y`）、step 2（双后缀映射）、step 3（`-icate` / `-ative` / `-alize` 等）、step 4（残余后缀）、step 5a（词尾 `e`）、step 5b（双 `l`）。

核心概念是 measure `m`：单词中“元音-辅音”对（VC）的个数。元音定义为 `a e i o u`，以及前面是辅音的 `y`。规则的条件用 `m`、`*v*`（词干含元音）、`*d`（双辅音结尾）、`*o`（cvc 且最后字母不是 `w/x/y`）表达。

一个容易实现错的细节是后缀匹配的“首个匹配说了算”：规则表按后缀长度从长到短排列；一旦某个后缀匹配，就只检查该规则的条件，条件失败**不再**尝试更短的后缀。例如 `agreement` 匹配 `ement`，条件在 `agre` 上不满足，于是保持不变，而不是退回去匹配 `ment`。最初的实现写成“条件失败继续试更短后缀”，导致 41 个词形被过度词干化（例如 `agreem`），后来由全词表对拍抓出并修正。

#### 2.5.3 变体选择

实现采用“论文 + 作者 canonical C 实现中三处 `--DEPARTURE--`”的语义，等价于 NLTK 的 `MARTIN_EXTENSIONS`：

1. 两个字母及以下的词不改（长度护栏）；
2. `bli → ble`（推广论文的 `abli → able`，如 `possibli → possibl`）；
3. `logi → log`（如 `chronologi → chronolog`）。

选择理由：作者本人推荐这些改进；`is → i` 一类合并过于激进；且与下载的 canonical C 实现一致。NLTK 的 `MARTIN_EXTENSIONS` 模式作为独立 oracle，用于全词表对拍。

规范化顺序在全链路统一为：**小写化 → 停用词过滤 → 词干化**（先滤后干）。这样停用词判断针对原始词形，词干化只作用于保留下来的词。

### 2.6 倒排索引

#### 2.6.1 posting 结构

对每个词干 `t`，保存一个按 docID 严格递增的 posting 列表：

```text
t → [ { docID: u32, posStart: u32, tf: u32 } ... ]
      + positions: u32[]（所有文档的位置连续存放，按 posStart/tf 切片）
```

- `docID` 从 1 开始，严格递增；
- `posStart` 是该文档在 `positions` 数组中的起始下标；
- `tf` 是该文档内的出现次数；
- `positions` 是 token 在**过滤后 token 流**中的偏移（停用词不占位置），用于短语匹配。

将 positions 放在独立的 arena 中，而不是每个 posting 各带一个 `vector`，可以减少小对象和内存分配次数，也让序列化更直接。

#### 2.6.2 词典结构：伸展树与哈希表

系统定义了统一的 `Dictionary` 接口：

```cpp
virtual TermEntry& upsert(const std::string& term) = 0;  // 查找或插入
virtual TermEntry* find(const std::string& term) = 0;    // 只查找
virtual std::vector<TermEntry*> sortedEntries() = 0;     // 字典序输出
```

两个实现：

**伸展树（主结构）**：自调整二叉搜索树。每次查找/插入后把访问节点通过 zig / zig-zig / zig-zag 旋转到根。支持的操作是“只插入、不删除”；词流呈 Zipf 分布，高频词会停留在根附近；中序遍历直接得到字典序词表，正好用于冻结 term 表。构建期统计字符串比较次数与旋转次数。

**哈希表（基线/互验）**：链地址法，FNV-1a 64 位哈希，负载因子超过 0.75 时翻倍 rehash。平均 O(1) 查找，但输出有序词表需要额外收集与排序。它与伸展树走完全相同的构建管线，产出的索引文件必须逐字节一致——这是独立于 Python 对拍的另一层互验。

选择伸展树作为主结构的理由：本场景“只插不删”，红黑树最复杂的删除情形不存在；访问高度偏斜，splay 的自调整能利用热点；最终需要字典序输出，中序遍历免费排序。哈希表保留为速度基线与交叉验证。

#### 2.6.3 构建算法

```text
for each doc D in manifest:                 // docID 递增
    position ← 0
    for token t in tokenize(D):             // 默认只统计正文行
        if t ∈ stopwords: continue          // 停用词不占 position
        s ← stem(t)
        dictionary.upsert(s).postings.append(D.id, position)
        position ← position + 1
    index.docs.append(D.id, title, position)
    index.tokenCount ← index.tokenCount + position

terms ← dictionary.sortedEntries()           // 伸展树中序 / 哈希收集后排序
serialize(index) → index.bin
```

构建期同时统计 token 总数、不同词干数、posting 数、position 数、索引文件大小与构建耗时。

#### 2.6.4 持久化格式（v1）

索引文件为小端、自描述、确定性序列化：

```text
[header]
  magic        "MSE1"       4 B
  version      u32          4 B   （v2 预留给压缩编码）
  flags        u32          4 B   （bit0=停用词，bit1=词干化）
  docCount     u32          4 B
  termCount    u32          4 B
  tokenCount   u64          8 B

[doc 表]   每项：docID u32, keptTokens u32, title（u32 长度 + 字节）
[term 表]  按字典序，每项：
             term（u32 长度 + 字节）
             df u32
             tf u64
             postingsOffset u64
             postingsBytes u32
[postings] 每个 term：
             df 条 { docID u32, tf u32, positions u32[tf] }
```

写入前先计算所有 offset，顺序写出；读取时先读 term 表，再按 offset 跳转到各自的 postings 块。这样写入与读取共享同一份规格，`dump` 可以验证往返一致性。

#### 2.6.5 不变量

`validateIndex()` 在加载后检查以下不变量，任一违反即抛出异常：

- docID 稠密且为 `1..N`；
- term 表严格字典序递增，且无空词；
- 每个 term 的 posting 非空；
- posting 内 docID 严格递增且不超过文档数；
- 每条 posting 的 `tf > 0`，且 `posStart` 连续（dense）；
- 每个 term 的 `tf` 之和等于其 positions 数组长度；
- 所有 term 的 position 总数等于 `tokenCount`；
- 所有文档的 `keptTokens` 之和等于 `tokenCount`。

### 2.7 查询处理

#### 2.7.1 查询管线

查询文本先用与语料相同的 tokenizer 切分，再按索引头标志位执行停用词过滤与词干化：

```text
query text → tokenize → [stop word check] → [stem] → terms
           → 词典二分查找 → posting 合并/位置验证 → 评分 → 阈值过滤 → 输出
```

词典查找使用冻结 term 表上的二分搜索，复杂度 O(log #terms)。

#### 2.7.2 三种查询模式

**单词（Word）**：要求恰好一个可检索 term；多个 token 会给出提示并返回空集。结果按“匹配词数降序、Σtf 降序、docID 升序”排序。

**短语（Phrase）**：把短语切成词序列 `w1..wk`，要求所有词都在索引中；选取 df 最小的词作为候选文档，对每个候选文档做位置验证：`pos(w_{i+1}) = pos(w_i) + 1`。由于 positions 在 posting 内按升序存放，验证使用二分查找。短语查询不受 `--max-df` / `--min-df` 裁词影响——短语的语义要求整段匹配，丢掉任何一个词都会改变语义。

**布尔（Boolean）**：`--words "a b c"` 指定多个词，默认要求全部命中（AND）；`--min-match k` 表示至少命中 k 个词；`--or` 是 `--min-match 1` 的简写。被管线或阈值丢弃的词不参与匹配，输出用 `note:` 行说明原因。

#### 2.7.3 评分与四个阈值

| 参数 | 含义 | 默认 |
|---|---|---|
| `--max-df <ratio>` | 丢弃 `df_ratio > ratio` 的查询词（太常见、区分度低） | 1.0（关闭） |
| `--min-df <n>` | 丢弃 `df < n` 的查询词（太罕见） | 1（关闭） |
| `--min-match <k>` | 布尔查询至少命中 k 个词；0 = 全部命中 | 0 |
| `--top-x <n>` | 只保留得分最高的 n 篇文档 | 0（不限） |

评分向量为 `(matched, tfSum, docID)`，排序规则为 matched 降序 → tfSum 降序 → docID 升序，保证结果确定性。`minMatch` 会被钳制到可用词数：如果查询只有 2 个可检索词，`--min-match 3` 等价于 2。

#### 2.7.4 输出

默认输出 `docID<TAB>标题`，多词查询附带 `[matched k/n, tf ...]`；`--pretty` 会重跑同一管线，把命中 position 映射回源行并打印；`--json` 输出机器可读结果，供阈值实验脚本消费。命中位置的映射函数 `lineOfPosition()` 只遍历 `LineKind::Body` 行，并对停用词跳过位置计数，因此与建索引时的 position 语义一致。

### 2.8 复杂度汇总

| 操作 | 时间复杂度 | 空间 |
|---|---|---|
| 分词 | O(字符数) | O(token 数) |
| 词频统计 | O(token 数)，哈希平均 O(1)/词 | O(distinct 词数) |
| 建索引（splay） | 摊还 O(log #terms)/次访问 | O(postings + positions) |
| 建索引（hash） | 平均 O(1)/次访问 | O(postings + positions) |
| 单词查询 | O(log #terms + df) | O(#docs) |
| 短语查询 | O(df_rarest × k × log tf) | O(#docs + k) |
| 布尔查询 | O(Σ df_i + #docs log #docs) | O(#docs) |

---

## 第 3 章 测试与结果

### 3.1 测试策略

测试分为四层，覆盖“规格是否正确”和“实现是否忠实于规格”两个问题：

1. **单元测试**：Catch2 v3（vendored，仅测试可执行文件链接），覆盖 tokenizer、stemmer、dict、index、query、stopwords；
2. **黄金样例**：讲义中的四文档例子，逐项比对 posting 表；
3. **独立对拍**：用 Python 独立实现同一规格，与 C++ 输出逐字节或逐项比较；
4. **不变量与边界**：索引加载校验、损坏文件、空查询、停用词查询、未知词等。

`ctest` 注册了 6 个单元测试可执行文件，外加 `mse_stats` 冒烟测试与 `crosscheck_build` / `crosscheck_brute_force` 对拍测试（Python 可选；未安装时跳过）。

### 3.2 单元测试结果

本次在 WSL2 + g++ 15.2（`-std=c++17 -O1`）下编译并运行六个测试可执行文件，全部通过：

| 测试文件 | TEST_CASE 数 | 断言数 | 结果 |
|---|---:|---:|---|
| `test_tokenizer` | 2 | 24 | 通过 |
| `test_stemmer` | 7 | 101 | 通过 |
| `test_dict` | 5 | 34 | 通过 |
| `test_index` | 4 | 50 | 通过 |
| `test_query` | 7 | 26 | 通过 |
| `test_stopwords` | 3 | 11 | 通过 |
| **合计** | **28** | **246** | **全部通过** |

此外，`mse stats` 冒烟测试确认语料仍为 195 个文档；`crosscheck_brute_force` 对全词表对拍通过（见 3.4）。

### 3.3 黄金样例：讲义四文档

`tests/fixtures/golden4/` 复刻了讲义 §1.2 的四文档例子：

| docID | 文本 |
|---:|---|
| 1 | Gold silver truck |
| 2 | Shipment of gold damaged in a fire |
| 3 | Delivery of silver arrived in a silver truck |
| 4 | Shipment of gold arrived in a truck |

原始构建（无停用词、无词干化）得到 4 个文档、11 个词、25 个 token。逐项比对结果：

| 词 | posting（docID, tf） |
|---|---|
| a | (2,1) (3,1) (4,1) |
| arrived | (3,1) (4,1) |
| damaged | (2,1) |
| delivery | (3,1) |
| fire | (2,1) |
| gold | (1,1) (2,1) (4,1) |
| in | (2,1) (3,1) (4,1) |
| of | (2,1) (3,1) (4,1) |
| shipment | (2,1) (4,1) |
| silver | (1,1) (3,2) |
| truck | (1,1) (3,1) (4,1) |

位置校验：doc1 中 `silver` 在 position 1；doc3 中 `silver` 在 positions 2 和 6。

**一处参考材料复核**：讲义表格把 `a` 记为 (2,1)(4,1)，但示例句子中 doc3 的原文是 “Delivery of silver arrived in **a** silver truck”，按句子重算应为 (2,1)(3,1)(4,1)。项目以句子为建索引的事实依据，测试中显式记录了这一差异。这一点也说明：引用示例时应以原始文本为准，而不是以二手表格为准。

停用词版本（过滤 `a/of/in`）得到 8 个词；`silver` 在 doc3 中的位置重新编号为 1 和 3，验证了“停用词不占 position”的语义。

### 3.4 独立对拍

对拍的价值在于：如果 C++ 与 Python 两边都错，只有“同错”才会通过；因此规格中的每个模糊点都必须在两边独立写成一致的行为。本项目的对拍结果如下：

| 对拍项 | 规模 | 结果 |
|---|---|---|
| 逐文档 token 数（C++ tokenizer vs Python） | 195 文档 | 100% 一致，866,626 token |
| `wordcount.csv` 默认配置 | 25,705 行 | 逐字节一致 |
| `wordcount.csv --keep-speakers` | 25,705 行 | 逐字节一致 |
| `wordcount.csv --stop-words` | 556,866 kept / 309,760 removed | 逐字节一致 |
| `wordcount.csv --stem` | 14,947 词干 | 逐字节一致（NLTK `MARTIN_EXTENSIONS`） |
| `wordcount.csv --stem --stop-words` | 14,937 词干 | 逐字节一致 |
| 词典层随机工作负载 | 20,000 次操作 / 300 词 | splay 与 hash 词表完全一致 |
| 索引文件（splay vs hash） | 3,685,655 B | **逐字节一致** |
| `brute_force.py` 全词表对拍 | 14,937 词干 | df、tf、docID、positions 全项一致 |
| `brute_force.py` raw 版 | 25,705 词 | 全项一致（见 notes.md 记录） |

`brute_force.py` 是独立实现的第二套索引路径：它重新扫描 `data/text/`，用独立的 tokenizer/停用词表/NLTK Porter 重建 posting，同时用独立的二进制解析器读取 `index.bin`，然后逐词比较。它不是“调用 C++ 的结果再比较”，而是真正从原始数据重建期望值。

### 3.5 任务 (1) 结果：词频与停用词

默认配置（只统计正文行）的统计结果：

```text
documents: 195
tokens   : 866,626
distinct : 25,705
hapax    : 9,753 (tf = 1)
```

top 10 词项：

| rank | term | tf | df | df_ratio | tf_share |
|---:|---|---:|---:|---:|---:|
| 1 | the | 28,436 | 177 | 90.8% | 3.28% |
| 2 | and | 25,553 | 189 | 96.9% | 2.95% |
| 3 | i | 21,131 | 154 | 79.0% | 2.44% |
| 4 | to | 20,302 | 187 | 95.9% | 2.34% |
| 5 | of | 16,976 | 175 | 89.7% | 1.96% |
| 6 | a | 14,527 | 138 | 70.8% | 1.68% |
| 7 | you | 13,998 | 75 | 38.5% | 1.62% |
| 8 | my | 12,934 | 159 | 81.5% | 1.49% |
| 9 | that | 11,513 | 172 | 88.2% | 1.33% |
| 10 | in | 11,479 | 181 | 92.8% | 1.32% |

stop_candidate 粗筛（`df_ratio ≥ 0.75 且 tf_share ≥ 0.1%`）得到 9 个词：`the and i to of my that in but`。最终按 2.4.2 的 0.40 候选带 + 人工结构判定得到 39 词停用表，覆盖 35.74% 的 token。

滤波前后对比：

| 配置 | token 数 | distinct | hapax | 说明 |
|---|---:|---:|---:|---|
| 不过滤 | 866,626 | 25,705 | 9,753 | 基线 |
| `--stop-words` | 556,866 | 25,666 | 9,753 | removed 309,760（35.74%） |
| `--stem` | 866,626 | 14,947 | 4,843 | 词干化合并词形 |
| `--stem --stop-words` | 556,866 | 14,937 | 4,846 | 正式索引配置 |

### 3.6 任务 (2) 结果：索引构建

同一份语料、同一构建管线，四种配置的索引规模：

| 配置 | terms | postings | positions | 索引大小 |
|---|---:|---:|---:|---:|
| raw | 25,705 | 152,650 | 866,626 | 5,598,793 B |
| stopwords | 25,666 | 147,909 | 556,866 | 4,320,622 B |
| stem | 14,947 | 121,903 | 866,626 | 4,958,598 B |
| **stopwords + stem** | **14,937** | **117,704** | **556,866** | **3,685,655 B** |

观察：

- 停用词过滤主要减少 token 与 posting（-35.74% token），但词型只减少 39 个；
- 词干化主要减少词型（25,705 → 14,947，-41.9%），但 token 数不变；
- 两者叠加得到最小的索引：14,937 个词干、117,704 条 posting、556,866 个位置、3.5 MB；
- `stem` 单独使用时索引反而比 `stopwords` 单独使用更大（4.96 MB vs 4.32 MB），因为它保留了全部 866,626 个位置；这说明“词表小”不等于“索引小”，posting 与 positions 的体积同样重要。

词典对比实验（正式配置 = stopwords + stem）：

| 词典 | 构建耗时 | 字符串比较次数 | 比较次数/访问 | 旋转 / rehash | 索引输出 |
|---|---:|---:|---:|---:|---|
| 伸展树（主） | 0.32 s | 6,392,724 | ≈ 11.5 | 5,850,795 次旋转 | 3,685,655 B |
| 哈希（基线） | 0.16 s | 688,494 | ≈ 1.24 | 11 次 rehash | **逐字节相同** |

说明：耗时是本机（WSL2, g++ 15.2, `-O2`）的测量，会随硬件变化；字符串比较次数、旋转次数与 rehash 次数是确定性指标，与硬件无关。

### 3.7 任务 (3) 结果：查询

正式索引上的代表性查询：

| 查询 | 模式 | 结果 | 说明 |
|---|---|---:|---|
| `osric` | word | 1 篇（Hamlet, doc 30） | tf=3，positions 17629/17933/18173 |
| `the` | word | 0 篇 | 停用词未建索引，输出 note |
| `my lord` | phrase | 42 篇 | `my` 是停用词，短语退化为 `lord` |
| `to be or not to be` | phrase | 0 篇 | 全部是停用词，输出 6 条 note |
| `king queen` | boolean, AND | 33 篇 | 默认全部命中；OR（k=1）为 43 篇 |
| `king richard` | phrase | 7 篇 | 两个词都在索引中 |
| `good night` | phrase | 23 篇 | 位置相邻验证 |
| `sweet sword moon` | boolean, AND | 30 篇 | k=1 时 93 篇，k=2 时 39 篇 |

`--pretty` 示例：

```text
$ ./build/mse query --index build/index.bin --stop-words data/stopwords.txt \
                    --word osric --pretty
 30    Hamlet
      > Osric, who brings back to him that you attend him in
```

`--dump` 示例：

```text
term osric: df=1, tf=3
  doc 30 "Hamlet"  tf=3  positions: 17629 17933 18173
```

### 3.8 任务 (4) 结果：阈值实验

`tools/threshold_matrix.py` 对 15 条查询（8 类：高频、中频、低频、专名、短语、含停用词短语、布尔、停用词）逐一扫描四个参数，每次只改变一个参数，共 **201 条测量**。每条测量记录返回文档数、与基线（全部阈值关闭）的 Jaccard 相似度、note 数与查询耗时，输出 `build/threshold_matrix.csv` 和 `build/threshold_matrix.svg`。

**`--max-df`（丢弃“太大众”的词）**

| 查询 | 基线 | 0.9 | 0.7 | 0.5 | 0.3 | 0.1 |
|---|---:|---:|---:|---:|---:|---:|
| love（df_ratio 0.71） | 138 | 138 | **0** | 0 | 0 | 0 |
| sweet（0.46） | 90 | 90 | 90 | 90 | **0** | 0 |
| good（0.29） | 57 | 57 | 57 | 57 | 57 | **0** |
| night（0.29） | 56 | 56 | 56 | 56 | 56 | **0** |
| sword（0.19） | 37 | 37 | 37 | 37 | 37 | **0** |
| moon（0.18） | 35 | 35 | 35 | 35 | 35 | **0** |
| banquo / osric（df=1） | 1 | 1 | 1 | 1 | 1 | 1 |
| king queen | 33 | 33 | 33 | 33 | 33 | **0** |
| sweet sword moon | 30 | 30 | 30 | 30 | 30 | **0** |
| 短语查询 | 7/23/42 | 7/23/42 | 7/23/42 | 7/23/42 | 7/23/42 | 7/23/42 |
| the / of | 0 | 0 | 0 | 0 | 0 | 0 |

**`--min-df`（丢弃太罕见的词）**

| 查询 | 基线 | 2 | 5 | 20 | 50 |
|---|---:|---:|---:|---:|---:|
| banquo / osric（df=1） | 1 | **0** | 0 | 0 | 0 |
| sword（df=37） | 37 | 37 | 37 | 37 | **0** |
| moon（df=35） | 35 | 35 | 35 | 35 | **0** |
| king queen | 33 | 33 | 33 | 33 | **0** |
| sweet sword moon | 30 | 30 | 30 | 30 | **90**（Jaccard 0.33） |

**`--min-match`（布尔语义松紧）**

| 查询 | 基线（AND） | k=1 | k=2 | k=3 |
|---|---:|---:|---:|---:|
| king queen | 33 | 43（Jaccard 0.77） | 33 | 33（钳制为 2） |
| sweet sword moon | 30 | 93（Jaccard 0.32） | 39（0.77） | 30 |

**`--top-x`（结果截断）**

| 查询 | 基线 | 5 | 10 | 20 |
|---|---:|---:|---:|---:|
| love | 138 | 5（Jaccard 0.04） | 10（0.07） | 20（0.14） |
| good night | 23 | 5（0.22） | 10（0.43） | 20（0.87） |
| king richard | 7 | 5（0.71） | 7（1.00） | 7（1.00） |
| banquo / osric | 1 | 1（1.00） | 1（1.00） | 1（1.00） |

关键结论：

1. **`--max-df` 会直接消除高频词的查询结果**。单词查询一旦词被丢弃，结果从上百篇直接归零；`love` 在 0.7 档（df_ratio 0.71）就被丢弃，`sweet` 在 0.3 档被丢弃，说明阈值必须按查询意图设定，不能一律取小。
2. **`--min-df` 与多词 AND 组合会产生反直觉的“丢词即放宽”**。`sweet sword moon` 在 `--min-df 50` 下丢弃了 `sword` 和 `moon`，只剩 `sweet`，结果从 30 篇增加到 90 篇，Jaccard 仅 0.33。这是“阈值作用于词、而不是作用于结果”的直接后果。
3. **`--min-match` 是布尔语义的松紧旋钮**。k=1 即 OR，结果集显著扩大；k 超过可检索词数时会被钳制，不会返回空集。
4. **`--top-x` 是纯截断**，Jaccard 随 x 单调回升；对结果少的查询（≤x）没有影响。
5. **短语查询不受 `--max-df/--min-df` 影响**，Jaccard 恒为 1.00。这是设计行为：短语必须整段匹配，丢弃任何一个词都会改变语义。
6. **停用词查询恒为空**，与阈值无关；这是过滤索引的预期行为，输出 note 解释原因。

### 3.9 测试发现并修复的问题

开发过程中，测试与对拍抓出了六个真实问题。它们比“全部通过”本身更能说明测试的有效性：

| # | 现象 | 原因 | 处理 |
|---:|---|---|---|
| 1 | 对拍脚本比 C++ 少算 4,340 个 token | 只按行首判断标记行，把 `[To HELENA] ...` 之类台词误判为标题行 | 改为行首与行尾同时匹配，并写入文本层规范 |
| 2 | 正文混入 `aside`、`sings` 和人名 | 610 处方括号舞台提示被当作正文 | 提取阶段把行首 `[...]` 重定位为 `(...)` 舞台行；净减 849 token，与片段统计吻合 |
| 3 | 过滤版词频的 `tf_share` 分母错误 | C++ 曾把过滤前 token 总数记入 `tokens` | 只累计通过过滤器的 token；`stopwordTokens` 单独累计；两版 CSV 恢复逐字节一致 |
| 4 | 41 个词形被过度词干化（如 `agreem`） | 误以为后缀条件失败后应继续尝试更短后缀 | 改为“首个匹配说了算、条件失败即停”，与 canonical C 一致 |
| 5 | 伸展树插入后段错误 | 插入分支保留了 `Node*&` 子指针引用，splay 旋转重接线后引用失效 | 先把新节点指针复制到局部变量，再 splay |
| 6 | 索引加载误报 “index file is truncated” | `Reader reader(readWholeFile(path))` 中临时 vector 在整句结束后销毁，Reader 持有的引用悬垂 | 先把文件内容存入具名变量，再构造 Reader |

另外，讲义四文档的 `a` 词项差异（3.3 节）说明：黄金样例本身也需要复核，不能盲目相信参考材料中的二手表格。

### 3.10 小结

第 3 章的验证链条可以概括为：

```text
单元测试（模块行为）
   + 黄金样例（已知输入/输出）
   + 独立 Python 对拍（同一规格的第二实现）
   + 不变量校验（结构自洽）
   + 全量 brute-force（从原始文本重建期望）
   = 对索引正确性的高置信度
```

阈值实验则回答了任务 (4)：四个参数分别从“词侧”和“结果侧”改变结果集，且不同查询类型（单词/短语/布尔/停用词）对阈值的反应不同。具体分析见第 4 章。

---

## 第 4 章 分析与讨论

### 4.1 停用词界线与文档粒度

“线画在哪里”没有唯一正确答案，它取决于语料、文档粒度和查询意图。本项目把线画在 0.40 的 df_ratio 候选带，再人工保留结构性功能词，得到 39 词。这个选择有三点依据：

1. **0.75 的粗筛线漏掉太多功能词**。154 首十四行诗很短，很多对话中的功能词（如 `you`、`me`、`be`）在诗歌中不出现，df_ratio 被稀释；如果只看 0.75，会得到 9 个词，过滤后仍保留大量噪声。
2. **0.40 之后开始混入内容词**。在 0.40 档，新增词中出现了 `love`、`time`；它们虽然高频，但在文学检索中有实际意义，不应删除。把线停在 0.40、再用“结构性功能词”做人工判定，可以在“去噪”和“保留检索价值”之间取得平衡。
3. **文档粒度会改变 df**。本项目采用“一个完整作品 = 一个文档”（195 篇）。如果把 154 首十四行诗合并成 1 篇（42 篇文档），诗歌对 df 的稀释会减弱，候选表会整体上移；这是 plan.md 中预留的粒度敏感性实验，尚未完成。报告中应明确：39 词表是针对当前 195 文档粒度推导的，不是普适常数。

另一个证据是 `the` 的 df_ratio 只有 90.8%，而不是接近 100%：154 首短诗中没有出现 `the` 的篇目不少。这说明 df_ratio 的绝对值高度依赖文档粒度；跨语料比较时更稳妥的指标是 tf_share 与词性/功能类别的结合。

### 4.2 Zipf 分布与 hapax

语料呈现典型的 Zipf 分布：

- 词型 25,705 个，其中 hapax 9,753 个，占 **38.0%** 的词型；
- 但 hapax 只占 **1.1%** 的 token；
- top 10 词项覆盖了相当大比例的 token，`the` 一个词就占 3.28%。

这对索引设计有两个含义：

1. **词表被大量低频词占据**。14,937 个词干中，近一半只出现一次；如果为每个词项分配固定大小的词典节点，内存会被“长尾”吃掉。字典结构的选择必须考虑这一点。
2. **高频词与低频词的处理策略应不同**。高频词（尤其是停用词）贡献了大部分 posting 长度和查询时的候选集，但区分度低；低频词区分度高，但 posting 很短。停用词过滤、`--max-df/--min-df`、`--top-x` 都是在这条长尾上做取舍。

一个有趣的反作用：在正式配置中，最高频的词（`the/and/...`）已经被停用词表过滤掉，伸展树面对的 Zipf 偏斜被削弱了。这解释了为什么 splay 的比较次数（≈11.5 次/访问）只比平衡树理论下界 `log2(14937) ≈ 13.9` 好一点，而没有出现“热点词全部贴近根”的极端优势。

### 4.3 词典结构对比

| 指标 | 伸展树 | 哈希 |
|---|---|---|
| 构建耗时 | 0.32 s | 0.16 s |
| 字符串比较/访问 | ≈ 11.5 | ≈ 1.24 |
| 旋转/rehash | 5,850,795 次旋转 | 11 次 rehash |
| 有序输出 | 中序遍历，免费 | 收集后排序，O(n log n) |
| 删除 | 不需要（只插不删） | 不需要 |
| 索引输出 | 逐字节一致 | 逐字节一致 |

哈希在纯构建速度上明显胜出；伸展树的优势在于“冻结 term 表”时无需额外排序，且其自调整特性在热点明显的词流上更稳健。在本项目中，词表只有约 1.5 万项，两者都远未触及性能瓶颈；选择伸展树作为主结构更多是教学目的（对应 class01 的内容）与结构性理由，而不是因为它在所有指标上更优。哈希作为基线，最大的价值是提供了“两种独立实现产出同一索引文件”的交叉验证。

如果把规模放大到百万级词表，哈希的 O(1) 优势会更明显；但哈希无法支持前缀查询和有序遍历，需要额外的有序结构。plan.md 中“哈希 + 冻结有序表”的组合正是对讲义 §2.3 结论的呼应：哈希负责构建期的高速查找，冻结后的有序 term 表负责查询期的二分与前缀能力。

### 4.4 索引压缩的机会

当前 v1 格式直接存储 32 位整数：每个 posting 12 B（docID + tf + posStart），每个 position 4 B。正式索引 3,685,655 B 中，posting 与 positions 约 3.17 MB，term 表与头部约 0.5 MB。可压缩的空间主要来自：

1. **docID 差分编码**：同一 term 的 posting 按 docID 递增，存储 gap 而不是绝对值；常见 gap 很小，可用变长字节编码（1–2 B）。
2. **positions 差分编码**：同一 posting 内 positions 递增，存储相邻位置差；短语查询仍然可以恢复绝对位置。
3. **term 表前端编码（front coding）**：按字典序排列的 term 共享前缀，可只存“与上一个词的最长公共前缀长度 + 后缀”。
4. **v2 版本预留**：索引头已有 `version` 字段，v1 与 v2 可以只改变 postings 段编码，term 表/头部结构不变。

按讲义《Compression》的思路，还可以讨论 Elias γ 与变长向量编码；本报告只做分析，不实现。plan.md 中的压缩对比实验（raw u32 vs 差分 + 字节对齐变长）尚未完成，建议作为后续工作：记录索引体积、构建耗时、加载耗时与 dump 正确性四项指标，并与当前 v1 对比。

### 4.5 查询阈值的语义与权衡

阈值实验揭示了一个重要区别：**阈值作用于“查询词”，而不是直接作用于“结果文档”**。

- `--max-df` 与 `--min-df` 是**词侧截断**：它们先决定“哪些查询词参与匹配”，再由匹配结果决定文档集。因此，单词查询被丢词后结果直接归零；多词 AND 查询被丢词后可能“放宽”为更少的词，结果反而变多（`sweet sword moon` 在 `--min-df 50` 下从 30 篇变为 90 篇）。
- `--min-match` 是**布尔语义的松紧**：它不丢弃词，只改变“至少命中几个词才算命中”。k=1 即 OR，k=termCount 即 AND。
- `--top-x` 是**结果侧截断**：它按得分保留前 n 篇，不改变匹配语义，但会牺牲召回率。Jaccard 随 x 单调回升，符合预期。

从系统设计角度看，索引期截断与查询期截断是两种不同的权衡：

| 截断位置 | 优点 | 缺点 |
|---|---|---|
| 建索引期（讲义 §4.6 的 top-x posting） | 索引体积小、查询快 | 一旦截断，后续查询无法恢复被丢弃的文档；构建成本高 |
| 查询期（本项目的 `--top-x`） | 保留完整索引，阈值可按查询调整，灵活 | 查询时需要遍历完整 posting，长 posting 的代价高 |

本项目的选择是“查询期截断”，因为课程语料规模不大，保留完整索引更利于实验与对拍；如果面向大规模语料，则应考虑索引期截断、分层索引或 WAND 等提前终止算法。

另一个设计行为是短语查询不受词侧阈值影响。这是语义决定的：短语是一个整体，丢掉任何一个词都不再是原短语。报告在第 3.8 节用 Jaccard 恒 1.00 验证了这一点。

### 4.6 局限性与改进方向

**已知局限**

1. **短语在“过滤后 token 流”上匹配**。这是本项目最重要的语义局限：`my lord` 会退化为 `lord`，`to be or not to be` 会因为全部是停用词而返回空集。讲义中提醒过 `to be or not to be` 的例子；本项目选择“先滤后干”的管线，因此必须把这一点写入文档并配测试。
2. **停用词查询返回空集**。这是过滤索引的预期行为，但对用户来说可能不直观；系统用 note 行解释原因。
3. **排序只使用 matched 与 Σtf**，没有 TF-IDF、BM25、文档长度归一化等相关性模型；对于“返回文档 ID”的任务要求已经足够，但检索质量有提升空间。
4. **文档粒度固定为作品级**。scene 级 manifest（约 1,080 篇）只预留为实验，尚未完成；粒度会显著影响 df 与停用词候选表。
5. **只支持 ASCII**。全语料非 ASCII 字符为 0，因此当前实现按字节处理；换语料（尤其是中文）需要重写分词与规范化。
6. **全内存设计**。Corpus、词典、posting、positions 全部驻留内存；当前语料约 5 MB 文本、3.5 MB 索引，没有问题，但无法直接扩展到 Bonus 的规模。
7. **无压缩、无并发、无增量更新**。v1 是只读索引；更新需要重建。

**改进方向**

- 完成压缩实验（docID/position 差分 + 变长编码），量化索引体积与查询延迟的权衡；
- 补 `std::map` 词典第三列，完成 plan.md §6.5 的词典对比；
- 完成 scene 级粒度与“154 首合并”的敏感性实验；
- 增加短语级 brute-force 对拍（随机短语的独立验证）；
- 增加排序模型（TF-IDF/BM25）与 `--top-x` 结合；
- 为 Bonus 设计外部排序/分块构建/分布式方案（见 4.7）；
- 提交前补齐注释率（当前 `src/` + `include/` 非空行注释率约 18.6%，低于课程要求的 30% 目标）。

### 4.7 Bonus：500,000 个文件与 400,000,000 个不同单词（开发中）

> **状态说明**：本节是截至 2026-10-19 的分析草案，Bonus 的实现与实验仍在开发中。以下数字为基于当前实现特征的估算，待补入实测数据后定稿。

#### 4.7.1 当前设计能否工作？

**结论：不能按当前实现直接工作。** 当前系统的核心假设是“整个语料、整个词典、整个 posting 表都能驻留内存”。在 500,000 个文件、400,000,000 个不同词的规模下，这个假设不再成立。

按当前语料的平均值外推：

- 当前 195 个文档约 4.9 MB 文本、866,626 个正文 token，平均每篇约 25 KB、4,444 个 token；
- 500,000 篇 × 25 KB ≈ **12.5 GB 文本**；
- 500,000 篇 × 4,444 token ≈ **22 亿个 token 出现**；
- 当前配置下 posting 数 / token 数 ≈ 0.211，外推约 **4.7 亿条 posting**。

内存估算（数量级）：

| 组成部分 | 估算 | 说明 |
|---|---:|---|
| 词典（400M terms） | 40–60 GB | 每项按 100–150 B 估算（字符串 + 节点 + 元数据） |
| postings（≈4.7 亿条） | 5.6 GB | 每条 12 B（docID/tf/posStart） |
| positions（≈22 亿个） | 8.9 GB | 每个 4 B |
| 语料文本 | 12.5 GB+ | 还有行对象与字符串开销 |
| **合计** | **≈ 70–90 GB+** | 尚未计 vector 容量冗余与哈希桶 |

这还没有算上构建期的临时结构、`std::string` 的小对象开销与内存碎片。普通工作站（16–64 GB 内存）无法容纳；即使内存足够，单机全内存构建的时间与 GC/分配压力也不可接受。

另一个限制是 32 位字段。当前 `docID` 用 `u32`（500,000 足够），`tf` 用 `u32`，`posStart` 用 `u32`。如果总 token 数超过 `2^32 ≈ 42.9 亿`，`posStart` 可能溢出；本估算的 22 亿 token 尚未越界，但更大规模时应改为 `u64`。

#### 4.7.2 需要改变什么？

**（1）外部排序 / 分块构建（SPIMI 思路）**

不再一次性把全部 posting 放在内存，而是按内存预算分块：

```text
for each block of documents that fits in memory:
    build a local inverted index (term → postings)
    write the local index to disk as a sorted run
merge all runs with a k-way merge, combining postings for the same term
```

设可用内存 40 MB、每条三元组 `<termID, docID, tf>` 约 12 B，则一个块约容纳 300 万条记录。按讲义《Building an Inverted Index》的估算，4 亿条三元组、40 MB 内存约需 100 个 run、7 趟归并、约 20 小时，临时空间约为文本的 2 倍。这个估算说明：外部排序在算法上可行，但代价是磁盘 I/O 与数小时级的构建时间。

**（2）磁盘词典与 term ID**

400M 个 term 不能全部放在内存哈希表。可选方案：

- 按字典序排列的 term 表 + 二分查找（查询 O(log 400M) ≈ 29 次比较），term 表可 mmap；
- B+ 树（为磁盘页设计，适合持久化词典）；
- 前端编码（front coding）压缩 term 字符串；
- 构建期用哈希分配 term ID，最终写回有序表。

查询时只读取目标 term 的 posting 块，而不是加载整个索引。

**（3）压缩 posting**

- docID 差分 + 变长字节编码；
- positions 差分 + 变长编码；
- 对超长 posting 做索引期 top-x 截断（讲义 §4.6）；
- term 表前端编码。

这些压缩可以把索引体积降低数倍，但会增加解码 CPU 成本；需要根据查询负载做权衡。

**（4）分布式构建与查询**

两种切分方式：

| 方案 | 优点 | 缺点 |
|---|---|---|
| 按文档切分 | 构建负载均衡；每台机器处理一部分文档 | 每次查询需要广播到所有机器并合并结果 |
| 按词切分 | 查询只访问包含目标词的少数机器 | 热门词造成热点；词表切分与负载均衡复杂 |

底层可参考 GFS 的“大文件、块复制、容错”设计。实际系统通常采用“文档切分 + 副本”或“词切分 + 热点复制”的混合策略。

**（5）查询侧优化**

- rare-term-first：先查 df 最小的词，缩小候选集；
- skip pointers / block-max WAND：跳过不可能进入 top-x 的 posting 块；
- 缓存高频词的 posting；
- 对 `--max-df/--min-df` 做查询计划级优化，而不是逐词过滤。

#### 4.7.3 当前程序的临界点

可以把内存需求写成：

```text
M ≈ T × c_term + P × 12 + O × 4 + C
```

其中 `T` 是不同词数，`P` 是 posting 数，`O` 是 token 出现数，`C` 是语料文本与对象开销，`c_term` 是每个词典项的平均开销（约 100–150 B）。

按此公式（忽略 `C` 与 vector 冗余）粗估：

| 可用内存 | 可支持的不同词数（仅词典） | 说明 |
|---:|---:|---|
| 64 MB | ≈ 0.4–0.6 M | 小规模实验 |
| 1 GB | ≈ 7–10 M | 中等语料 |
| 16 GB | ≈ 100–150 M | 大型单机 |
| 64 GB | ≈ 400–600 M | 仅词典；postings/positions 仍需额外空间 |

因此，400M 词的目标已经接近“纯词典”在 64 GB 机器上的上限；加上 22 亿 token 的 positions 与 4.7 亿条 posting，单机全内存方案不可行。当前实现可以处理“百万级词表、千万级 token”的语料，但面对 500,000 文件 / 400M 词，需要外部存储与分布式架构。

#### 4.7.4 待补实验

- 在当前语料上测量峰值内存与索引各段体积，校准 `c_term`、posting 与 positions 的实际系数；
- 构造 10×/100× 的合成语料，测量构建时间与内存随规模的增长曲线；
- 实现一个最小化的外部排序原型（分块 + 归并），验证磁盘索引的可行性；
- 对比“文档切分”与“词切分”的查询延迟与构建负载；
- 补入压缩前后索引体积与查询延迟的实测数据。

### 4.8 结论

本项目完成了题目要求的四项任务：在莎士比亚全集上实现了词频统计与停用词推导、带 Porter 词干化的倒排索引、单词/短语/布尔查询，以及四个查询阈值的系统性实验。正确性通过单元测试、黄金样例、独立 Python 对拍与全量 brute-force 四层验证；正式索引为 14,937 个词干、117,704 条 posting、556,866 个位置、约 3.5 MB。

项目的主要经验是：

1. **规格必须唯一且可执行**。分词、标记行判定、停用词顺序、词干化变体，任何一处模糊都会在 C++ 与 Python 对拍中放大成数字差异。
2. **独立实现的对拍比“再跑一遍”更有价值**。`brute_force.py` 从原始文本重建 posting，抓出了多个真实 bug。
3. **阈值作用于查询词，而不是结果**。这是理解 `--max-df/--min-df` 行为的关键，也是多词查询可能出现“丢词反而结果变多”的原因。
4. **全内存设计的边界清晰**。当前语料远低于边界；Bonus 规模需要外部排序、磁盘词典、压缩与分布式架构。

后续工作包括：完成 Bonus 的规模实验与原型、补压缩对比与词典第三列、完成粒度敏感性实验、补齐代码注释并完成最终报告排版与匿名检查。

---

## 参考文献

1. M. F. Porter. *An algorithm for suffix stripping*. Program, 14(3):130–137, 1980.
2. M. F. Porter. *The Porter Stemming Algorithm*（官方页面与 canonical C 实现）. <https://tartarus.org/martin/PorterStemmer/>, accessed 2026-10-16.
3. NLTK Project. `nltk.stem.PorterStemmer`（`MARTIN_EXTENSIONS` 模式，作为对拍 oracle）. <https://www.nltk.org/>, accessed 2026-10-16. Apache License 2.0.
4. Snowball. *The English (Porter2) stemming algorithm*. <https://snowballstem.org/algorithms/english/stemmer.html>, accessed 2026-10-16. BSD-3-Clause.
5. MIT Shakespeare. *The Complete Works of William Shakespeare*. <http://shakespeare.mit.edu/>, accessed 2026-10-10. GitHub mirror: <https://github.com/TheMITTech/shakespeare>, commit `6b82db852c7322dd33e95db347f0ddfc812c6409`.
6. 课程讲义：*Building an Inverted Index*、*Compression*、*Inverted Index Construction*、*The Google File System*.
7. C. D. Manning, P. Raghavan, H. Schütze. *Introduction to Information Retrieval*. Cambridge University Press, 2008.
8. G. Fowler, M. Vo, et al. *FNV-1a 64-bit hash*（FNV 规范）. <http://www.isthe.com/chongo/tech/comp/fnv/>, accessed 2026-10-19.

---

## 附录 A 构建与运行

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

# 词频统计（Task 1）
./build/mse count --csv build/wordcount.csv --top 50

# 正式索引（Task 2）
./build/mse build --index build/index.bin \
                  --stop-words data/stopwords.txt --stem

# 查询（Task 3）
./build/mse query --index build/index.bin \
                  --stop-words data/stopwords.txt --word osric --pretty
./build/mse query --index build/index.bin \
                  --stop-words data/stopwords.txt --phrase "my lord"
./build/mse query --index build/index.bin \
                  --stop-words data/stopwords.txt --words "king queen" --min-match 2

# 阈值实验（Task 4）
python3 tools/threshold_matrix.py

# 独立对拍
python3 tools/check_wordcount.py > build/wordcount_py.csv
diff build/wordcount.csv build/wordcount_py.csv
python3 tools/brute_force.py --stop-words data/stopwords.txt --stem
```

## 附录 B 数据与索引格式速查

`data/manifest.tsv`（制表符分隔，首行表头）：

```text
docid  kind  category  title  text  source  url
30     play  Tragedy   Hamlet data/text/0030_hamlet.txt  data/raw/hamlet/full.html  http://shakespeare.mit.edu/hamlet/full.html
```

`data/text/*.txt` 行标记：

| 标记 | 含义 | 默认 |
|---|---|---|
| `[ACT I]` / `[SCENE ...]` | 标题 | 过滤 |
| `(Exit Ghost)` | 舞台指示 | 过滤 |
| `@BERNARDO` | 说话人 | 过滤 |
| 其他 | 正文 | 保留 |
| `#` | 文件头注释 | 跳过 |

索引文件 v1：见 2.6.4 节；magic `MSE1`，小端，version 1。

## 附录 C 评分对照

| 评分项 | 分值 | 本报告对应位置 |
|---|---:|---|
| Word counting 程序 | 1 | 2.4、3.5；代码 `src/count.cpp` |
| Index generation 程序 | 5 | 2.6、3.6；代码 `src/index.cpp`、`src/dict.cpp` |
| Query processing 程序 | 3 | 2.7、3.7；代码 `src/query.cpp` |
| 倒排索引正确性测试 | 2 | 3.2–3.4、3.9 |
| 查询阈值测试 | 2 | 3.8 |
| 分析与评论 | 3 | 第 4 章 |
| Bonus | +2 | 4.7（开发中） |
| 文档第 1 章 | 1 | 第 1 章 |
| 文档第 2 章 | 2 | 第 2 章 |
| 整体文档风格 | 1 | 全文结构、表格、术语统一；待补图表编号与 PDF 排版 |

---

### 待办 / 写作备注（提交前删除）

- [ ] 补入 Bonus 实测数据（4.7 节）；
- [ ] 补入压缩实验（4.4 节）与 `std::map` 第三列（4.3 节）；
- [ ] 补入粒度敏感性实验（4.1 节）；
- [ ] 把 `build/threshold_matrix.svg` 与 Zipf 图插入第 3.8 节；
- [ ] 统一图表编号与交叉引用；
- [ ] 检查全文无姓名、学号、昵称、组号等身份信息；
- [ ] 按课程要求转 PDF，文件名使用课程格式；
- [ ] 代码注释率补齐到 ≥ 30%（当前约 18.6%）。
