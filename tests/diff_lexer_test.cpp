#include "DiffLexer.h"

#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;
int checks = 0;

void ExpectStyle(std::string_view name, std::string_view line,
                 notepad::DiffStyle expected) {
    ++checks;
    const auto actual = notepad::ClassifyDiffLine(line);
    if (actual != expected) {
        ++failures;
        std::cerr << name << ": expected style " << static_cast<int>(expected)
                  << ", got " << static_cast<int>(actual) << '\n';
    }
}

void ExpectLevel(std::string_view name, notepad::DiffStyle style, char first,
                 int previous, int expected) {
    ++checks;
    const int actual = notepad::DiffFoldLevel(style, first, previous);
    if (actual != expected) {
        ++failures;
        std::cerr << name << ": expected fold level " << expected
                  << ", got " << actual << '\n';
    }
}

}  // namespace

int main() {
    using notepad::DiffStyle;
    struct Case {
        std::string_view name;
        std::string_view line;
        DiffStyle expected;
    };
    const Case cases[] = {
        {"git command", "diff --git a/one b/one\n", DiffStyle::Command},
        {"plain diff command", "diff old new\r\n", DiffStyle::Command},
        {"subversion command", "Index: source.cpp\n", DiffStyle::Command},
        {"command requires space", "diff\n", DiffStyle::Comment},
        {"index requires space", "Index:\tfile\n", DiffStyle::Comment},
        {"unified old header", "--- a/file.cpp\n", DiffStyle::Header},
        {"unified new header", "+++ b/file.cpp\n", DiffStyle::Header},
        {"unified hunk", "@@ -1,3 +1,4 @@\n", DiffStyle::Position},
        {"combined hunk", "@@@ -1,3 -1,3 +1,4 @@@\n", DiffStyle::Position},
        {"context old header", "*** a/file.cpp\n", DiffStyle::Header},
        {"context old range", "*** 1,3 ****\n", DiffStyle::Position},
        {"context new range", "--- 1,4 ----\n", DiffStyle::Position},
        {"context separator", "***************\n", DiffStyle::Position},
        {"context changed", "! changed\n", DiffStyle::Changed},
        {"p4 header", "==== //depot/file#1\n", DiffStyle::Header},
        {"difflib hint", "?   ^\n", DiffStyle::Header},
        {"normal diff range", "12c12\n", DiffStyle::Position},
        {"zero starts range", "0a1\n", DiffStyle::Position},
        {"normal diff removed", "< old line\n", DiffStyle::Deleted},
        {"normal diff added", "> new line\n", DiffStyle::Added},
        {"removed line", "-old line\n", DiffStyle::Deleted},
        {"added line", "+new line\n", DiffStyle::Added},
        {"context line", " unchanged\n", DiffStyle::Default},
        {"combined patch addition", "++added in both\n", DiffStyle::PatchAdd},
        {"combined patch deletion", "+-different parents\n", DiffStyle::PatchDelete},
        {"removed patch addition", "-+different parents\n", DiffStyle::RemovedPatchAdd},
        {"removed patch deletion", "--removed in both\n", DiffStyle::RemovedPatchDelete},
        {"four dashes retain combined style", "----\n", DiffStyle::RemovedPatchDelete},
        {"three dashes with LF", "---\n", DiffStyle::Position},
        {"three dashes with CRLF", "---\r\n", DiffStyle::Position},
        {"three dashes with CR", "---\r", DiffStyle::Position},
        {"three dashes at EOF", "---", DiffStyle::Deleted},
        {"three dashes with text", "---text\n", DiffStyle::Deleted},
        {"three pluses with text", "+++text\n", DiffStyle::PatchAdd},
        {"three pluses at EOF", "+++", DiffStyle::PatchAdd},
        {"three stars at EOF", "***", DiffStyle::Header},
        {"two stars at EOF", "**", DiffStyle::Comment},
        {"numeric old header without slash", "--- 12file\n", DiffStyle::Position},
        {"numeric new header without slash", "+++ 12file\n", DiffStyle::Position},
        {"numeric context header without slash", "*** 12file\n", DiffStyle::Position},
        {"numeric header with slash", "--- 12/file\n", DiffStyle::Header},
        {"zero does not imply range", "--- 0\n", DiffStyle::Header},
        {"zero plus range", "+++ 000\n", DiffStyle::Header},
        {"zero context range", "*** 0\n", DiffStyle::Header},
        {"signed range", "--- -12\n", DiffStyle::Position},
        {"positive signed range", "+++ +12\n", DiffStyle::Position},
        {"whitespace before number", "*** \t12\n", DiffStyle::Position},
        {"sign without number", "--- -file\n", DiffStyle::Header},
        {"ordinary comment", "Only in a: file\n", DiffStyle::Comment},
        {"binary diff comment", "Binary files differ\n", DiffStyle::Comment},
        {"missing newline marker", "\\ No newline at end of file\n", DiffStyle::Comment},
        {"empty final line", "", DiffStyle::Comment},
        {"empty LF line", "\n", DiffStyle::Comment},
        {"empty CRLF line", "\r\n", DiffStyle::Comment},
        {"space only", " ", DiffStyle::Default},
        {"tab only", "\t", DiffStyle::Comment},
        {"single plus", "+", DiffStyle::Added},
        {"single minus", "-", DiffStyle::Deleted},
        {"single question mark", "?", DiffStyle::Comment},
        {"unicode added text", "+\xe4\xb8\xad\xe6\x96\x87\n", DiffStyle::Added},
        {"unicode header", "--- a/\xe4\xb8\xad\xe6\x96\x87\n", DiffStyle::Header},
        {"raw BOM remains part of line", "\xef\xbb\xbf" "diff a b\n", DiffStyle::Comment},
    };
    for (const auto& test : cases) {
        ExpectStyle(test.name, test.line, test.expected);
    }

    // A slash at byte offset 14 is visible; a slash at offset 15 is truncated.
    // The numeric prefix stays within int range in both cases.
    ExpectStyle("slash in last copied byte", "--- 1" + std::string(9, 'x') + "/path\n",
                DiffStyle::Header);
    ExpectStyle("slash just outside copied prefix", "--- 1" + std::string(10, 'x') + "/path\n",
                DiffStyle::Position);
    ExpectStyle("long body does not alter prefix", "++" + std::string(100000, 'x'),
                DiffStyle::PatchAdd);
    ExpectStyle("large numeric prefix is deterministically nonzero", "--- 99999999999",
                DiffStyle::Position);

    const std::string bomDocument = "\xef\xbb\xbf" "diff a b\n";
    ExpectStyle("decoded first line after BOM removal", std::string_view(bomDocument).substr(3),
                DiffStyle::Command);
    constexpr char withNul[] = "--- 7\0/path\n";
    ExpectStyle("slash beyond embedded NUL follows C-string semantics",
                std::string_view(withNul, sizeof(withNul) - 1), DiffStyle::Position);
    const std::string storage = "diff extra bytes outside view";
    ExpectStyle("input view need not be NUL terminated", std::string_view(storage.data(), 5),
                DiffStyle::Command);

    constexpr int base = notepad::kDiffFoldLevelBase;
    constexpr int header = notepad::kDiffFoldLevelHeaderFlag;
    ExpectLevel("command fold", DiffStyle::Command, 'd', base + 3, base | header);
    ExpectLevel("file header fold", DiffStyle::Header, '-', base | header,
                (base + 1) | header);
    ExpectLevel("hunk fold", DiffStyle::Position, '@', base + 1, (base + 2) | header);
    ExpectLevel("context old range fold", DiffStyle::Position, '*', base + 1,
                (base + 2) | header);
    ExpectLevel("context new range retains body level", DiffStyle::Position, '-',
                base + 3, base + 3);
    ExpectLevel("line after hunk header", DiffStyle::Added, '+', (base + 2) | header,
                base + 3);
    ExpectLevel("comment retains nesting", DiffStyle::Comment, 'O', base + 3, base + 3);
    ExpectLevel("repeated file header signals caller repair", DiffStyle::Header, '+',
                (base + 1) | header, (base + 1) | header);
    ExpectLevel("initial comment", DiffStyle::Comment, 'O', base, base);

    if (failures != 0) {
        std::cerr << failures << " of " << checks << " checks failed\n";
        return 1;
    }
    std::cout << "All " << checks << " diff lexer checks passed\n";
    return 0;
}
