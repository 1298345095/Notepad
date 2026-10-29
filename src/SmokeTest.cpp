#include "SmokeTest.h"
#include "MainFrame.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <wx/app.h>
#include <wx/button.h>
#include <wx/dcmemory.h>
#include <wx/dcscreen.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/utils.h>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace notepad {
namespace {
std::string JsonString(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"') out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c == '\t') out << "\\t";
        else if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
        else out << c;
    }
    return out.str() + '"';
}

std::string Utf8(const wxString& value) {
    const auto bytes = value.utf8_str();
    return std::string(bytes.data(), bytes.length());
}

std::string Text(EditorCtrl* editor) {
    const auto bytes = editor->GetTextRaw();
    return std::string(bytes.data(), static_cast<size_t>(editor->GetTextLength()));
}

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void WriteResult(const wxString& output, const std::string& content) {
    const auto path = NativePath(output);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << content;
    stream.close();
}

struct TempFiles {
    std::vector<wxString> paths;
    wxString Make(const wxString& suffix) {
        const wxString allocated = wxFileName::CreateTempFileName("notepad-test-");
        if (allocated.empty()) throw std::runtime_error("Could not create a smoke-test fixture");
        paths.push_back(allocated);
        const wxString named = allocated + suffix;
        paths.push_back(named);
        return named;
    }
    ~TempFiles() {
        for (const auto& path : paths) {
            std::error_code ignored;
            std::filesystem::remove(NativePath(path), ignored);
        }
    }
};

const char* Platform() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

std::string PeakRss() {
#if defined(__linux__) || defined(__APPLE__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
        return std::to_string(usage.ru_maxrss / 1024);
#else
        return std::to_string(usage.ru_maxrss);
#endif
    }
#endif
    return "null";
}

bool SaveScreenshot(MainFrame& frame, const wxString& path) {
#ifdef __WXGTK__
    // Optional evidence in the Linux virtual display. Other platforms need
    // separate screenshot permissions; a screenshot is not an IME test.
    wxTheApp->Yield(true);
    frame.Update();
    const auto rect = frame.GetScreenRect();
    wxBitmap bitmap(rect.GetWidth(), rect.GetHeight());
    wxMemoryDC target(bitmap);
    wxScreenDC source;
    const bool captured = target.Blit(0, 0, rect.GetWidth(), rect.GetHeight(), &source, rect.GetX(), rect.GetY());
    target.SelectObject(wxNullBitmap);
    return captured && bitmap.SaveFile(path, wxBITMAP_TYPE_PNG);
#else
    (void)frame;
    (void)path;
    return false;
#endif
}
}

bool RunSmokeTest(MainFrame& frame, const wxString& output) {
    std::vector<std::string> passed;
    bool success = false;
    bool screenshot = false;
    std::string error;
    TempFiles fixtures;
    auto* editor = frame.Editor();
    try {
        const std::string original = "你好，世界！\r\nemoji: 😀\r\n世界\r\n";
        const wxString input = fixtures.Make(".txt");
        SaveDocument(NativePath(input), original, Encoding::Utf16LE);
        Require(frame.LoadPath(input, false), "LoadPath failed on a UTF-16 fixture");
        Require(frame.CurrentEncoding() == Encoding::Utf16LE && Text(editor) == original, "UTF-16 to Scintilla round trip failed");
        Require(!frame.IsModified(), "Loading a file must leave a clean save point");
        Require(editor->GetEOLMode() == wxSTC_EOL_CRLF, "CRLF insertion mode was not detected");
        passed.emplace_back("UTF-16 LE load, Chinese/emoji, CRLF and clean save point");

        frame.ConfigureSearch(wxString::FromUTF8("世界"), "world");
        Require(frame.FindNext() && editor->GetSelectedText() == wxString::FromUTF8("世界"), "Unicode find failed");
        Require(frame.FindNext(true), "Backward search failed");
        Require(frame.ReplaceAll() == 2, "Unicode replace-all count is incorrect");
        Require(Text(editor) == "你好，world！\r\nemoji: 😀\r\nworld\r\n", "Unicode replace-all result is incorrect");
        Require(frame.IsModified(), "Replacement must mark document modified");
        editor->Undo();
        Require(Text(editor) == original, "Replace-all must be one undo action");
        editor->Redo();
        Require(Text(editor).find("world") != std::string::npos, "Redo failed");
        passed.emplace_back("Unicode forward/backward find, replace-all, grouped undo and redo");

        const wxString saved = fixtures.Make(".txt");
        frame.ChangeEncoding(Encoding::Utf16BE);
        Require(frame.SavePath(saved), "Saving as UTF-16 BE failed");
        const auto reloaded = LoadDocument(NativePath(saved));
        Require(reloaded.encoding == Encoding::Utf16BE && reloaded.text == Text(editor), "Saved UTF-16 BE bytes do not match the document");
        Require(!frame.IsModified(), "Successful saving must clear the modified flag");
        passed.emplace_back("UTF-16 BE save and independent byte round trip");

        const wxString malformed = fixtures.Make(".bin");
        { std::ofstream binary(NativePath(malformed), std::ios::binary); binary.write("bad\0data", 8); }
        const auto beforeBadOpen = Text(editor);
        Require(!frame.LoadPath(malformed, false) && Text(editor) == beforeBadOpen, "Invalid file must not replace the current document");
        passed.emplace_back("Rejected binary input preserves the existing document");

        SaveDocument(NativePath(saved), "external change\n", Encoding::Utf8);
        editor->AppendText("unsaved");
        Require(!frame.SavePath(saved), "An external disk modification must be detected");
        Require(LoadDocument(NativePath(saved)).text == "external change\n", "An external disk modification was overwritten");
        passed.emplace_back("External file changes detected before save");

        int promptAnswer = wxYES;
        int promptCount = 0;
        auto confirmFrame = std::unique_ptr<MainFrame, void(*)(MainFrame*)>(
            new MainFrame(true, [&](const wxString&) { ++promptCount; return promptAnswer; }),
            [](MainFrame* window) { window->Destroy(); });
        const wxString reopen = fixtures.Make(".txt");
        SaveDocument(NativePath(reopen), "original", Encoding::Utf8);
        Require(confirmFrame->LoadPath(reopen, false), "Reopen fixture load failed");
        confirmFrame->Editor()->AppendText(" edited");
        Require(confirmFrame->LoadPath(reopen), "Save-then-reopen failed");
        Require(promptCount == 1 && Text(confirmFrame->Editor()) == "original edited"
            && LoadDocument(NativePath(reopen)).text == "original edited" && !confirmFrame->IsModified(),
            "Reopening after Save must load the newly saved version");
        confirmFrame->Editor()->AppendText(" keep me");
        promptAnswer = wxCANCEL;
        Require(!confirmFrame->LoadPath(input) && Text(confirmFrame->Editor()) == "original edited keep me",
            "Cancelling open must preserve unsaved edits");
        promptAnswer = wxNO;
        Require(confirmFrame->LoadPath(input) && Text(confirmFrame->Editor()) == original,
            "Discard then open must load the requested document");
        Require(LoadDocument(NativePath(reopen)).text == "original edited", "Discard must not write changes");
        confirmFrame.reset();
        passed.emplace_back("Save/discard/cancel prompts and reopening the same modified file");

        Require(frame.NewDocument(), "NewDocument failed");
        Require(!editor->CanUndo() && !frame.IsModified() && frame.CurrentEncoding() == Encoding::Utf8, "New document must reset history and encoding");
        editor->SetTextRaw("alpha alphabet ALPHA\nalpha\n");
        frame.ConfigureSearch("alpha", wxString::FromUTF8("β"), false, true);
        Require(frame.ReplaceAll() == 3, "Whole-word case-insensitive matching failed");
        Require(Text(editor) == "β alphabet β\nβ\n", "Whole-word replacement damaged a partial word");
        editor->Undo();
        frame.ConfigureSearch("ALPHA", "omega", true, true);
        Require(frame.ReplaceAll() == 1 && Text(editor) == "alpha alphabet omega\nalpha\n", "Case-sensitive matching failed");
        passed.emplace_back("New document resets history; whole-word and case-sensitive replacement");

        const auto beforeSearchEdit = Text(editor);
        editor->SetSelection(0, 5);
        frame.ConfigureSearch("needle");
        wxCommandEvent showFind(wxEVT_MENU, wxID_FIND);
        frame.GetEventHandler()->ProcessEvent(showFind);
        auto* findControl = wxDynamicCast(wxWindow::FindWindowByName("Find text", &frame), wxTextCtrl);
        Require(findControl != nullptr, "Find control is missing");
        findControl->ChangeValue("needle");
        findControl->SetFocus();
        wxTheApp->Yield(true);
        wxCommandEvent selectFind(wxEVT_MENU, wxID_SELECTALL);
        frame.GetEventHandler()->ProcessEvent(selectFind);
        long selectedStart = 0, selectedEnd = 0;
        findControl->GetSelection(&selectedStart, &selectedEnd);
        Require(selectedStart == 0 && selectedEnd == 6, "Select All menu must target the focused search field");
        Require(editor->GetSelectionStart() == 0 && editor->GetSelectionEnd() == 5,
            "Search field menu commands must preserve the document selection");
        wxCommandEvent cutFind(wxEVT_MENU, wxID_CUT);
        frame.GetEventHandler()->ProcessEvent(cutFind);
        Require(findControl->GetValue().empty() && Text(editor) == beforeSearchEdit,
            "Cut menu must edit the search field without deleting document text");
        auto* closeSearch = wxDynamicCast(wxWindow::FindWindowByLabel("Close", &frame), wxButton);
        Require(closeSearch != nullptr, "Search close button is missing");
        wxCommandEvent closeFind(wxEVT_BUTTON, closeSearch->GetId());
        closeSearch->GetEventHandler()->ProcessEvent(closeFind);
        passed.emplace_back("Edit menu follows search-field focus without changing the document");

        const wxString diffPath = fixtures.Make(".patch");
        const std::string diff =
            "diff --git a/hello.cpp b/hello.cpp\n"
            "index 182caa1..975ca42 100644\n"
            "--- a/hello.cpp\n"
            "+++ b/hello.cpp\n"
            "@@ -1,7 +1,10 @@\n"
            " #include <iostream>\n"
            " \n"
            " int main() {\n"
            "-    std::cout << \"Hello, Windows!\";\n"
            "+    // 你好，跨平台编辑器 👋\n"
            "+    std::cout << \"Hello, Windows / macOS / Linux!\";\n"
            "+    return 0;\n"
            " }\n"
            "\n"
            "diff --git a/notes.txt b/notes.txt\n"
            "--- a/notes.txt\n"
            "+++ b/notes.txt\n"
            "@@ -1,2 +1,5 @@\n"
            "-A Windows-only starting point.\n"
            "+A native desktop prototype.\n"
            "+Scintilla editing, UTF-8 / UTF-16, find and replace.\n"
            "+Diff classification migrated from Notepad4.\n"
            "+Built with C++20 and wxWidgets.\n"
            " One small step at a time.\n";
        SaveDocument(NativePath(diffPath), diff, Encoding::Utf8Bom);
        Require(frame.LoadPath(diffPath, false), "Diff fixture failed to load");
        Require(editor->ActiveLanguage() == Language::Diff, "Diff file was not detected");
        editor->Colourise(0, -1);
        wxTheApp->Yield(true);
        Require(editor->GetStyleAt(0) == 2, "Diff command style is incorrect");
        Require(editor->GetStyleAt(static_cast<int>(diff.find("--- a/"))) == 3, "Diff header style is incorrect");
        Require(editor->GetStyleAt(static_cast<int>(diff.find("@@"))) == 4, "Diff hunk style is incorrect");
        Require(editor->GetStyleAt(static_cast<int>(diff.find("-    std"))) == 5, "Diff removed-line style is incorrect");
        Require(editor->GetStyleAt(static_cast<int>(diff.find("+    //"))) == 6, "Diff added-line style is incorrect");
        Require((editor->GetFoldLevel(0) & wxSTC_FOLDLEVELHEADERFLAG) != 0, "Diff folding header is missing");
        passed.emplace_back("Real Notepad4 Diff rules integrated into Scintilla styles and fold levels");

        auto verifyIncrementalFolds = [&] {
            std::vector<int> incremental;
            for (int line = 0; line < editor->GetLineCount(); ++line) incremental.push_back(editor->GetFoldLevel(line));
            editor->Colourise(0, -1);
            for (int line = 0; line < editor->GetLineCount(); ++line)
                Require(incremental[static_cast<size_t>(line)] == editor->GetFoldLevel(line), "Incremental Diff folds differ from a full recomputation");
        };
        Require((editor->GetFoldLevel(2) & wxSTC_FOLDLEVELHEADERFLAG) == 0, "Adjacent Diff headers must merge");
        editor->SetTargetStart(editor->PositionFromLine(2));
        editor->SetTargetEnd(editor->PositionFromLine(3));
        editor->ReplaceTarget("--- a/renamed.cpp\n");
        editor->Colourise(editor->PositionFromLine(2), editor->PositionFromLine(3) - 1);
        Require((editor->GetFoldLevel(2) & wxSTC_FOLDLEVELHEADERFLAG) == 0,
            "Renaming an unchanged adjacent header must not restore its hidden fold flag");
        verifyIncrementalFolds();
        editor->SetTargetStart(editor->PositionFromLine(3));
        editor->SetTargetEnd(editor->PositionFromLine(4));
        editor->ReplaceTarget(" context line\n");
        editor->Colourise(editor->PositionFromLine(3), editor->PositionFromLine(4));
        Require((editor->GetFoldLevel(2) & wxSTC_FOLDLEVELHEADERFLAG) != 0, "Editing the adjacent header must restore the prior fold header");
        verifyIncrementalFolds();
        Require(frame.LoadPath(diffPath, false), "Reloading the Diff fixture failed");
        editor->SetTargetStart(editor->PositionFromLine(3));
        editor->SetTargetEnd(editor->PositionFromLine(4));
        editor->ReplaceTarget("");
        editor->Colourise(editor->PositionFromLine(3), editor->PositionFromLine(4));
        verifyIncrementalFolds();
        passed.emplace_back("Diff folds converge after changing or deleting adjacent headers");

        editor->SetLanguage(Language::Cpp);
        editor->SetTextRaw("int main() { return 0; }\n");
        editor->Colourise(0, -1);
        Require(editor->GetStyleAt(0) == wxSTC_C_WORD2, "Built-in C++ type highlighting failed");
        editor->SetDark(true);
        editor->SetZoom(1);
        editor->SetWrapMode(wxSTC_WRAP_WORD);
        Require(editor->IsDark() && editor->GetZoom() == 1 && editor->GetWrapMode() == wxSTC_WRAP_WORD, "Editor view controls failed");
        passed.emplace_back("C++ highlighting, dark editor, zoom and word wrap");

        Require(frame.LoadPath(diffPath, false), "Restoring the final demo document failed");
        editor->SetDark(false);
        editor->SetZoom(1);
        editor->SetWrapMode(wxSTC_WRAP_NONE);
        frame.SetTitle("review.patch - Notepad");
        editor->SetSelection(0, 0);
        editor->SetFocus();
        screenshot = SaveScreenshot(frame, output + ".png");
        success = true;
    } catch (const std::exception& failure) {
        error = failure.what();
        std::cerr << "GUI smoke failed: " << error << '\n';
    }
    std::ostringstream report;
    report << "{\n  \"success\": " << (success ? "true" : "false")
           << ",\n  \"platform\": " << JsonString(Platform())
           << ",\n  \"wx_version\": " << JsonString(wxVERSION_NUM_DOT_STRING)
           << ",\n  \"screenshot\": " << (screenshot ? "true" : "false")
           << ",\n  \"checks\": [";
    for (size_t index = 0; index < passed.size(); ++index) {
        if (index != 0) report << ',';
        report << "\n    " << JsonString(passed[index]);
    }
    report << "\n  ],\n  \"error\": " << JsonString(error) << "\n}\n";
    try { WriteResult(output, report.str()); }
    catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return false; }
    return success;
}

bool RunBenchmark(MainFrame& frame, const wxString& file, const wxString& output) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    if (!frame.LoadPath(file, false)) {
        std::cerr << "Benchmark load failed: " << Utf8(frame.LastError()) << '\n';
        return false;
    }
    const auto loaded = Clock::now();
    wxTheApp->Yield(true);
    frame.Update();
    const auto painted = Clock::now();
    auto* editor = frame.Editor();
    const int bytes = editor->GetTextLength();
    const int lines = editor->GetLineCount();
    const auto editing = Clock::now();
    editor->AppendText("benchmark edit");
    editor->Undo();
    const auto undone = Clock::now();
    if (editor->GetTextLength() != bytes) return false;
    auto milliseconds = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
    std::ostringstream report;
    report << std::fixed << std::setprecision(3)
           << "{\n  \"success\": true,\n  \"platform\": " << JsonString(Platform())
           << ",\n  \"wx_version\": " << JsonString(wxVERSION_NUM_DOT_STRING)
           << ",\n  \"utf8_bytes\": " << bytes << ",\n  \"lines\": " << lines
           << ",\n  \"large_file_mode\": " << (editor->IsLargeFileMode() ? "true" : "false")
           << ",\n  \"language\": " << JsonString(EditorCtrl::LanguageName(editor->ActiveLanguage()))
           << ",\n  \"load_and_style_ms\": " << milliseconds(loaded - start)
           << ",\n  \"load_through_first_paint_ms\": " << milliseconds(painted - start)
           << ",\n  \"append_and_undo_ms\": " << milliseconds(undone - editing)
           << ",\n  \"peak_rss_kib\": " << PeakRss()
           << ",\n  \"notes\": \"Single run in this environment; not cold-start or input-latency measurement. Peak RSS includes GUI initialization and file loading.\"\n}\n";
    try { WriteResult(output, report.str()); }
    catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return false; }
    return true;
}

} // namespace notepad
