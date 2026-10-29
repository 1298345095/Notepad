# Notepad4 lexer migration / 词法分析迁移记录

## Source and scope / 来源与范围

This prototype ports the **Diff line classifier and current-line fold-level
calculation** from Notepad4's actual `LexDiff.cxx`, pinned to commit
`89b8b36e0ee2c23f73be922315f17261ccfbbe1a`:

- [LexDiff.cxx](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/lexers/LexDiff.cxx)
- [LexAccessor::GetRange](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/lexlib/LexAccessor.cxx#L41)
- [Upstream permission notice](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/License.txt)

移植代码位于 `src/DiffLexer.h` 和 `src/DiffLexer.cpp`，使用纯 C++20，
不依赖 wxWidgets 或 Scintilla 的私有接口。`DiffStyle` 的 0–11 数值保持
原版 `SCE_DIFF_*` 编号，供容器词法分析模式调用。

The original source copyright is retained in `DiffLexer.cpp`; the complete
permission notice is copied to `third_party/scintilla/License.txt`.

## Preserved behavior / 保留的行为

- 保留 unified、context、normal、combined diff，以及 Subversion、Perforce
  和 difflib 前缀的分类顺序，包括 `---` 与 `--`、`+++ ` 与 `++` 的优先级。
- 原版缓冲区大小为 16 字节，但 `GetRange` 实际最多复制 **15 字节 + NUL**。
  本移植保持这个边界：第 16 个数据字节及之后的 `/` 不参与头部判定。
- 传入一整行时应包含已有 CR/LF。`---\n` 是位置标记，文件末尾没有换行的
  `---` 是删除行；空行和制表符起始行也保持原版分类。
- UTF-8 正文字节不改变 ASCII 前缀分类。此纯函数不猜测是否为文件第一行，
  因而保留传入的 BOM 字节；若文件加载器已移除 BOM，则对解码后的正文分类。
- 数字前缀沿用原版 `atoi(...) != 0` 的正常语义（前导空白、可选正负号、
  十进制数字前缀）。这里只计算“是否非零”，避免整数溢出；超出 `int` 范围
  的非零数字也按非零处理，原版 `atoi` 对该输入没有定义结果。

## Folding integration / 折叠集成

`DiffFoldLevel(style, firstChar, previousLevel)` returns the current line's
Scintilla-compatible fold level. Start with `kDiffFoldLevelBase`.

原版还会回修前一行：如果当前层级带 header 标志且与前一行完全相同，清除
前一行的 header 标志。该动作依赖文档，由 UI 调用方完成；传给下一次计算
的 `previousLevel` 仍是本次返回值。增量重算时，应让折叠状态沿后续行传播
到收敛，不能只更新改变的单行。

## Verification / 验证

`tests/diff_lexer_test.cpp` checks concrete diff examples, CR/LF and EOF
differences, the 15-byte truncation boundary, UTF-8/BOM input, embedded NUL,
long lines, numeric prefixes, and fold-level transitions.

Standalone invocation from the repository root:

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I src \
    src/DiffLexer.cpp tests/diff_lexer_test.cpp -o diff_lexer_test
./diff_lexer_test
```

## Remaining work / 后续范围

此切片没有移植 Notepad4 的完整 Scintilla 分支、lexer 插件接口、其他语言
lexer、词表、Win32 布局与并行优化，也没有证明整个 Notepad4 功能或性能
已经等价。Diff 的逐行规则不依赖复杂跨行语法状态；下一批移植需要单独验证
真实的 `ILexer5`／`IDocument` 差异，以及跨行高亮、词表属性和折叠行为。

UI 层负责文档范围、按字节设置样式、增量通知、折叠回修和重新着色。
冷启动、内存、大文件编辑、输入法和三平台行为仍需在实际应用构建中验证。
