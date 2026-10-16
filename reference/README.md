# reference/ — 词干化第三方参考资料

> 用途：Porter / Porter2（Snowball English）词干化的规则定义、参考实现与对拍基准。
> **这些不是本项目的源码**，仅供实现参考与交叉验证；报告 References 按下方格式引用。
> 下载日期：2026-10-16（全部经 curl 直接抓取，未改动内容）。

## 1. Porter（1980）论文与官方页面

| 文件 | 来源 URL | 说明 |
|------|----------|------|
| `porter-1980-paper.txt` | https://tartarus.org/martin/PorterStemmer/def.txt | 论文全文 *An algorithm for suffix stripping*（Program 14(3), 1980）的官方纯文本版；作者声明"留给实现者抄规则用" |
| `porter-site-index.html` | https://tartarus.org/martin/PorterStemmer/ | 作者官方页面（各语言实现索引）。页内许可声明："The software is completely free for any purpose, unless notes at the head of the program text indicates otherwise." |

## 2. 作者亲笔 C 实现（canonical）

| 文件 | 来源 URL | 说明 |
|------|----------|------|
| `porter-canonical-c.txt` | https://tartarus.org/martin/PorterStemmer/c.txt | ANSI C；作者自述 "canonical"，含 `--DEPARTURE--` 标注的改进点（与论文略有出入） |

## 3. 对拍基准（Python，独立实现）

| 文件 | 来源 URL | 说明 |
|------|----------|------|
| `nltk-porter-3.9.1.py` | https://raw.githubusercontent.com/nltk/nltk/3.9.1/nltk/stem/porter.py | NLTK `PorterStemmer`（三种模式）；**我们的对拍 oracle 用 `MARTIN_EXTENSIONS` 模式**（= 作者 canonical C 行为）；NLTK 项目 Apache-2.0 |
| `nltk-snowball-develop.py` | https://raw.githubusercontent.com/nltk/nltk/develop/nltk/stem/snowball.py | NLTK Snowball 各语言实现，`EnglishStemmer` = Porter2；Apache-2.0。**抓自 develop 分支（未定 tag），日期 2026-10-16** |

## 4. Snowball English（Porter2）

| 文件 | 来源 URL | 说明 |
|------|----------|------|
| `snowball-english-algorithm.html` | https://snowballstem.org/algorithms/english/stemmer.html | Porter2 官方算法说明 |
| `libstemmer_c.tgz` | https://snowballstem.org/dist/libstemmer_c.tgz | Snowball 官方 C 发行包（全部语言的生成代码，含 COPYING） |
| `snowball-english-c/` | 上者解包所得 | 仅抽出 English 部分：`COPYING`（BSD-3-Clause，© 2001 Martin Porter、© 2004–2005 Richard Boulton）+ `src_c/stem_{ISO_8859_1,UTF_8}_english.{c,h}` |

## 参考引用格式（报告 References 备用）

- M. F. Porter, "An algorithm for suffix stripping", *Program*, vol. 14, no. 3, pp. 130–137, 1980.
- M. F. Porter, "The Porter Stemming Algorithm"（reference C implementation）,
  https://tartarus.org/martin/PorterStemmer/, accessed 2026-10-16.
- NLTK Project, `nltk.stem.PorterStemmer`, https://www.nltk.org/, accessed 2026-10-16
  (Apache License 2.0).
- Snowball, "The English (Porter2) stemming algorithm",
  https://snowballstem.org/algorithms/english/stemmer.html, accessed 2026-10-16
  (BSD-3-Clause).

## 使用约定

- 本项目**不直接运行**这些代码：`src/stemmer.cpp` 将按论文规则自行实现；这些文件用于
  （a）抄录规则、（b）作对拍基准（尤其 NLTK）。
- 若课程提交的 zip 包含本目录，须保留本 README 与许可文件（`snowball-english-c/COPYING`）；
  也可以只在报告中引用、不打包本目录（以最终打包说明为准）。
