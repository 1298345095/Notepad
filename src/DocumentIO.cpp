#include "DocumentIO.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace notepad {
namespace {

constexpr std::size_t kMaxDocumentBytes = 256U * 1024U * 1024U;
constexpr std::string_view kUtf8Bom{"\xEF\xBB\xBF", 3};
constexpr std::string_view kUtf16LEBom{"\xFF\xFE", 2};
constexpr std::string_view kUtf16BEBom{"\xFE\xFF", 2};

void CheckSize(std::size_t size, std::size_t additional = 0) {
    if (size > kMaxDocumentBytes || additional > kMaxDocumentBytes - size) {
        throw std::runtime_error("Document exceeds the prototype's 256 MiB limit");
    }
}

[[noreturn]] void InvalidUnicode(std::string_view encoding, std::size_t offset,
                                 std::string_view reason) {
    throw std::runtime_error("Invalid " + std::string(encoding) + " at byte " +
                             std::to_string(offset) + ": " + std::string(reason));
}

std::uint32_t NextUtf8(std::string_view text, std::size_t& offset) {
    const auto start = offset;
    const auto first = static_cast<unsigned char>(text[offset]);
    std::size_t length = 1;
    std::uint32_t value = first;
    std::uint32_t minimum = 0;

    if (first >= 0xC2 && first <= 0xDF) {
        length = 2;
        value = first & 0x1FU;
        minimum = 0x80;
    } else if (first >= 0xE0 && first <= 0xEF) {
        length = 3;
        value = first & 0x0FU;
        minimum = 0x800;
    } else if (first >= 0xF0 && first <= 0xF4) {
        length = 4;
        value = first & 0x07U;
        minimum = 0x10000;
    } else if (first >= 0x80) {
        InvalidUnicode("UTF-8", start, "invalid leading byte");
    }

    if (length > text.size() - offset) {
        InvalidUnicode("UTF-8", start, "incomplete sequence");
    }
    for (std::size_t index = 1; index < length; ++index) {
        const auto next = static_cast<unsigned char>(text[offset + index]);
        if ((next & 0xC0U) != 0x80U) {
            InvalidUnicode("UTF-8", offset + index, "invalid continuation byte");
        }
        value = (value << 6U) | (next & 0x3FU);
    }
    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) {
        InvalidUnicode("UTF-8", start, "not a Unicode scalar value");
    }
    if (value == 0) {
        InvalidUnicode("UTF-8", start, "NUL bytes are not supported text");
    }
    offset += length;
    return value;
}

void ValidateUtf8(std::string_view text) {
    CheckSize(text.size());
    for (std::size_t offset = 0; offset < text.size();) {
        (void)NextUtf8(text, offset);
    }
}

void AppendUtf8(std::string& text, std::uint32_t value) {
    const std::size_t length = value < 0x80 ? 1 : value < 0x800 ? 2 :
                               value < 0x10000 ? 3 : 4;
    CheckSize(text.size(), length);
    if (length == 1) {
        text.push_back(static_cast<char>(value));
    } else if (length == 2) {
        text.push_back(static_cast<char>(0xC0U | (value >> 6U)));
        text.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else if (length == 3) {
        text.push_back(static_cast<char>(0xE0U | (value >> 12U)));
        text.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    } else {
        text.push_back(static_cast<char>(0xF0U | (value >> 18U)));
        text.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
    }
}

std::string DecodeUtf16(std::string_view bytes, bool littleEndian) {
    if (bytes.size() % 2 != 0) {
        InvalidUnicode("UTF-16", bytes.size() - 1, "odd byte count");
    }
    const auto unitAt = [bytes, littleEndian](std::size_t offset) {
        const auto first = static_cast<unsigned char>(bytes[offset]);
        const auto second = static_cast<unsigned char>(bytes[offset + 1]);
        return static_cast<std::uint32_t>(littleEndian ?
            first | (second << 8U) : (first << 8U) | second);
    };

    std::string result;
    result.reserve(bytes.size());
    for (std::size_t offset = 0; offset < bytes.size();) {
        const auto start = offset;
        auto value = unitAt(offset);
        offset += 2;
        if (value >= 0xD800 && value <= 0xDBFF) {
            if (offset == bytes.size()) {
                InvalidUnicode("UTF-16", start, "unpaired high surrogate");
            }
            const auto low = unitAt(offset);
            if (low < 0xDC00 || low > 0xDFFF) {
                InvalidUnicode("UTF-16", offset, "expected low surrogate");
            }
            value = 0x10000 + ((value - 0xD800) << 10U) + (low - 0xDC00);
            offset += 2;
        } else if (value >= 0xDC00 && value <= 0xDFFF) {
            InvalidUnicode("UTF-16", start, "unpaired low surrogate");
        }
        if (value == 0) {
            InvalidUnicode("UTF-16", start, "NUL characters are not supported text");
        }
        AppendUtf8(result, value);
    }
    return result;
}

std::string EncodeText(std::string_view text, Encoding encoding) {
    CheckSize(text.size());
    std::string bytes;
    switch (encoding) {
    case Encoding::Utf8:
        ValidateUtf8(text);
        return std::string(text);
    case Encoding::Utf8Bom:
        ValidateUtf8(text);
        CheckSize(text.size(), kUtf8Bom.size());
        bytes.assign(kUtf8Bom);
        bytes.append(text);
        return bytes;
    case Encoding::Utf16LE:
        bytes.assign(kUtf16LEBom);
        break;
    case Encoding::Utf16BE:
        bytes.assign(kUtf16BEBom);
        break;
    default:
        throw std::invalid_argument("Unsupported output encoding");
    }

    const bool littleEndian = encoding == Encoding::Utf16LE;
    const auto appendUnit = [&bytes, littleEndian](std::uint32_t unit) {
        CheckSize(bytes.size(), 2);
        const char low = static_cast<char>(unit & 0xFFU);
        const char high = static_cast<char>(unit >> 8U);
        bytes.push_back(littleEndian ? low : high);
        bytes.push_back(littleEndian ? high : low);
    };
    for (std::size_t offset = 0; offset < text.size();) {
        auto value = NextUtf8(text, offset);
        if (value < 0x10000) {
            appendUnit(value);
        } else {
            value -= 0x10000;
            appendUnit(0xD800U + (value >> 10U));
            appendUnit(0xDC00U + (value & 0x3FFU));
        }
    }
    return bytes;
}

std::filesystem::path TemporaryPath(const std::filesystem::path& parent) {
    static std::atomic<unsigned long long> sequence{0};
#ifdef _WIN32
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    return parent / (".notepad-save-" + std::to_string(process) + "-" +
                     std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
}

#ifdef _WIN32

[[noreturn]] void FileError(const char* operation,
                           const std::filesystem::path& path, DWORD error) {
    throw std::filesystem::filesystem_error(operation, path,
        std::error_code(static_cast<int>(error), std::system_category()));
}

class TemporaryFile {
public:
    explicit TemporaryFile(const std::filesystem::path& parent) {
        for (int attempt = 0; attempt < 128; ++attempt) {
            auto candidate = TemporaryPath(parent);
            handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                path = std::move(candidate);
                return;
            }
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
                FileError("Cannot create temporary save file", candidate, error);
            }
        }
        throw std::runtime_error("Cannot allocate a unique temporary save file");
    }
    ~TemporaryFile() {
        if (handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
        if (!path.empty()) {
            DeleteFileW(path.c_str());
        }
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    HANDLE handle{INVALID_HANDLE_VALUE};
    std::filesystem::path path;
};

void WriteAtomically(const std::filesystem::path& destination, std::string_view bytes) {
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    const bool exists = attributes != INVALID_FILE_ATTRIBUTES;
    if (!exists) {
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            FileError("Cannot inspect save destination", destination, error);
        }
    } else if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
                              FILE_ATTRIBUTE_READONLY)) != 0) {
        throw std::runtime_error("Save destination is read-only, a link, or not a regular file");
    }

    const auto parent = destination.has_parent_path() ? destination.parent_path() :
                                                       std::filesystem::path(".");
    TemporaryFile temporary(parent);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, 1024U * 1024U));
        DWORD written = 0;
        if (!WriteFile(temporary.handle, bytes.data() + offset, count, &written, nullptr)) {
            FileError("Cannot write temporary save file", temporary.path, GetLastError());
        }
        if (written == 0) {
            throw std::runtime_error("Writing temporary save file made no progress");
        }
        offset += written;
    }
    if (!FlushFileBuffers(temporary.handle)) {
        FileError("Cannot flush temporary save file", temporary.path, GetLastError());
    }
    const HANDLE handle = temporary.handle;
    temporary.handle = INVALID_HANDLE_VALUE;
    if (!CloseHandle(handle)) {
        FileError("Cannot close temporary save file", temporary.path, GetLastError());
    }

    const BOOL replaced = exists ?
        ReplaceFileW(destination.c_str(), temporary.path.c_str(), nullptr, 0, nullptr, nullptr) :
        MoveFileExW(temporary.path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH);
    if (!replaced) {
        FileError("Cannot replace save destination", destination, GetLastError());
    }
    temporary.path.clear();
}

#else

[[noreturn]] void FileError(const char* operation,
                           const std::filesystem::path& path, int error) {
    throw std::filesystem::filesystem_error(operation, path,
                                             std::error_code(error, std::generic_category()));
}

class TemporaryFile {
public:
    TemporaryFile(const std::filesystem::path& parent, bool replacing) {
        for (int attempt = 0; attempt < 128; ++attempt) {
            auto candidate = TemporaryPath(parent);
            descriptor = open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                              replacing ? 0600 : 0666);
            if (descriptor >= 0) {
                path = std::move(candidate);
                return;
            }
            if (errno != EEXIST) {
                FileError("Cannot create temporary save file", candidate, errno);
            }
        }
        throw std::runtime_error("Cannot allocate a unique temporary save file");
    }
    ~TemporaryFile() {
        if (descriptor >= 0) {
            close(descriptor);
        }
        if (!path.empty()) {
            unlink(path.c_str());
        }
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    int descriptor{-1};
    std::filesystem::path path;
};

void WriteAtomically(const std::filesystem::path& destination, std::string_view bytes) {
    struct stat original{};
    const bool exists = lstat(destination.c_str(), &original) == 0;
    if (!exists) {
        if (errno != ENOENT) {
            FileError("Cannot inspect save destination", destination, errno);
        }
    } else {
        if (!S_ISREG(original.st_mode)) {
            throw std::runtime_error("Save destination is a link or not a regular file");
        }
        if ((original.st_mode & 0222) == 0) {
            throw std::runtime_error("Save destination is read-only");
        }
        if (access(destination.c_str(), W_OK) != 0) {
            FileError("Cannot write save destination", destination, errno);
        }
    }

    const auto parent = destination.has_parent_path() ? destination.parent_path() :
                                                       std::filesystem::path(".");
    TemporaryFile temporary(parent, exists);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = std::min<std::size_t>(bytes.size() - offset, 1024U * 1024U);
        const auto written = write(temporary.descriptor, bytes.data() + offset, count);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            FileError("Cannot write temporary save file", temporary.path, errno);
        }
        if (written == 0) {
            throw std::runtime_error("Writing temporary save file made no progress");
        }
        offset += static_cast<std::size_t>(written);
    }

    if (exists) {
        struct stat created{};
        if (fstat(temporary.descriptor, &created) != 0) {
            FileError("Cannot inspect temporary save file", temporary.path, errno);
        }
        if ((created.st_uid != original.st_uid || created.st_gid != original.st_gid) &&
            fchown(temporary.descriptor, original.st_uid, original.st_gid) != 0) {
            FileError("Cannot preserve file ownership", destination, errno);
        }
        if (fchmod(temporary.descriptor, original.st_mode & 0777) != 0) {
            FileError("Cannot preserve file permissions", destination, errno);
        }
    }
    if (fsync(temporary.descriptor) != 0) {
        FileError("Cannot flush temporary save file", temporary.path, errno);
    }
    const int descriptor = temporary.descriptor;
    temporary.descriptor = -1;
    if (close(descriptor) != 0) {
        FileError("Cannot close temporary save file", temporary.path, errno);
    }
    if (rename(temporary.path.c_str(), destination.c_str()) != 0) {
        FileError("Cannot replace save destination", destination, errno);
    }
    temporary.path.clear();
}

#endif

} // namespace

LoadedDocument LoadDocument(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("Cannot open document: path is not a regular file");
    }
    const auto size = std::filesystem::file_size(path);
    if (size > kMaxDocumentBytes) {
        throw std::runtime_error("Document exceeds the prototype's 256 MiB limit");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open document for reading");
    }
    std::string bytes;
    bytes.reserve(static_cast<std::size_t>(size));
    std::array<char, 64U * 1024U> chunk{};
    while (input) {
        input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto count = static_cast<std::size_t>(input.gcount());
        CheckSize(bytes.size(), count);
        bytes.append(chunk.data(), count);
    }
    if (!input.eof() || input.bad()) {
        throw std::runtime_error("Cannot read document completely");
    }

    std::string_view payload(bytes);
    LoadedDocument document{{}, Encoding::Utf8, Eol::None};
    if (payload.starts_with(std::string_view{"\xFF\xFE\x00\x00", 4}) ||
        payload.starts_with(std::string_view{"\x00\x00\xFE\xFF", 4})) {
        throw std::runtime_error("UTF-32 is not supported by this prototype");
    }
    if (payload.starts_with(kUtf8Bom)) {
        document.encoding = Encoding::Utf8Bom;
        payload.remove_prefix(kUtf8Bom.size());
    } else if (payload.starts_with(kUtf16LEBom)) {
        document.encoding = Encoding::Utf16LE;
        payload.remove_prefix(kUtf16LEBom.size());
    } else if (payload.starts_with(kUtf16BEBom)) {
        document.encoding = Encoding::Utf16BE;
        payload.remove_prefix(kUtf16BEBom.size());
    }
    if (document.encoding == Encoding::Utf16LE || document.encoding == Encoding::Utf16BE) {
        document.text = DecodeUtf16(payload, document.encoding == Encoding::Utf16LE);
    } else {
        ValidateUtf8(payload);
        document.text.assign(payload);
    }
    document.eol = DetectEol(document.text);
    return document;
}

void SaveDocument(const std::filesystem::path& path, std::string_view utf8,
                  Encoding encoding) {
    if (path.empty() || path.filename().empty()) {
        throw std::invalid_argument("A filename is required to save the document");
    }
    const auto bytes = EncodeText(utf8, encoding);
    WriteAtomically(path, bytes);
}

Eol DetectEol(std::string_view text) {
    bool lf = false;
    bool crlf = false;
    bool cr = false;
    for (std::size_t offset = 0; offset < text.size(); ++offset) {
        if (text[offset] == '\r') {
            if (offset + 1 < text.size() && text[offset + 1] == '\n') {
                crlf = true;
                ++offset;
            } else {
                cr = true;
            }
        } else if (text[offset] == '\n') {
            lf = true;
        }
    }
    const int kinds = static_cast<int>(lf) + static_cast<int>(crlf) + static_cast<int>(cr);
    if (kinds > 1) {
        return Eol::Mixed;
    }
    return lf ? Eol::LF : crlf ? Eol::CRLF : cr ? Eol::CR : Eol::None;
}

const char* EncodingName(Encoding encoding) {
    switch (encoding) {
    case Encoding::Utf8: return "UTF-8";
    case Encoding::Utf8Bom: return "UTF-8 BOM";
    case Encoding::Utf16LE: return "UTF-16 LE";
    case Encoding::Utf16BE: return "UTF-16 BE";
    }
    return "Unknown";
}

const char* EolName(Eol eol) {
    switch (eol) {
    case Eol::LF: return "LF";
    case Eol::CRLF: return "CRLF";
    case Eol::CR: return "CR";
    case Eol::Mixed: return "Mixed";
    case Eol::None: return "None";
    }
    return "Unknown";
}

} // namespace notepad
