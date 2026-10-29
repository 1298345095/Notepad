// Scintilla source code edit control: lexer for diff results.
// Copyright 1998-2001 by Neil Hodgson <neilh@scintilla.org>
// Distributed under the permission notice in third_party/scintilla/License.txt.
//
// Adapted from Notepad4 scintilla/lexers/LexDiff.cxx at
// 89b8b36e0ee2c23f73be922315f17261ccfbbe1a. The line classification and
// fold-level rules are retained; Scintilla's Accessor is replaced by string_view.

#include "DiffLexer.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace notepad {
namespace {

constexpr std::size_t kDiffBufferSize = 16;

// LexDiff uses atoi(prefix) != 0. We only need its nonzero predicate, so
// recognize the decimal prefix without overflowing an int. This agrees with
// atoi for all representable values and defines its otherwise-undefined
// overflow case as nonzero. Diff syntax uses ASCII whitespace and digits.
bool HasNonzeroDecimalPrefix(const char* text) noexcept {
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) {
        ++text;
    }
    if (*text == '+' || *text == '-') {
        ++text;
    }
    bool nonzero = false;
    while (*text >= '0' && *text <= '9') {
        nonzero = nonzero || *text != '0';
        ++text;
    }
    return nonzero;
}

}  // namespace

DiffStyle ClassifyDiffLine(std::string_view line) noexcept {
    // LexAccessor::GetRange reserves one of the 16 bytes for its terminator.
    std::array<char, kDiffBufferSize> buffer{};
    const std::size_t length = std::min(line.size(), buffer.size() - 1);
    if (length != 0) {
        std::memcpy(buffer.data(), line.data(), length);
    }
    const std::string_view prefix(buffer.data(), length);

    if (prefix.starts_with("diff ") || prefix.starts_with("Index: ")) {
        return DiffStyle::Command;
    }
    if (prefix.starts_with("---") && buffer[3] != '-') {
        if (buffer[3] == ' ' && HasNonzeroDecimalPrefix(buffer.data() + 4)
            && std::strchr(buffer.data(), '/') == nullptr) {
            return DiffStyle::Position;
        }
        if (buffer[3] == '\r' || buffer[3] == '\n') {
            return DiffStyle::Position;
        }
        if (buffer[3] == ' ') {
            return DiffStyle::Header;
        }
        return DiffStyle::Deleted;
    }
    if (prefix.starts_with("+++ ")) {
        if (HasNonzeroDecimalPrefix(buffer.data() + 4)
            && std::strchr(buffer.data(), '/') == nullptr) {
            return DiffStyle::Position;
        }
        return DiffStyle::Header;
    }
    if (prefix.starts_with("====")) {
        return DiffStyle::Header;
    }
    if (prefix.starts_with("***")) {
        if (buffer[3] == ' ' && HasNonzeroDecimalPrefix(buffer.data() + 4)
            && std::strchr(buffer.data(), '/') == nullptr) {
            return DiffStyle::Position;
        }
        if (buffer[3] == '*') {
            return DiffStyle::Position;
        }
        return DiffStyle::Header;
    }
    if (prefix.starts_with("? ")) {
        return DiffStyle::Header;
    }
    if (buffer[0] == '@' || (buffer[0] >= '0' && buffer[0] <= '9')) {
        return DiffStyle::Position;
    }
    if (prefix.starts_with("++")) {
        return DiffStyle::PatchAdd;
    }
    if (prefix.starts_with("+-")) {
        return DiffStyle::PatchDelete;
    }
    if (prefix.starts_with("-+")) {
        return DiffStyle::RemovedPatchAdd;
    }
    if (prefix.starts_with("--")) {
        return DiffStyle::RemovedPatchDelete;
    }
    if (buffer[0] == '-' || buffer[0] == '<') {
        return DiffStyle::Deleted;
    }
    if (buffer[0] == '+' || buffer[0] == '>') {
        return DiffStyle::Added;
    }
    if (buffer[0] == '!') {
        return DiffStyle::Changed;
    }
    if (buffer[0] != ' ') {
        return DiffStyle::Comment;
    }
    return DiffStyle::Default;
}

int DiffFoldLevel(DiffStyle style, char firstChar, int previousLevel) noexcept {
    if (style == DiffStyle::Command) {
        return kDiffFoldLevelBase | kDiffFoldLevelHeaderFlag;
    }
    if (style == DiffStyle::Header) {
        return (kDiffFoldLevelBase + 1) | kDiffFoldLevelHeaderFlag;
    }
    if (style == DiffStyle::Position && firstChar != '-') {
        return (kDiffFoldLevelBase + 2) | kDiffFoldLevelHeaderFlag;
    }
    if ((previousLevel & kDiffFoldLevelHeaderFlag) != 0) {
        return (previousLevel & kDiffFoldLevelNumberMask) + 1;
    }
    return previousLevel;
}

}  // namespace notepad
