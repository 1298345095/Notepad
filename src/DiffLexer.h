#pragma once

#include <string_view>

namespace notepad {

// Values match SCE_DIFF_* in the Notepad4 Scintilla fork.
enum class DiffStyle {
    Default = 0,
    Comment = 1,
    Command = 2,
    Header = 3,
    Position = 4,
    Deleted = 5,
    Added = 6,
    Changed = 7,
    PatchAdd = 8,
    PatchDelete = 9,
    RemovedPatchAdd = 10,
    RemovedPatchDelete = 11,
};

// Pass one line, including its CR/LF terminator when present. Classification
// uses the first 15 bytes plus a NUL terminator, matching LexAccessor::GetRange
// with Notepad4's 16-byte diff buffer. BOM removal belongs to file decoding.
[[nodiscard]] DiffStyle ClassifyDiffLine(std::string_view line) noexcept;

inline constexpr int kDiffFoldLevelBase = 0x400;
inline constexpr int kDiffFoldLevelHeaderFlag = 0x2000;
inline constexpr int kDiffFoldLevelNumberMask = 0x0fff;

// Computes the current line's level. When two consecutive returned levels are
// identical headers, the caller must clear the preceding line's header flag,
// as ColouriseDiffDoc does. The previousLevel argument is the unmodified level
// returned for the preceding line, or kDiffFoldLevelBase for the first line.
[[nodiscard]] int DiffFoldLevel(DiffStyle style, char firstChar,
                                int previousLevel) noexcept;

}  // namespace notepad
