# 项目报告写作约定（STYLE.md）

> 本文件是 `reports/` 下所有作者（Lead / writer-* / figure-maker / reviewer）的协作契约。
> 写作前必须通读；与本文冲突时先告知 Lead，不要自行改约定。

## 1. 交付物与目录

```
reports/
├── report.tex           # 主文件：article + ctex（Lead）
├── preamble.tex         # 导言区（Lead）
├── build.sh             # latexmk -xelatex，产物 reports/report.pdf
├── report.pdf           # 最终 PDF（由 build.sh 生成，提交物）
├── STYLE.md             # 本文件
├── sections/            # 各章 .tex（按下方 owner 表，禁止改别人的文件）
│   ├── 00-abstract.tex
│   ├── 01-introduction.tex
│   ├── 02-design.tex
│   ├── 03-results.tex
│   ├── 04-analysis.tex
│   ├── 05-references.tex
│   └── 06-appendix.tex
├── figures/             # 全部插图（figure-maker 唯一 owner）
│   ├── *.tex            # 每个文件是一整个 figure 环境（含 caption + label）
│   └── data/*.dat       # pgfplots 数据（由 CSV 生成）
└── data/                # 本轮重新运行项目产生的证据（analyst 唯一 owner）
    ├── results-tasks.md
    ├── results-verify.md
    ├── logs/tasks/  logs/verify/
    ├── csv/tasks/   csv/verify/
    └── work/        # 临时索引文件等，不作为证据
```

## 2. 硬性要求

1. 文档类必须是 `article`，中文由 `ctex` 宏包提供（已固定在 `report.tex`，不要改）。
2. **所有数字必须来自本轮重新运行项目得到的 `reports/data/results-*.md`**，
   并可用其中给出的命令/日志复现。禁止从 README.md / notes.md / plan.md / 旧报告 /
   旧 build 产物里抄数字（那些是开发期历史值，可能过期）。引用时在 `.tex` 注释里
   标注 `% src: F12`（对应 results 文件中的编号）。
3. 禁止编造数据。拿不到的数字：写 `\TODO{缺失：...}` 并通知 Lead，不要估算后当成实测值。
4. 不得写任何个人身份信息（姓名、学号、昵称、组号、邮箱）；页面属性同样保持匿名。
5. 只改自己 owner 的文件。需要新宏/新宏包 → 通知 Lead，不要改 `preamble.tex`。
6. 中文技术写作：完整段落、先结论后论证；不要用大量项目符号凑篇幅；
   避免"值得注意的是/总而言之/正如我们所知"这类空话；不要出现英文长句直译腔。
7. 终稿不允许出现 `\TODO`（Lead 会 grep）。

## 3. 排版约定

- 中文标点用全角（，。；：、？！），英文术语/代码用半角；中英文之间不手动加空格
  （ctex 自动处理），但数字与单位之间按惯例留窄空格。
- 术语首次出现给英文原文：如"倒排索引（inverted index）"，之后只用中文。
- 数字：千位用窄空格（`866\,626`）；百分比保留 1~2 位小数并写明口径
  （如 df/文档数 = 90.8%）。表格内同类指标小数位对齐。
- 交叉引用：`图~\ref{fig:...}`、`表~\ref{tab:...}`、`第~\ref{sec:...}~章`；
  公式用 `equation` 环境并 `\label{eq:...}`。
- 表格一律 `booktabs`：`\toprule/\midrule/\bottomrule`，无竖线；表题在表上方，
  用 `\caption{...}\label{tab:...}`；表注用 `\par\smallskip\footnotesize 注：...`
  放在表下方。列宽不够用 `tabularx`；数字列右对齐（`r` 或自定义 `R`）。
- 插图：只写 `\input{figures/xxx}`（figure 环境、caption、label 都在图文件里），
  正文只引用 `\ref`。图宽用 `width=\linewidth` 或更小，不要手工缩放文字。
- 代码/命令行：短的行内用 `\code{...}`；多行用
  `\begin{lstlisting}[style=cli] ... \end{lstlisting}`（**代码块内只用 ASCII**，
  中文说明写在外面）。命令行输出保留原始格式（空格、制表符）。
- 段落里的英文缩写首次出现给全称：df（document frequency）、tf（term frequency）。
- 每节开头 1~2 句交代这一节回答什么问题；结尾用 1~2 句收束，不要把结论留到章末才说。

## 4. 章节分工与篇幅目标

| 文件 | 内容 | Owner | 目标篇幅 |
|------|------|-------|----------|
| `sections/01-introduction.tex` | 第 1 章 设计流程：语料收集 | Lead | 4–5 页 |
| `sections/02-design.tex` | 第 1 章 设计流程：统计、索引与查询 | writer-design | 8–10 页 |
| `sections/03-results.tex` | 第 2 章 测试与结果分析 | writer-results | 10–12 页 |
| `sections/03-bonus.tex` | 第 3 章 Bonus 的结果与分析 | writer-analysis | 3–4 页 |
| `sections/04-analysis.tex` | 第 4 章 项目评价 | writer-analysis | 3–4 页 |
| `sections/06-appendix.tex` | 附录 A 复现命令；附录 B 数据格式 | writer-analysis | 2–3 页 |
| `sections/00-abstract.tex` | 摘要 + 关键词 | Lead | 0.7 页 |
| `sections/05-references.tex` | 参考文献 | Lead | 1 页 |
| `figures/*.tex` | 全部插图 | figure-maker | — |

各章用 `\section{...}`，小节用 `\subsection{...}`；不要写 `\chapter`（article 无章）。

### 当前固定大纲

1. **设计流程**：语料收集（来源、预处理与切词、特殊情况处理）；词频统计（设计
   原理、算法设计）；停用词选择（统计学选择、算法设计）；词干化（公开方案选择、
   算法设计）；倒排索引（含持久化）；词典结构；磁盘 B+ 树与块式构建；查询与阈值。
2. **测试与结果分析**：过程性结果、正确性证据、问题分析和复杂度。
3. **Bonus 的结果与分析**：规模边界、B+ 树、块式构建、500k 测量、v2 压缩。
4. **项目评价**：RFC 第二语料推广、局限与改进。
5. **参考文献**。

## 5. 插图登记表（figure-maker 唯一 owner）

| label | 文件 | 内容与数据来源 |
|-------|------|----------------|
| `fig:pipeline` | `figures/pipeline.tex` | 总体数据流（TikZ）：HTML→文本层+manifest→tokenizer→count/stem/stop→词典→posting→索引；查询侧走同一规范化管线；磁盘 B+Tree sidecar 分支。无数据依赖。 |
| `fig:index-layout` | `figures/index-layout.tex` | 索引文件布局（header / doc 表 / term 表 / postings）+ posting 记录结构（TikZ）。无数据依赖。 |
| `fig:zipf` | `figures/zipf.tex` | 词频—排名双对数散点 + 1/rank 参考线，标注 hapax。数据：`reports/data/csv/tasks/wordcount-default.csv`。 |
| `fig:entropy` | `figures/entropy.tex` | 速率熵判别：功能词 vs 内容词 vs 专名的 `entropy_rate_norm` 条形图，0.93 阈值线。数据同上 CSV。 |
| `fig:threshold` | `figures/threshold.tex` | 2×2 面板：max-df / min-df / min-match / top-x 对返回文档数的影响。数据：`reports/data/csv/tasks/threshold_matrix.csv`。 |
| `fig:bptree` | `figures/bptree.tex` | 1×2 面板：文件体积（对数坐标）与每次查询页读 vs 键数；实测点 + 4 亿键外推虚线。数据：`reports/data/csv/verify/bptree-bench.csv`。 |

图内文字（轴标签、图例、注释）用英文，caption 用中文；不出现个人信息。
每张图必须 `\label`，caption 一句话说清"图展示了什么 + 结论"。

## 6. 表格登记表（各章作者按需填，数字必须来自 results 文件）

- 第 1 章：表 `tab:tasks`（任务→实现→验证）、`tab:corpus`（语料规模）、
  `tab:env`（运行环境）、`tab:coverage`（完成情况）。
- 第 2 章：表 `tab:line-types`（文本层行类型）、`tab:tokenizer-cases`、
  `tab:stopword-rule`、`tab:entropy-features`、`tab:stemmer-departures`、
  `tab:index-scale`、`tab:dict-choice`、`tab:index-layout`、`tab:threshold-params`、
  `tab:complexity`。
- 第 3 章：表 `tab:test-suite`、`tab:unit-tests`、`tab:golden`、
  `tab:crosscheck`、`tab:wordcount`、`tab:candidates`、`tab:stopword-sweep`、
  `tab:index-configs`、`tab:dict-compare`、`tab:query-examples`、
  `tab:threshold-rows`、`tab:bugs`、`tab:rfc`。
- 第 4 章：表 `tab:bptree-scale`、`tab:extrapolate`、`tab:500k`、
  `tab:compression`、`tab:limits`。

## 7. 参考文献 key（只能引用这些；需要新增先告知 Lead）

`problem`、`lecture-index`、`lecture-build`、`lecture-compress`、`manning-ir`、
`shakespeare`、`shakespeare-gh`、`porter1980`、`porter-c`、`nltk`、`snowball`、
`rfc1035`、`rfc5321`、`rfc7231`、`zipf1949`、`gfs`、`catch2`。

引用放句末标点前，如"……的倒排记录表~\cite{manning-ir}。"

## 8. 数据契约（results 文件格式）

> 除两位 analyst 的 `results-tasks.md` / `results-verify.md` 外，Lead 另外补充了
> `reports/data/results-preprocess.md`（预处理事实 P1–P6，供 §2.2 引用；日志在
> `reports/data/logs/preprocess/`）。第 2 章预处理相关数字用它，并标注 `% src: P#`。

`reports/data/results-tasks.md`、`reports/data/results-verify.md` 由 analyst 维护，
每项数据写成带编号的条目（F1、F2…），包含：**数值**、**口径/条件**、**来源命令**、
**原始日志路径**。示例：

```markdown
### F10 词频统计（默认配置）
| 指标 | 值 | 命令/来源 |
|---|---|---|
| tokens | 866626 | `mse count --top 0` → logs/tasks/count-default.log |
| distinct | 25705 | 同上 |
```

作者引用时在 `.tex` 里写 `% src: F10` 便于 reviewer 审计。

## 9. 编译与自检

- 全书编译：`bash reports/build.sh`（会覆盖 `reports/report.pdf`）。
- 单人临时编译（不覆盖正式产物）：
  `cd reports && latexmk -xelatex -interaction=nonstopmode -halt-on-error -outdir=build/wip-<name> report.tex`
- 提交前自检清单：无 `\TODO`、无未定义引用（log 里 `undefined references` 为 0）、
  无 `Overfull \hbox` 超过 5pt 的段落、图/表编号连续、数字与 results 文件一致。
- **不要**在 `reports/` 里生成 `*.aux/*.log` 等杂项文件（latexmk 已 `-outdir=build`）。

## 10. 证据边界（重要）

- analyst 只写 `reports/data/**`；figure-maker 只写 `reports/figures/**`；
  writer 只写自己的 section 文件；Lead 维护 `report.tex/preamble.tex/00/01/05`。
- 本轮基线：2026-10-20 已删除旧 `build/` 并重新 `cmake` 配置、编译、`ctest`（18/18 通过）。
  一切证据以本轮重新运行的结果为准；不得引用任何旧构建产物或旧报告内容。

## 11. 章节标签登记表（必须严格使用；Ch1 已引用其中若干）

| label | 章节 | owner |
|-------|------|-------|
| `sec:intro` / `sec:scope` / `sec:corpus` / `sec:architecture` / `sec:coverage` / `sec:env` | 1 及 1.1–1.5 | Lead（已写入） |
| `sec:design` / `sec:preprocess` / `sec:tokenizer` / `sec:count-design` / `sec:entropy` / `sec:stemmer` / `sec:posting` / `sec:dict` / `sec:persistence` / `sec:bptree-design` / `sec:spimi` / `sec:query-design` / `sec:complexity` | 2 及 2.1–2.13 | writer-design |
| `sec:results` / `sec:unit-tests` / `sec:golden` / `sec:crosscheck` / `sec:wordcount` / `sec:stopwords` / `sec:index-build` / `sec:query` / `sec:threshold` / `sec:bugs` / `sec:rfc` | 3 及 3.1–3.11 | writer-results |
| `sec:analysis` / `sec:granularity` / `sec:threshold-semantics` / `sec:dict-analysis` / `sec:preprocess-analysis` / `sec:compression` / `sec:bonus` / `sec:limits` / `sec:conclusion` | 4 及 4.1–4.8 | writer-analysis |
| `app:reproduce` / `app:format` | 附录 A / B | writer-analysis |

注意：Ch1 已引用 `sec:design`、`sec:results`、`sec:analysis`、`sec:wordcount`、
`sec:index-build`、`sec:query`、`sec:unit-tests`、`sec:threshold`、`sec:bonus`，
对应小节必须存在且 label 完全一致。

## 12. 数字归属（避免重复表格）

- 第 1 章：只放语料规模总表 `tab:corpus` 与环境表 `tab:env`。
- 第 2 章：放**设计规格**（结构、语义、不变量、参数、复杂度）与少量"规模事实"；
  完整的结果表（多配置对比、测试统计）归第 3 章，不要在第 2 章重复。
- 第 3 章：所有任务 1–4 的实测表格与图。
- 第 4 章：分析时可引用第 3 章的数字（用 `\ref` 指表/图或直接引用），
  只在引入新实验（Bonus/压缩/500k）时新建表格。
