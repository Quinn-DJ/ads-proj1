# 项目报告（reports/）

本目录是迷你搜索引擎课程项目的**项目报告**：一份用 LaTeX 排版的中文文档
（文档类 `article`，中文由 `ctex` 宏包支持，XeLaTeX 编译），以及报告用到的
全部插图与实测证据。

## 编译

```bash
bash reports/build.sh        # 产物：reports/report.pdf
```

等价命令：

```bash
cd reports
latexmk -xelatex -interaction=nonstopmode -halt-on-error -outdir=build report.tex
```

依赖：TeX Live（XeLaTeX、ctex、fandol 字体、pgfplots、booktabs 等常规宏包）；
不需要额外安装 Python/字体。中间文件写入 `reports/build/`（已忽略）。

## 目录结构

```text
reports/
├── report.tex              # 主文件：article + ctex，封面/摘要/目录/各章 include
├── preamble.tex            # 导言区：字体、版面、表格、代码、页眉页脚、宏
├── build.sh                # 一键编译，输出 reports/report.pdf
├── report.pdf              # 编译产物（提交物）
├── STYLE.md                # 写作与协作约定（章节分工、图表登记、数据契约）
├── REVIEW.md               # 独立评审报告（编译/数字/视觉/匿名/复现逐项结论）
├── README.md               # 本文件
├── sections/               # 正文章节
│   ├── 00-abstract.tex     #   摘要与关键词
│   ├── 01-introduction.tex #   第 1 章 设计流程：语料收集
│   ├── 02-design.tex       #   第 1 章 设计流程：统计、索引与查询
│   ├── 03-results.tex      #   第 2 章 测试与结果分析
│   ├── 03-bonus.tex        #   第 3 章 Bonus 的结果与分析
│   ├── 04-analysis.tex     #   第 4 章 项目评价
│   ├── 05-references.tex   #   参考文献
│   └── 06-appendix.tex     #   附录 A 复现命令 / 附录 B 数据格式
├── figures/                # 全部插图（矢量：TikZ / pgfplots）
│   ├── pipeline.tex        #   fig:pipeline     系统总体数据流
│   ├── index-layout.tex    #   fig:index-layout 索引文件与 posting 布局
│   ├── zipf.tex            #   fig:zipf         词频—排名分布
│   ├── entropy.tex         #   fig:entropy      速率熵判别
│   ├── threshold.tex       #   fig:threshold    四阈值对返回数的影响
│   ├── bptree.tex          #   fig:bptree       B+Tree 规模实测与外推
│   └── data/               #   pgfplots 数据文件与生成脚本（确定性）
└── data/                   # 本轮重新运行项目得到的实测证据
    ├── results-tasks.md        # 任务 1–3 测量（F1–F42）
    ├── results-verify.md       # 测试/交叉验证/Bonus（F1–F18）
    ├── results-preprocess.md   # 预处理复核（P1–P6）
    ├── logs/tasks|verify|preprocess/   # 原始命令输出
    ├── csv/tasks|verify/               # 报告图表输入 CSV
    └── work/                          # 临时索引/中间产物与复现脚本
```

## 证据与可复现性

报告中的**每一个数字**都来自 2026-10-20 对项目的重新构建与重新运行
（旧的 `build/` 产物已删除、重新 `cmake` 配置编译，`ctest` 18/18 通过），
而不是开发过程中的历史记录。`reports/data/results-*.md` 中每条事实都给出
数值、口径、执行命令与原始日志路径；报告 `.tex` 中用 `% src: F##` 注释标注
数字来源，便于审计。

重建全部证据的最小流程：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
bash reports/build.sh
```

- 词频/索引/查询/阈值实验：见 `reports/data/results-tasks.md` 各条的"命令"；
- 单元测试、交叉验证、B+Tree/50 万文档规模实验：见 `reports/data/results-verify.md`；
- 预处理事实（方括号舞台提示、行标记规则、ASCII 前提）：见 `reports/data/results-preprocess.md`。

## 说明

- 报告按课程要求**匿名**：正文、封面与 PDF 元数据中不含姓名/学号/组号等信息。
- `reports/data/work/` 下的索引文件是测量过程的中间产物，可随时删除；
  保留的脚本与日志足以复现全部读数。
- 报告只依赖本仓库的数据与工具：莎士比亚语料与 RFC 第二语料均已随仓库提交，
  无需联网即可重新生成报告 PDF。

## 质量验收

本轮已按“设计流程、测试与结果分析、Bonus、项目评价、参考文献”重排章节，
并重新编译当前 PDF（19 页、0 error、0 undefined reference、0 Overfull hbox）。
原有的数字一致性审计记录保留在 `reports/REVIEW.md`；重排后的目录、章节编号
与页面以当前 `reports/build/report.toc` 和 `reports/report.pdf` 为准。
