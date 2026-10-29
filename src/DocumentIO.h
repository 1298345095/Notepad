#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace notepad {

enum class Encoding { Utf8, Utf8Bom, Utf16LE, Utf16BE };
enum class Eol { LF, CRLF, CR, Mixed, None };

struct LoadedDocument {
    std::string text;
    Encoding encoding;
    Eol eol;
};

// Text is strict UTF-8. Newline sequences are never normalized. UTF-16 requires
// a BOM on load, and both UTF-16 output formats include a BOM. Legacy encodings,
// UTF-32, embedded NULs, and malformed Unicode are rejected with an exception.
// This prototype keeps the entire file in memory and limits both the encoded
// and decoded representations to 256 MiB.
LoadedDocument LoadDocument(const std::filesystem::path& path);

// Validate and encode before touching the destination, write an exclusive
// temporary file in the same directory, flush it, then replace the destination.
// Existing read-only files, symlinks, and nonregular files are rejected. POSIX
// ownership and ordinary rwx permissions are retained; extended attributes,
// POSIX ACLs, and special mode bits are not copied by this prototype. Windows
// replacement uses ReplaceFileW to retain the existing file's security metadata.
// Atomic replacement prevents partial files; it is not a power-loss guarantee.
void SaveDocument(const std::filesystem::path& path, std::string_view utf8,
                  Encoding encoding);

Eol DetectEol(std::string_view text);
const char* EncodingName(Encoding encoding);
const char* EolName(Eol eol);

} // namespace notepad
