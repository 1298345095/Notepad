#pragma once
#include <wx/string.h>

namespace notepad {
class MainFrame;
bool RunSmokeTest(MainFrame& frame, const wxString& output);
bool RunBenchmark(MainFrame& frame, const wxString& file, const wxString& output);
}
