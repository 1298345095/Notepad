#pragma once

#include <wx/stc/stc.h>

namespace notepad {

enum class Language { Automatic, PlainText, Cpp, Python, JavaScript, Json, Html, Css, Markdown, Diff };

// One Scintilla document. UI and platform code stay out of the portable core.
class EditorCtrl final : public wxStyledTextCtrl {
public:
    explicit EditorCtrl(wxWindow* parent);
    void SetLanguage(Language language, const wxString& filename = {});
    void SetDark(bool dark);
    void SetLargeFileMode(bool enabled);
    void RefreshLineNumberWidth();
    Language ActiveLanguage() const { return activeLanguage_; }
    Language SelectedLanguage() const { return selectedLanguage_; }
    bool IsDark() const { return dark_; }
    bool IsLargeFileMode() const { return largeFile_; }
    static const char* LanguageName(Language language);

private:
    void ApplyStyles();
    void StyleDiff(int endPosition);
    void OnStyleNeeded(wxStyledTextEvent& event);
    void OnMarginClick(wxStyledTextEvent& event);
    Language selectedLanguage_ = Language::Automatic;
    Language activeLanguage_ = Language::PlainText;
    wxString filename_;
    bool dark_ = false;
    bool largeFile_ = false;
    int marginDigits_ = 0;
};

} // namespace notepad
