# 8-1 自己动手写迷你搜索引擎（Roll Your Own Mini Search Engine）

> 本文为 [problem.md](./problem.md) 的中文翻译。

---

在本项目中，你需要创建一个自己的迷你搜索引擎，它能够对《莎士比亚全集》（The Complete Works of William Shakespeare，http://shakespeare.mit.edu/）进行查询。

你可以从互联网上下载用于处理停用词（stop words）和词干化（stemming）的函数，只要在参考文献列表中注明来源即可。

你的任务是：

- (1) 对莎士比亚语料（Shakespeare set）做一次词频统计（word count），并尝试识别其中的停用词（stop words，也叫**噪声词** noisy words）——你如何、在哪里划定“有趣的”词与“噪声”词之间的界线？
- (2) 在莎士比亚语料上建立带词干化（word stemming）的倒排索引（inverted index）。第 (1) 部分识别出的停用词不得包含在内。
- (3) 在你的倒排文件索引（inverted file index）之上编写查询程序，接受用户指定的单词（或短语），返回包含该词的文档的 ID。
- (4) 运行测试，展示查询的阈值（thresholds）可能会如何影响结果。

## 评分标准（Grading Policy）

**编程（Programming）**：编写词频统计（1 分）、索引生成（5 分）和查询处理（3 分）程序，并附有充分的注释。

**测试（Testing）**：为倒排索引的正确性（2 分）和查询阈值处理（2 分）设计测试。撰写分析与评论（3 分）。

**附加题（Bonus）**：*如果你有 500 000 个文件和 400 000 000 个不同的单词，会怎样？你的程序还能正常工作吗？*（+2 分）

**文档（Documentation）**：第 1 章（1 分）、第 2 章（2 分），最后是一份完整的报告（文档整体风格 1 分）。

**注（Note）**：任何在附加题上回答出色的同学都将获得额外加分。
