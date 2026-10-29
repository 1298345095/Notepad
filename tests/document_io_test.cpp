#include "DocumentIO.h"

#include <array>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace {
namespace fs = std::filesystem;
using notepad::Encoding;
using notepad::Eol;

int assertions = 0;

void Check(bool condition, std::string_view message) {
    ++assertions;
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Operation>
void ExpectFailure(Operation operation, std::string_view message) {
    ++assertions;
    try {
        operation();
    } catch (const std::exception& error) {
        Check(std::string_view(error.what()).size() != 0, "Failure has no explanation");
        return;
    }
    throw std::runtime_error(std::string(message));
}

class ScratchDirectory {
public:
    ScratchDirectory() {
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = fs::current_path() /
                ("document-io-test-" + std::to_string(timestamp) + "-" + std::to_string(attempt));
            if (fs::create_directory(candidate)) {
                path = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("Cannot create test directory");
    }
    ~ScratchDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
    fs::path path;
};

#ifndef _WIN32
// Exercise a real write failure after a temporary file has been created. The
// soft limit applies only to this test process and is restored on every exit.
class SmallFileLimit {
public:
    SmallFileLimit() {
        if (getrlimit(RLIMIT_FSIZE, &original) != 0) {
            throw std::runtime_error("Cannot inspect file-size resource limit");
        }
        oldSignal = std::signal(SIGXFSZ, SIG_IGN);
        if (oldSignal == SIG_ERR) {
            throw std::runtime_error("Cannot configure file-size failure test");
        }
        auto limited = original;
        limited.rlim_cur = 64;
        if (setrlimit(RLIMIT_FSIZE, &limited) != 0) {
            std::signal(SIGXFSZ, oldSignal);
            throw std::runtime_error("Cannot set file-size resource limit");
        }
    }
    ~SmallFileLimit() {
        (void)setrlimit(RLIMIT_FSIZE, &original);
        std::signal(SIGXFSZ, oldSignal);
    }
private:
    struct rlimit original{};
    void (*oldSignal)(int) = SIG_DFL;
};
#endif

std::string Bytes(std::initializer_list<unsigned char> bytes) {
    return std::string(bytes.begin(), bytes.end());
}

void WriteRaw(const fs::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    Check(static_cast<bool>(output), "Cannot write test fixture");
}

std::string ReadRaw(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    Check(static_cast<bool>(input), "Cannot read test fixture");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

constexpr std::array encodings{Encoding::Utf8, Encoding::Utf8Bom,
                                Encoding::Utf16LE, Encoding::Utf16BE};

void TestRoundTrips(const fs::path& directory) {
    const std::string text = "ASCII, \xE4\xB8\xAD\xE6\x96\x87, \xE2\x82\xAC, "
                             "\xF0\x9F\x98\x80, e\xCC\x81, \xF4\x8F\xBF\xBF\r\n"
                             "second\nthird\rfourth";
    const auto path = directory / fs::path(u8"Unicode-\u6587\u4EF6.txt");
    for (const auto encoding : encodings) {
        notepad::SaveDocument(path, text, encoding);
        const auto loaded = notepad::LoadDocument(path);
        Check(loaded.text == text, "Unicode or newline round trip changed text");
        Check(loaded.encoding == encoding, "Wrong encoding after save");
        Check(loaded.eol == Eol::Mixed, "Mixed newlines not detected");
        notepad::SaveDocument(path, "replacement\r\n", encoding);
        Check(notepad::LoadDocument(path).text == "replacement\r\n", "Repeated save failed");
    }

    const std::string scalarText = "A\xE4\xB8\xAD\xF0\x9F\x98\x80\n";
    notepad::SaveDocument(path, scalarText, Encoding::Utf16LE);
    Check(ReadRaw(path) == Bytes({0xFF, 0xFE, 0x41, 0x00, 0x2D, 0x4E,
                                 0x3D, 0xD8, 0x00, 0xDE, 0x0A, 0x00}),
          "UTF-16 LE bytes or surrogate pair are incorrect");
    notepad::SaveDocument(path, scalarText, Encoding::Utf16BE);
    Check(ReadRaw(path) == Bytes({0xFE, 0xFF, 0x00, 0x41, 0x4E, 0x2D,
                                 0xD8, 0x3D, 0xDE, 0x00, 0x00, 0x0A}),
          "UTF-16 BE bytes or surrogate pair are incorrect");
    notepad::SaveDocument(path, scalarText, Encoding::Utf8);
    Check(ReadRaw(path) == scalarText, "UTF-8 unexpectedly changed bytes");
    notepad::SaveDocument(path, scalarText, Encoding::Utf8Bom);
    Check(ReadRaw(path) == Bytes({0xEF, 0xBB, 0xBF}) + scalarText,
          "UTF-8 BOM was not emitted correctly");
}

void TestEmptyAndEol(const fs::path& directory) {
    const auto path = directory / "empty.txt";
    const std::array prefixes{std::string{}, Bytes({0xEF, 0xBB, 0xBF}),
                              Bytes({0xFF, 0xFE}), Bytes({0xFE, 0xFF})};
    for (std::size_t index = 0; index < encodings.size(); ++index) {
        notepad::SaveDocument(path, "", encodings[index]);
        Check(ReadRaw(path) == prefixes[index], "Empty file has incorrect bytes");
        const auto loaded = notepad::LoadDocument(path);
        Check(loaded.text.empty(), "Empty file has text");
        Check(loaded.encoding == encodings[index], "Empty file lost encoding");
        Check(loaded.eol == Eol::None, "Empty file has a newline type");
    }
    Check(notepad::DetectEol("plain") == Eol::None, "No-EOL detection failed");
    Check(notepad::DetectEol("a\nb\n") == Eol::LF, "LF detection failed");
    Check(notepad::DetectEol("a\r\nb\r\n") == Eol::CRLF, "CRLF detection failed");
    Check(notepad::DetectEol("a\rb\r") == Eol::CR, "CR detection failed");
    Check(notepad::DetectEol("a\r\nb\n") == Eol::Mixed, "Mixed CRLF/LF detection failed");
    Check(notepad::DetectEol("\r\r\n") == Eol::Mixed, "Mixed CR/CRLF detection failed");

    for (const std::string text : {"a\nb\n", "a\r\nb\r\n", "a\rb\r", "a\r\nb\nc\r"}) {
        for (const auto encoding : encodings) {
            notepad::SaveDocument(path, text, encoding);
            Check(notepad::LoadDocument(path).text == text, "Line endings were normalized");
        }
    }
}

void TestMalformedInput(const fs::path& directory) {
    const auto path = directory / "invalid.txt";
    const std::vector malformedUtf8{
        Bytes({0x80}), Bytes({0xC0, 0xAF}), Bytes({0xE0, 0x80, 0xAF}),
        Bytes({0xED, 0xA0, 0x80}), Bytes({0xF4, 0x90, 0x80, 0x80}),
        Bytes({0xF0, 0x9F, 0x98}), Bytes({0xF5, 0x80, 0x80, 0x80}),
        Bytes({0xE2, 0x28, 0xA1}), Bytes({0xFF}), Bytes({0x61, 0x00, 0x62})
    };
    for (const auto& bytes : malformedUtf8) {
        WriteRaw(path, bytes);
        ExpectFailure([&] { (void)notepad::LoadDocument(path); }, "Malformed UTF-8 was accepted");
        WriteRaw(path, Bytes({0xEF, 0xBB, 0xBF}) + bytes);
        ExpectFailure([&] { (void)notepad::LoadDocument(path); }, "BOM hid malformed UTF-8");
        for (const auto encoding : encodings) {
            WriteRaw(path, "keep original\n");
            ExpectFailure([&] { notepad::SaveDocument(path, bytes, encoding); },
                          "Saving malformed UTF-8 succeeded");
            Check(ReadRaw(path) == "keep original\n", "Failed validation destroyed original");
        }
    }
    const std::vector malformedUtf16{
        Bytes({0xFF, 0xFE, 0x41}), Bytes({0xFE, 0xFF, 0x00}),
        Bytes({0xFF, 0xFE, 0x00, 0xD8}), Bytes({0xFE, 0xFF, 0xD8, 0x00}),
        Bytes({0xFF, 0xFE, 0x00, 0xDC}), Bytes({0xFE, 0xFF, 0xDC, 0x00}),
        Bytes({0xFF, 0xFE, 0x00, 0xD8, 0x41, 0x00}),
        Bytes({0xFE, 0xFF, 0xD8, 0x00, 0x00, 0x41}),
        Bytes({0xFF, 0xFE, 0x41, 0x00, 0x00, 0x00}),
        Bytes({0xFE, 0xFF, 0x00, 0x41, 0x00, 0x00}),
        Bytes({0xFF, 0xFE, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00}),
        Bytes({0x00, 0x00, 0xFE, 0xFF, 0x00, 0x00, 0x00, 0x41})
    };
    for (const auto& bytes : malformedUtf16) {
        WriteRaw(path, bytes);
        ExpectFailure([&] { (void)notepad::LoadDocument(path); },
                      "Malformed or unsupported UTF-16/UTF-32 was accepted");
    }
}

void TestSaveFailuresAndPermissions(const fs::path& directory) {
    const auto path = directory / "preserved.txt";
    WriteRaw(path, "original");
    ExpectFailure([&] { notepad::SaveDocument(path, "new", static_cast<Encoding>(99)); },
                  "Unknown encoding was accepted");
    Check(ReadRaw(path) == "original", "Unsupported encoding destroyed original");
    ExpectFailure([&] { notepad::SaveDocument({}, "new", Encoding::Utf8); },
                  "Empty path was accepted");
    ExpectFailure([&] { (void)notepad::LoadDocument(directory / "missing.txt"); },
                  "Missing input was accepted");
    ExpectFailure([&] { notepad::SaveDocument(directory / "missing" / "note.txt", "new", Encoding::Utf8); },
                  "Save into nonexistent parent succeeded");
    Check(!fs::exists(directory / "missing"), "Failed save created parent directories");

    fs::permissions(path, fs::perms::owner_read, fs::perm_options::replace);
    ExpectFailure([&] { notepad::SaveDocument(path, "new", Encoding::Utf8); },
                  "Read-only destination was replaced");
    Check(ReadRaw(path) == "original", "Read-only failure destroyed original");
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);

    const auto nonfile = directory / "directory-destination";
    fs::create_directory(nonfile);
    WriteRaw(nonfile / "child", "untouched");
    ExpectFailure([&] { notepad::SaveDocument(nonfile, "new", Encoding::Utf8); },
                  "Directory destination was accepted");
    Check(ReadRaw(nonfile / "child") == "untouched", "Failed save damaged destination directory");
    ExpectFailure([&] { (void)notepad::LoadDocument(nonfile); }, "Directory input was accepted");

#ifndef _WIN32
    {
        SmallFileLimit limit;
        ExpectFailure([&] { notepad::SaveDocument(path, std::string(4096, 'x'), Encoding::Utf8); },
                      "OS file-size limit did not fail the save");
    }
    Check(ReadRaw(path) == "original", "Partial temporary write destroyed original");

    const auto permissions = fs::perms::owner_all | fs::perms::group_read;
    fs::permissions(path, permissions, fs::perm_options::replace);
    notepad::SaveDocument(path, "saved", Encoding::Utf8);
    Check(fs::status(path).permissions() == permissions, "Existing file permissions changed");

    const auto link = directory / "linked.txt";
    fs::create_symlink(path.filename(), link);
    ExpectFailure([&] { notepad::SaveDocument(link, "new", Encoding::Utf8); },
                  "Symbolic link destination was replaced");
    Check(fs::is_symlink(link), "Failed save removed symbolic link");
    Check(ReadRaw(path) == "saved", "Failed symlink save damaged target");
#endif

    for (const auto& entry : fs::directory_iterator(directory)) {
        Check(!entry.path().filename().string().starts_with(".notepad-save-"),
              "Temporary save file leaked");
    }
}

} // namespace

int main() {
    try {
        ScratchDirectory scratch;
        TestRoundTrips(scratch.path);
        TestEmptyAndEol(scratch.path);
        TestMalformedInput(scratch.path);
        TestSaveFailuresAndPermissions(scratch.path);
        std::cout << "DocumentIO: " << assertions << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DocumentIO test failed: " << error.what() << '\n';
        return 1;
    }
}
