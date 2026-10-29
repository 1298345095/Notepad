#pragma once

#include "DocumentIO.h"
#include "EditorCtrl.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <wx/checkbox.h>
#include <wx/frame.h>
#include <wx/panel.h>
#include <wx/textctrl.h>

namespace notepad {

std::filesystem::path NativePath(const wxString& path);

class MainFrame final : public wxFrame {
public:
    using SavePrompt = std::function<int(const wxString&)>;
    explicit MainFrame(bool automated = false, SavePrompt savePrompt = {});
    EditorCtrl* Editor() const { return editor_; }
    bool LoadPath(const wxString& path, bool confirmChanges = true);
    bool SavePath(const wxString& path);
    bool NewDocument();
    bool IsModified() const;
    Encoding CurrentEncoding() const { return encoding_; }
    void ChangeEncoding(Encoding encoding);
    void ConfigureSearch(const wxString& needle, const wxString& replacement = {},
                         bool matchCase = false, bool wholeWord = false);
    bool FindNext(bool backwards = false);
    bool ReplaceCurrent();
    int ReplaceAll();
    wxString LastError() const { return lastError_; }
    void UpdateStatus();

private:
    void BuildMenus();
    void BuildSearchBar(wxWindow* parent);
    void BindCommands();
    void OpenDialog();
    bool Save(bool saveAs = false);
    bool ConfirmChanges();
    void ShowSearch(bool replace);
    void HideSearch();
    void GoToLine();
    void SetEol(int mode);
    void ReportError(const wxString& message);
    void RememberDiskState();
    bool DiskChanged() const;
    int SearchFlags() const;
    void UpdateTitle();
    void EditCommand(int command);
    void UpdateEditCommand(wxUpdateUIEvent& event);

    EditorCtrl* editor_ = nullptr;
    wxPanel* searchBar_ = nullptr;
    wxTextCtrl* findText_ = nullptr;
    wxTextCtrl* replaceText_ = nullptr;
    wxCheckBox* matchCase_ = nullptr;
    wxCheckBox* wholeWord_ = nullptr;
    wxString path_;
    wxString lastError_;
    Encoding encoding_ = Encoding::Utf8;
    bool formatChanged_ = false;
    bool mixedEol_ = false;
    bool automated_ = false;
    SavePrompt savePrompt_;
    wxWindow* editTarget_ = nullptr;
    std::optional<std::filesystem::file_time_type> diskTime_;
    std::uintmax_t diskSize_ = 0;
};

} // namespace notepad
