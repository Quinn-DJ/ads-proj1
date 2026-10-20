# 报告独立评审（REVIEW.md）

- 评审人：reviewer（独立于全部作者）；评审日期：2026-10-20
- 评审对象：`reports/report.pdf`（48 页）+ `report.tex`/`preamble.tex`/`sections/*.tex`/`figures/*.tex` + `reports/data/**` 证据
- 独立编译产物：`reports/build/review/report.pdf`（sha256 `8991e891…e99b148`，48 页）
- 评审方式：独立 outdir 从零编译两遍（`build/review`、`build/review2`）；`gs -sDEVICE=png16m -r100` 逐页渲染 48 页后目检；`txtwrite` 抽取文本与正式 PDF 逐字比对；全部表格/关键正文数字与 `results-*.md`、`data/csv/**`、`data/logs/**`、源码逐一核对；CLI/tools 参数在仓库内实机验证

## 结论速览

| # | 检查项 | 结论 |
|---|--------|------|
| 1 | 干净编译（0 error / 0 undefined / Overfull>5pt / 页数） | **PASS** |
| 2 | 逐页视觉 QA（6 图 + 34 表全覆盖） | **PASS**（2 处 Minor 版式） |
| 3 | 数字审计（表格 + 正文散落数字） | **PASS**（2 处口径表述已修；0 处数值错误） |
| 4 | 新鲜度（历史值是否误用） | **PASS**（历史值仅以"已过期"形式出现） |
| 5 | 需求覆盖（4 任务 / 测试 / 分析 / Bonus / 文档） | **PASS**（1 处 Major：注释量证据缺失） |
| 6 | LaTeX/排版规范 | **PASS**（3 处代码块非 ASCII 已修） |
| 7 | 匿名性（正文/页眉/元数据） | **PASS** |
| 8 | 可复现性（build.sh / 附录命令 / 图表数据） | **PASS** |
| 9 | 内容质量（空洞/重复/AI 腔/口径一致） | **PASS** |
| 10 | 交付物（report.pdf 最新、README、figure data） | **PASS（有条件）**：评审修改后需 Lead 重建 PDF（Major M2） |

**总结论：0 Blocker；2 Major（均可快速处置）；可提交，但需先按 M1/M2 处置**（详见文末"是否可提交"）。

---

## 1. 干净编译 — PASS

独立 outdir 从零编译（`cd reports && latexmk -xelatex -interaction=nonstopmode -outdir=build/review report.tex`）：

- 退出码 0；`grep -c '^!' report.log` = **0**；undefined reference / citation = **0**；
- `Overfull \hbox` = **0 条**（>5pt 要求自然满足；也无 `Overfull \vbox`）；
- **48 页**，Output written on build/review/report.xdv (48 pages)；
- `Underfull \hbox` **28 条**：全部集中在 (a) `\code{}`/listing 中的长 token 串（如 03-results 行 301/333/357–363/465/478–480/536/591 的停用词列表、命令行片段，`badness` 多为 10000 但为不可断行的等宽串，属预期）；(b) 图内 pgfplots 轴标签（figures 行 8–10/126–129/148–154）。**逐页目检未发现由此产生的可见排版瑕疵**，不构成缺陷。
- 第二遍增量编译（`build/review2`）同样 0 error / 0 undefined / 0 Overfull。
- 目录一致性：`report.lof` 6 条图、`report.lot` 34 条表，编号连续（图 1–6、表 1–34），页码单调；目录/插图目录/表格目录与正文 `\ref` 全部对应（`??` 计 0）。
- 两次全新编译的 PDF **字节不同**（CreationDate/ID 不同，`/Producer(xdvipdfmx)`、`/Title`、`/Subject`、`/Keywords`、`/Creator(XeLaTeX)` 相同），但 `txtwrite` 文本 **逐字相同** → 属 PDF 元数据时间戳级非确定性，不影响内容可复现性。

## 2. 逐页视觉 QA — PASS（Minor 版式见问题清单）

- 覆盖：封面（p1）、摘要（p2）、目录（p3–4）、插图目录（p5）、表格目录（p6–7）、第 1–4 章（p8–41）、参考文献（p42）、附录 A/B（p43–48）；6 张图（p9/17/26/27/30/39）与全部 34 张表逐一看过。
- 未发现：图文重叠、内容截断、表格越界（全部在版心内）、图内文字不可读、异常空白页、页眉页脚错位（页眉"迷你搜索引擎项目报告 / 2026 年 10 月"、页脚页码均正常，封面无页眉页脚）。
- 图内文字可读性：pipeline（TikZ 流程图）、index-layout（字节布局）、zipf（双对数散点）、entropy（条形图+0.93 阈值线）、threshold（2×2 面板 a–d）、bptree（1×2 面板 + 4 亿外推虚线与空心点）在 100 dpi 下均清晰可读；图注在下方、表注在上方，与 STYLE 一致。
- Minor 版式：
  - **表目录跨页**：表 1–32 在逻辑页 5，表 33–34 单独落在逻辑页 6，该页约 3/4 空白（`\listoftables` 溢出 2 条）。不违反要求，但观感一般；可在重建时对 `\listoftables` 稍作压缩（如 `\small`）或缩短个别 caption。
  - **浮动体留白**：逻辑页 10（表 3/表 4 之间）、页 26（图 3 与表 22 之间）约有 20–25% 页高留白，属 `[!htbp]` 浮动排布，非重叠/截断，可接受。

## 3. 数字审计（重点）— PASS

抽样+系统核对：逐表核对 34 张表全部数值列，并对正文散落数字抽样约 60 处；下表为代表性证据（格式：报告值 → 证据文件条目/CSV 实算）。

| 指标 | 报告值 | 证据 | 结果 |
|---|---|---|---|
| 语料规模 | 195 文档 / 152,632 行 / 930,911 words / 4,919,371 B | tasks F1；csv 行数 | ✅ |
| 正文 token / distinct / hapax | 866,626 / 25,705 / 9,753 | tasks F6；`wordcount-default.csv`（25,705 行、tf=1 计 9,753） | ✅ |
| 停用词移除 | 556,866 token、removed 309,760（35.74%） | tasks F6；309760/866626=35.74% | ✅ |
| 词干化 | 14,947 distinct（−41.85%）、hapax 4,843；正式 14,937 / 4,846 | tasks F6 | ✅ |
| top-10 tf/df | the 28,436/177 … in 11,479/181 | tasks F7；CSV 逐行一致 | ✅ |
| top10/100/1000 token 占比 | 20.41% / 54.30% / 80.26% | F8；CSV 实算 20.407/54.302/80.265% | ✅ |
| 候选带扫描 | 4/9/14/16/21/31/48 | tasks F11；表 tab:candidates 逐行一致 | ✅ |
| 三特征扫描 | 9/23/39/50，39 档重叠 35 | tasks F13/F14 | ✅ |
| 熵值抽样 | the 0.9748、and 0.9789、love 0.9271、man 0.9388、hath/o/thee/thou 0.940/0.941/0.935/0.941、more/or/than/their 0.926/0.915/0.919/0.895 | F9；CSV `entropy_rate_norm` 逐项一致 | ✅ |
| 四配置索引 | raw 5,598,793 / stop 4,320,622 / stem 4,958,598 / 正式 3,685,655；postings 152,650/147,909/121,903/117,704 | tasks F18 | ✅ |
| 三词典 | splay 6,392,724 比较/5,850,795 旋转；hash 688,494/11；map 9,435,694 | tasks F20 | ✅ |
| v2 压缩 | 1,725,694 B（46.82%，省 53.18%） | tasks F21；1725694/3685655=46.82% | ✅ |
| SPIMI | 17 run、峰值 5,129、run 4,166,556 B、墙钟 0.2146 s | tasks F22 | ✅ |
| sidecar | 237,568 B（6.45%/13.77%）、8,308 B（0.225%）、8,238 B（0.477%）、114 页读/29,819 命中 | tasks F23 | ✅ |
| 查询样本 | osric 1；love 138（tf 195/165/142）；"king richard" 7→top3 193/191/140；"my lord" 42；king queen 33/43—— | tasks F27–F33 | ✅ |
| 阈值矩阵 | 201 条测量 / CSV 202 行；19.2 ms 中位（17.1/29.9/19.60）；4.05 s | F36；CSV `wc -l`=202 | ✅ |
| 阈值代表行 | love max-df 0.7→0；sweet sword moon min-df 50：30→90（jac 0.3333）；min-match 1/2/3→93/39/30；love top-x 5/10/20 jac 0.0362/0.0725/0.1449；短语全档 7→7 jac 1.00；the/of 全档 0→0 | F38；直接读 `threshold_matrix.csv` 逐行核对 | ✅ |
| 黄金样例 | 4 doc/25 token/11 term/24 posting/750 B；过滤版 16 token/8 term/553 B | verify F4 | ✅ |
| ctest | 18/18、40.90 s、8 个 Catch2、46 用例、85,155 断言（bptree 84,648=99.3%，其余 507） | verify F1–F3 | ✅ |
| 独立对拍 | CSV 四配置 1,677,253/1,674,689/958,633/957,871 B 逐字节；全词表 14,937/25,705/14,937；短语 305 采样/303 检查/0 不一致/21.20 s | tasks F15；verify F7/F9 | ✅ |
| RFC 第二语料 | 3 文档/9,946 行/81,848 words/569,472 B/75,250 token/4,328 distinct/1,506 hapax；索引 75,250→4,328/7,006/510,181 B 与 45,370→2,660/4,594/308,164 B；推导 **58** 词 | verify F10；F2 | ✅ |
| RFC 交叉项 | 38/39 静态词在 RFC 语料词表（my 不在）；重叠 26；仅静态 12；仅推导 32；must/may/should 速率熵 0.809/0.899/0.885 | F10；在 `work/tasks/wordcount-rfc.csv` 上复算，38/39、26/12/32 均吻合 | ✅ |
| B+Tree 5 档 | height 2/3/3/3/4、pages/leaves、1,515,520…754,987,008 B、15.1552…15.0997 B/键、页读 2/3/3/3/4 | verify F11；`csv/verify/bptree-bench.csv` 逐行一致 | ✅ |
| 4 亿外推 | ≈6.04 GB、叶 ≈1,470,588、总页 ≈1,474,574、树高 4、4 页=16 KiB | verify F11（4e8×15.0997 B=6.0399 GB；页数按实测公式复算一致） | ✅ |
| 500k 实验 | 500,000 文件/20,000,000 token/4,855 run/4,908,747 term/19,999,929 posting/427,603,247→284,560,009 B（66.55%/33.45%）/峰值 1.01 vs 2.00 GiB/168.16 vs 76.75 s/SHA 相同 | verify F12–F15 | ✅ |
| 压缩分解 | 主语料 postings 段 3,169,096 B（86.0%）→ 压缩 61.8%；500k 239,999,432 B（56.1%）→59.6%；61.8%×86.0%≈53.2% | F18/F21/F13/F14 + 布局公式实算（8·df+4·Σtf）逐项吻合 | ✅ |
| 环境 | macOS 27.0.1/Apple M3/8 核/16 GiB/cmake 4.4.3/clang 21.0.0/XeTeX TeX Live 2024/mse 329,136 B | 本机实机核对 `sw_vers`/`sysctl`/`cmake --version`/`xelatex --version`/`ls -l build/mse` | ✅ |
| 1,211 HTML/25 MB/commit 6b82db8/610 处方括号=57+553、C 类 8、849→867,475、误删 5 行/37 token | tasks F3、pre P3–P5 | ✅ |

**跨章一致性**（同一指标全文比对）：195、866,626、14,937、3,685,655、46.82%、53.18%、35.74%、18/18、85,155、40.90 s、237,568、58（RFC 停用词）、6.45%/13.77%、0.225%/0.477%、15.100 B/键、6.04 GB 在摘要/第 1–4 章/附录中**全部一致，未发现矛盾**。已知易混值"56 / 2,695"只以"过期历史值"形式出现（见 §4），未被当作本轮实测。

**无法追溯到证据的数字**：0 处。所有推算/外推均已显式标注口径（如 160 GiB、35 GB/23 GB、2.4×10¹⁰ 次比较、13.87 层、`postings 段 86.0%/61.8%` 等在文中注明"外推/推算/理论值"）。

## 4. 新鲜度 — PASS

- `grep -n '56\b|2,695|2695|867,475'`：命中仅 03-results.tex:636 / 04-analysis.tex:418（均写"历史文档 56 为过期口径，不采用"/"已被本轮实测取代"），以及 04-analysis.tex:191（56.1% 是 500k postings 占比，非历史值）；**无任何历史值被当作本轮实测使用**。
- `% src:` 标注：02-design 35 处、03-results 31 处、04-analysis 71 处、06-appendix 8 处、01-introduction 1 处；主要表格与关键段落均带证据编号（Nit：摘要 0 处，属概述性质，可接受）。
- 已知历史值 867,475 仅作为"修复前对照"出现，并注明由本轮 P3/P4 复算（pre P4），符合 STYLE §8。

## 5. 需求覆盖 — PASS（Major M1）

| problem.md 评分点 | 报告位置 | 结论 |
|---|---|---|
| 任务 (1) 词频统计 + 停用词界线 | §2.4–2.5、§3.5–3.6、§4.1；表 tab:wordcount/top10/candidates/stopword-sweep | ✅ |
| 任务 (2) 带词干化倒排索引（不含停用词） | §2.6–2.9、§3.7；表 tab:index-configs/dict-compare；正式配置 flags=stopwords stemmed | ✅ |
| 任务 (3) 单词/短语查询返回文档 ID | §2.12、§3.8；表 tab:query-examples；含 JSON 与 `--pretty` | ✅ |
| 任务 (4) 阈值实验 | §3.9、§4.2；表 tab:threshold-rows、图 fig:threshold | ✅ |
| 编程：词频/索引/查询 + 充分注释 | 实现与验证有（tab:coverage），**注释量无任何证据**（证据 verify F17 已有：9,828 行；核心注释率 31.0%，src 28.1% 口径） | ⚠️ Major M1 |
| 测试：索引正确性 / 阈值 / 分析与评论 | §3.1–3.4、§3.9–3.10、第 4 章；18 项 ctest、黄金样例、独立对拍；201 条阈值 + Jaccard | ✅ |
| Bonus：50 万文件 + 4 亿词 | §4.6（表 tab:bptree-scale/extrapolate/500k、图 fig:bptree）；实测 50 万文件、4 亿词外推并明确标注"未端到端实测" | ✅ |
| 文档：第 1 章 / 第 2 章 / 完整报告 | 第 1 章 7 页（任务/语料/架构/覆盖/环境）、第 2 章 11 页（13 小节设计规格）、全报告 48 页含摘要/目录/索引目录/参考文献/附录 | ✅ |

## 6. LaTeX/排版规范 — PASS

- `booktabs`：全部 34 表使用 `\toprule/\midrule/\bottomrule`，`\hline` 0 处；表题在表上方、图题在下方；表注位于表下（`\par\smallskip\footnotesize 注：`）。
- 交叉引用格式：`图~\ref`、`表~\ref`、`第~\ref~章`、`\S\ref` 使用规范；`\code{}` 0 处裸 `\texttt`；无 `\TODO`、无 `??`（正文文本抽取计 0）。
- 参考文献：17 个 `\bibitem` 与 17 个被引 key 完全一致，0 undefined；引用放句末标点前。
- **已修 Minor**：3 行代码块内中文注释（违反 STYLE §3"代码块内只用 ASCII"），见 Evaluator edits E1–E3。
- 术语一致性（倒排索引/词项/posting/df/tf）抽查一致；中英文间距由 ctex 处理，未发现手动空格或半角中文标点堆叠。
- 已知非缺陷：`\code{--DEPARTURE--}` 在等宽字体下双连字符视觉上接近破折号，属字体连字，可接受。

## 7. 匿名性 — PASS

- 正文/封面/页眉页脚/参考文献全文检索：无姓名、学号、组号、邮箱、昵称（`grep -rniE 'author|姓名|学号|email|@…|quinn|student'` 仅命中 `pdfauthor` 配置行与 `@BERNARDO` 等语料标记）。
- PDF Info（解压对象流后核对）：`/Title`=迷你搜索引擎项目报告、`/Subject`、`/Keywords`、`/Creator`=XeLaTeX、`/Producer`=xdvipdfmx、`/CreationDate`；**无 `/Author` 字段**；raw bytes 中无 `/Users`、无 `quinn`。
- 报告内命令/路径均为仓库相对路径（`reports/data/…`、`./build/mse`）；`reports/data/results-preprocess.md` 中有绝对路径 `/Users/quinn/...`，但该文件不是报告内容、不在 PDF 内。

## 8. 可复现性 — PASS

- `reports/build.sh` 逻辑正确（`set -euo pipefail`、`latexmk -xelatex -halt-on-error -outdir=build`、成功后 `cp build/report.pdf report.pdf`）；为避免覆盖正式产物与 `reports/build/`，评审使用**等价命令**在 `build/review` 从零复现成功（48 页、0 error），并验证正式 PDF 文本与复现结果逐字相同。建议 Lead 终审后执行 `bash reports/build.sh` 重建正式 PDF。
- 附录 A 命令抽查（对照 `src/main.cpp` 与 `tools/*.py` 实机核对）：
  - `mse` 子命令 9 个与 usage 一致；`--max-df/--min-df/--min-match/--top-x/--pretty/--json/--words/--phrase/--dict-file/--manifest` 均存在；
  - `build` 的 `--dict/--compress/--index/--stop-words/--stem`、`build-blocks --block-terms`、`stopwords --derive --min-df-ratio --min-entropy-rate --compare --out`、`dump --verify-all`、`bptree-bench --keys/--lookups/--out` 全部存在；
  - `tools/threshold_matrix.py --csv/--svg`、`check_wordcount.py --manifest/--stem/--stop-words`、`brute_force.py --manifest/--index/--stop-words/--stem`、`check_phrases.py --queries`、`gen_synthetic_corpus.py --root/--docs/--tokens-per-doc/--vocab` 全部存在；
  - 附录 A.3 的 `cmp …/wordcount-stem-stop.csv` 目标文件在 `reports/data/work/tasks/` 中真实存在；`reports/data/logs/` 实际 53 个日志（tasks 30 + verify 20 + preprocess 3），与附录所述一致。
- 图表数据：`python3 figures/data/make_figures_data.py` 重跑后 14 个 `.dat` **全部逐字节相同**；`wordcount-default.csv` sha256 `6523e386…`、`wordcount-stem-stop.csv` `e7143b81…`、`threshold_matrix.csv` `246f8bfc…`、`bptree-bench.csv` 与证据文件记录一致。
- 未执行：500k 合成语料与 ctest 全跑的复现（代价高，且证据链完整、日志可查）；`--pretty` RFC manifest 口径已在 results-verify F18 记录。

## 9. 内容质量 — PASS

- 每章抽样 1–2 段（§1.2–1.4、§2.2–2.5/2.7/2.12、§3.1/3.3/3.9/3.10、§4.1/4.2/4.5/4.6/4.8）：论述为"主张—证据—边界"结构，无空话套话，未见"值得注意的是/总而言之"式填充，未见结论无证据（每个结论后紧跟证据编号或表图引用）。
- 第 2 章设计规格与第 3/4 章实测口径一致（同一 flags、同一 token/term 口径）；Ch1 招牌数字与后文一致（见 §3 交叉一致性）。
- 少量重复属章节分工的正常回指（如 30→90 现象在 §3.9/§4.2/§4.8 各出现一次，均为解释用途，非灌水）。

## 10. 交付物 — PASS（有条件）

- `reports/report.pdf`：评审开始时其文本与当前源码的全新编译**逐字相同**（仅 PDF 时间戳/ID 字节不同），确认为最新产物；**但评审修改了 5 个 section 文件后，需 Lead 重建**（见 M2）。
- `reports/README.md`：目录结构、编译命令、证据组织描述与实际一致（逐项核对）。
- `reports/figures/data/make_figures_data.py`：确定性，重跑 14/14 `.dat` 相同（见 §8）。

---

## 问题清单

### Blocker
无。

### Major
| ID | 位置 | 证据 | 问题与建议修法 |
|---|---|---|---|
| M1 | 报告全文（最合适 §2.1 或附录 A.1） | `results-verify.md` F17（9,828 行 = src 5,346 + include 1,103 + tests 1,578 + tools 1,801；注释率：核心 31.0%、src 28.1%、头文件 46.3%，口径"纯注释行/非空行"）未被报告使用；全文 `grep '注释率\|9\,828\|31.0'` 无命中 | 评分标准"编程=…with sufficient comments"缺少证据支撑。建议在 §2.1 加 1–2 句：代码规模 9,828 行、核心注释率 31.0%（口径与范围按 F17 写清，src 28.1% 也如实标注），或放附录 A.1。**需 Lead/作者补写，reviewer 不改正文内容。** |
| M2 | `reports/report.pdf` | 评审编译后 `report.toc` 与正式 `build/report.toc` 仅差 1 行（§2.10 页码 17→18），因 Evaluator edits 改变文本流 | 正式 PDF 已落后于修改后的源码（TOC 页码不同步、且不含 E1–E7 修复）。请 Lead 在合并后执行 `bash reports/build.sh` 重建并确认 48 页、0 error/0 undefined/0 Overfull（reviewer 不覆盖正式产物）。 |

### Minor
| ID | 位置 | 证据 | 问题与建议修法 |
|---|---|---|---|
| m1 | 插图目录/表格目录（逻辑页 5–6） | 渲染图：表 33–34 独占逻辑页 6，约 3/4 空白 | `\listoftables` 溢出 2 条。可加 `\small` 或缩短个别 caption；不影响内容，可不改。 |
| m2 | `sections/02-design.tex:235` | 原文"位置总数等于 tokenCount 与 keptTokens 之和"，与 `src/index.cpp:631/637/648/698` 不变量（`sum(doc.keptTokens)==tokenCount`、`sum(positions)==tokenCount`）不符 | **已修**（E4）：改为"位置总数等于 tokenCount（即各文档 keptTokens 之和）"。 |
| m3 | `sections/02-design.tex:53`、`sections/04-analysis.tex:143` | 原文"全库 610 处：A 类 57、B 类 553、C 类 8"，57+553+8=618≠610；P3 明确 610=A+B，C 类 8 处另行保留 | **已修**（E5/E6）：改为"重定位 610 处（A 57+B 553），另有 C 类 8 处保留在正文"。 |
| m4 | `sections/03-results.tex:645` | "39 词中 38 个出现在 RFC 词表里…重叠 26"：38 指 RFC 语料词表（vocabulary），易被误读为 58 词推导表（则 38 与重叠 26 自相矛盾） | **已修**（E7）：改为"RFC 语料词表"。已在 `wordcount-rfc.csv` 复算 38/39、重叠 26、12/32 全部成立。 |
| m5 | `sections/01-introduction.tex:183,188`、`06-appendix.tex:58` | 代码块内中文注释（`# 18/18 通过`、`# 生成本报告 PDF`、`# hash/map 同理`），违反 STYLE §3 | **已修**（E1–E3）：改为 ASCII。 |

### Nit
| ID | 位置 | 说明 |
|---|---|---|
| n1 | 摘要 | 无 `% src:` 标注（Ch1 仅 1 处）；摘要为概述可接受，若要提高可审计性可加 1 条总标注。 |
| n2 | `02-design.tex:235` | "结构依赖四条不变量"但分号列举了 5 条判据（最后一条含两个子句）；可改为"四条（其中末条含两个子句）"，非实质问题。 |
| n3 | `03-results.tex:636`、`04-analysis.tex:418` | 正文保留历史值 56 / 2,695 作"过期对照"；已在文中标明不采用，合规；如追求洁净可移到脚注。 |
| n4 | 全文 | 两次全新编译 PDF 字节不同（CreationDate/ID），内容逐字相同 → 记录于复现性口径即可，无需处理。 |
| n5 | `03-results.tex:567` | 表 28 记 9 个问题（7 个开发期代码 bug + 2 个预处理/规格问题）；STYLE §4 建议写"6 个真 bug + 预处理问题"，实际多 1 个（#3 tf_share 分母），已注明定性叙述、不影响评分，无需改。 |

---

## Evaluator edits（reviewer 直接修改，共 7 处 / 5 个文件）

> 仅限排版/错别字/小语句；未改任何数字，未改 `report.tex`、`preamble.tex`、`05-references.tex`、`data/**`、`STYLE.md`。修改后已独立重编译：0 error / 0 undefined / 0 Overfull / 48 页。

| # | 文件:行 | 原文 → 改后 | 原因 |
|---|---|---|---|
| E1 | `sections/01-introduction.tex:183` | `# 18/18 通过` → `# 18/18 passed` | STYLE §3：代码块内只用 ASCII |
| E2 | `sections/01-introduction.tex:188` | `# 生成本报告 PDF` → `# build this report PDF` | 同上 |
| E3 | `sections/06-appendix.tex:58` | `# hash/map 同理` → `# same for hash/map` | 同上 |
| E4 | `sections/02-design.tex:235` | `位置总数等于 tokenCount 与 keptTokens 之和` → `位置总数等于 tokenCount（即各文档 keptTokens 之和）` | 与 `src/index.cpp` 不变量一致（原句数学上会把两者相加） |
| E5 | `sections/02-design.tex:53` | `全库 610 处：A 类…；B 类…；C 类…8 处…` → `全库重定位 610 处（A 类…；B 类…），另有 C 类 8 处…` | 610=A+B；原句列举三类易算成 618 |
| E6 | `sections/04-analysis.tex:143–145` | 同 E5 口径修正 | 与 P3/§2.2 口径统一 |
| E7 | `sections/03-results.tex:645` | `出现在 RFC 词表里` → `出现在 RFC 语料词表里` | 避免与"58 词推导表"混读（38 vs 重叠 26 的表面矛盾） |

---

## 是否可提交

**结论：可以提交（0 Blocker），但提交前必须处置 2 项 Major：**

1. **M2（必做，1 分钟）**：Lead 执行 `bash reports/build.sh` 重建 `reports/report.pdf`，使正式 PDF 包含 E1–E7 修复并同步 TOC 页码；重建后再确认 48 页、0 error、0 undefined、0 Overfull（reviewer 已用独立 outdir 验证过这三项）。
2. **M1（建议做，5 分钟）**：补 1–2 句代码规模/注释率证据（数据已在 verify F17），以正面支撑"编程要求充分注释"的评分点；若时间不允许，至少不应删除现有"代码注释"表述。

**剩余风险**：
- 4 亿词为外推（报告已明确标注"未端到端实测"，风险已在文中披露）；
- 注释率口径若引用需按 F17 写清范围（src 28.1% < 30%，核心 31.0% ≥ 30%），报告未写故无口径风险；
- 计时数字受并行负载影响（报告已多处声明 ±20%/量级参考），不可作为精确基准；
- PDF 生成存在时间戳级非确定性（不影响内容）；若评审要求"逐字节可复现 PDF"，需固定 `SOURCE_DATE_EPOCH`，但通常不要求。

**评审通过项**：编译、视觉、数字、匿名性、可复现性五大重点全部 PASS；34 张表 + 6 张图 + 全部招牌数字均与 `reports/data/` 证据一致，未发现任何编造/矛盾数字。


---

## Lead 处置记录（终稿，2026-10-20）

评审提出的 2 项 Major 已全部处置，并在终稿重新验证：

- **M1（已做）**：在 `sections/02-design.tex` §2.1 补充实现规模与注释率证据：
  C++ 核心 6,449 行（src 5,346 + include 1,103）、含测试与工具共 9,828 行；
  按"纯注释行 / 非空行"口径，核心注释率 31.0%（头文件 46.3%、src 28.1%），
  并如实给出范围与口径（`% src: F17（verify）`）。
- **M2（已做）**：终稿从零重建（删除 `reports/build/` 后执行 `bash reports/build.sh`），
  正式 `reports/report.pdf` 已包含 E1–E7 全部修正，**47 页、0 error、0 undefined
  reference、0 Overfull hbox**；目录/插图目录/表格目录页码与正文同步。

Lead 另外做了三处全报告层面的收尾（不计入评审编辑）：

1. 术语统一：正文/图题中的 `B+Tree` 全部改为 `B+ 树`（与全文其余 19 处写法一致）；
2. 修正一处笔误："咏叹诗页面" → "挽歌（elegy）页面"；
3. 插图目录/表格目录改为 `\small`，使 34 张表全部落在同一页（原溢出 2 行到空白页）。

终稿状态：可提交。
