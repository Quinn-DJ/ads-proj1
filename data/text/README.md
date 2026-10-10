# data/text/ 格式说明（由 tools/extract.py 生成，勿手改）

每个文档一个 `.txt`，一行一个单元，`#` 开头是头部注释：

- `[ACT I]` / `[SCENE I. ...]`：标题行（H1–H3），索引时可用开关决定是否保留
- `(Exit Ghost)`：舞台指示（<i>），以及从诗句中拆出的方括号提示（`[Aside]`、`[Sings]`），默认过滤，可开关
- `@BERNARDO`：说话人标签（<b>），默认过滤，可开关
- 其余行：台词/诗行/散文正文，保留原标点

重新生成：`python3 tools/extract.py`（覆盖 `[0-9][0-9][0-9][0-9]_*.txt` 与 README 本身）。
