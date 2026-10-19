# 迷你搜索引擎项目：独立测试报告

本目录把测试证据拆成四份独立报告，避免全部堆进主报告。每份报告都是自包含的
LaTeX 文档，并提交编译好的 PDF。

| 报告 | 文件 | 内容 |
|---|---|---|
| TR-01 | `01-unit-tests.pdf` | Catch2 单元测试、CTest 集成测试、46 个 TEST_CASE / 85,155 条断言 |
| TR-02 | `02-correctness.pdf` | 黄金样例、独立 Python 实现、全词表 brute-force、短语对拍、磁盘词典、RFC 第二语料 |
| TR-03 | `03-thresholds.pdf` | 四个查询阈值的 201 条测量、Jaccard、阈值语义分析 |
| TR-04 | `04-scale.pdf` | SPIMI、v2 压缩、磁盘 B+Tree、500k 文件跑批、50M 键规模实测 |

## 构建

```bash
bash reports/build.sh
```

依赖：XeLaTeX / latexmk、ctex + Fandol 中文字体、DejaVu 字体，以及 `style.tex`
中列出的 LaTeX 包。中间文件写入 `reports/build/`（已忽略），PDF 复制到
`reports/pdf/`。

只编译一份：

```bash
cd reports
latexmk -xelatex -interaction=nonstopmode -halt-on-error \
  -outdir=build 03-thresholds.tex
```

## 原始证据

- `data/threshold_matrix.csv`：TR-03 的 201 条原始测量；
- `data/ctest.log`：本分支上 `ctest --test-dir build --output-on-failure` 的完整输出；
- `data/unit-tests.txt`：八个 Catch2 可执行文件的断言数汇总；
- `figures/threshold_matrix.png`、`figures/bptree_scale.png`：报告使用的图。

## 与主报告的关系

这些报告不依赖主报告的 `.tex` 文件，也不修改主报告。主报告可以引用本目录的
结论与数据；如果需要合并排版，直接复制对应章节或表格即可。

测试日期为 2026-10-19；报告版本 1.0。提交前请按课程要求检查匿名信息与文件命名。
