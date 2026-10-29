#include "MainFrame.h"
#include "SmokeTest.h"

#include <iostream>
#include <wx/app.h>
#include <wx/image.h>
#include <wx/log.h>

namespace notepad {
class NotepadApp final : public wxApp {
public:
    bool OnInit() override {
        SetAppName("Notepad");
        SetVendorName("Notepad");
        wxInitAllImageHandlers();
        bool smoke = false;
        wxString smokeOutput = "smoke-result.json";
        wxString benchmarkFile;
        wxString benchmarkOutput = "benchmark-result.json";
        wxString file;
        bool literal = false;
        for (int index = 1; index < argc; ++index) {
            const wxString arg = argv[index];
            if (!literal && arg == "--") { literal = true; continue; }
            if (!literal && arg == "--smoke-test") smoke = true;
            else if (!literal && (arg == "--smoke-output" || arg == "--benchmark" || arg == "--benchmark-output")) {
                if (++index == argc) { std::cerr << "Missing option value\n"; return false; }
                if (arg == "--smoke-output") smokeOutput = argv[index];
                else if (arg == "--benchmark") benchmarkFile = argv[index];
                else benchmarkOutput = argv[index];
            } else if (!literal && (arg == "--help" || arg == "-h")) {
                std::cout << "Notepad [file]\nNotepad --smoke-test --smoke-output result.json\nNotepad --benchmark file --benchmark-output result.json\n";
                return false;
            } else if ((!literal && arg.StartsWith("-")) || !file.empty()) {
                std::cerr << "Unknown option or multiple input files; see --help\n";
                return false;
            } else file = arg;
        }
        if (smoke && !benchmarkFile.empty()) { std::cerr << "Choose one automation mode\n"; return false; }
        const bool automated = smoke || !benchmarkFile.empty();
        if (automated) wxLog::SetActiveTarget(new wxLogStderr());
        auto* frame = new MainFrame(automated);
        SetTopWindow(frame);
        frame->Show();
        if (automated) {
            CallAfter([this, frame, smoke, smokeOutput, benchmarkFile, benchmarkOutput] {
                exitCode_ = (smoke ? RunSmokeTest(*frame, smokeOutput) : RunBenchmark(*frame, benchmarkFile, benchmarkOutput)) ? 0 : 1;
                frame->Destroy();
            });
        } else if (!file.empty()) frame->LoadPath(file, false);
        return true;
    }
    int OnRun() override { wxApp::OnRun(); return exitCode_; }
private:
    int exitCode_ = 0;
};
} // namespace notepad

wxIMPLEMENT_APP(notepad::NotepadApp);
