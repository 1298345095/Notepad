#include "MainFrame.h"

#include <algorithm>
#include <climits>
#include <stdexcept>
#include <wx/aboutdlg.h>
#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/numdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <wx/toolbar.h>

namespace notepad {
namespace {
enum : int {
    IdFindNext = wxID_HIGHEST + 1, IdFindPrevious, IdGoToLine, IdWrap, IdWhitespace,
    IdDark, IdZoomIn, IdZoomOut, IdZoomReset, IdEolLF, IdEolCRLF, IdEolCR,
    IdEncodingFirst, IdEncodingLast = IdEncodingFirst + 3,
    IdLanguageFirst, IdLanguageLast = IdLanguageFirst + 9,
};
constexpr size_t kLargeFileBytes = 20 * 1024 * 1024;

class BulkLoadNotifications {
public:
    explicit BulkLoadNotifications(EditorCtrl& editor)
        : editor_(editor), mask_(editor.GetModEventMask()), undo_(editor.GetUndoCollection()) {
        // wx converts SCN_MODIFIED's inserted text into a wxString even if no
        // application handler consumes it. Avoid a full-document wide-string
        // temporary during bulk loading; normal edit notifications resume.
        editor_.SetModEventMask(0);
        editor_.SetUndoCollection(false);
    }
    ~BulkLoadNotifications() {
        editor_.SetUndoCollection(undo_);
        editor_.SetModEventMask(mask_);
    }
private:
    EditorCtrl& editor_;
    int mask_;
    bool undo_;
};

class DocumentDropTarget final : public wxFileDropTarget {
public:
    explicit DocumentDropTarget(MainFrame* frame) : frame_(frame) {}
    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString& files) override {
        return files.size() == 1 && frame_->LoadPath(files.front());
    }
private:
    MainFrame* frame_;
};

wxString SizeLabel(int bytes) {
    if (bytes >= 1024 * 1024) return wxString::Format("%.1f MiB", bytes / (1024.0 * 1024.0));
    if (bytes >= 1024) return wxString::Format("%.1f KiB", bytes / 1024.0);
    return wxString::Format("%d bytes", bytes);
}
}

std::filesystem::path NativePath(const wxString& path) {
#ifdef _WIN32
    return std::filesystem::path(path.ToStdWstring());
#else
    const auto bytes = path.utf8_str();
    return std::filesystem::path(std::string(bytes.data(), bytes.length()));
#endif
}

MainFrame::MainFrame(bool automated, SavePrompt savePrompt)
    : wxFrame(nullptr, wxID_ANY, "Notepad", wxDefaultPosition, wxSize(1080, 740)), automated_(automated), savePrompt_(std::move(savePrompt)) {
    SetMinSize(FromDIP(wxSize(720, 480)));
    SetSize(FromDIP(wxSize(1080, 740)));
    BuildMenus();
    auto* toolbar = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
    toolbar->SetToolBitmapSize(FromDIP(wxSize(20, 20)));
    auto addTool = [toolbar](int id, const wxString& label, const wxArtID& art, const wxString& help) {
        toolbar->AddTool(id, label, wxArtProvider::GetBitmap(art, wxART_TOOLBAR), help);
    };
    addTool(wxID_NEW, "New", wxART_NEW, "New document (Ctrl+N)");
    addTool(wxID_OPEN, "Open", wxART_FILE_OPEN, "Open a text file (Ctrl+O)");
    addTool(wxID_SAVE, "Save", wxART_FILE_SAVE, "Save (Ctrl+S)");
    toolbar->AddSeparator();
    addTool(wxID_UNDO, "Undo", wxART_UNDO, "Undo (Ctrl+Z)");
    addTool(wxID_REDO, "Redo", wxART_REDO, "Redo (Ctrl+Y)");
    toolbar->AddSeparator();
    addTool(wxID_FIND, "Find", wxART_FIND, "Find text (Ctrl+F)");
    toolbar->Realize();

    auto* surface = new wxPanel(this);
    auto* layout = new wxBoxSizer(wxVERTICAL);
    BuildSearchBar(surface);
    layout->Add(searchBar_, 0, wxEXPAND);
    editor_ = new EditorCtrl(surface);
    editTarget_ = editor_;
    layout->Add(editor_, 1, wxEXPAND);
    surface->SetSizer(layout);
    auto* frameLayout = new wxBoxSizer(wxVERTICAL);
    frameLayout->Add(surface, 1, wxEXPAND);
    SetSizer(frameLayout);
    searchBar_->Hide();
    CreateStatusBar(5);
    const int widths[] = {-1, FromDIP(140), FromDIP(150), FromDIP(140), FromDIP(120)};
    SetStatusWidths(5, widths);
    editor_->SetDropTarget(new DocumentDropTarget(this));
    BindCommands();
    editor_->Bind(wxEVT_STC_UPDATEUI, [this](wxStyledTextEvent&) {
        editor_->RefreshLineNumberWidth();
        UpdateStatus();
    });
    editor_->Bind(wxEVT_STC_SAVEPOINTLEFT, [this](wxStyledTextEvent&) { UpdateTitle(); });
    editor_->Bind(wxEVT_STC_SAVEPOINTREACHED, [this](wxStyledTextEvent&) { UpdateTitle(); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (!automated_ && event.CanVeto() && !ConfirmChanges()) { event.Veto(); return; }
        Destroy();
    });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE && searchBar_->IsShown()) HideSearch();
        else event.Skip();
    });
    NewDocument();
    Centre();
    editor_->SetFocus();
}

void MainFrame::BuildMenus() {
    auto* bar = new wxMenuBar;
    auto* file = new wxMenu;
    file->Append(wxID_NEW, "&New\tCtrl+N");
    file->Append(wxID_OPEN, "&Open...\tCtrl+O");
    file->Append(wxID_SAVE, "&Save\tCtrl+S");
    file->Append(wxID_SAVEAS, "Save &as...\tCtrl+Shift+S");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit\tAlt+F4");
    bar->Append(file, "&File");
    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
    edit->Append(wxID_REDO, "&Redo\tCtrl+Y");
    edit->AppendSeparator();
    edit->Append(wxID_CUT, "Cu&t\tCtrl+X");
    edit->Append(wxID_COPY, "&Copy\tCtrl+C");
    edit->Append(wxID_PASTE, "&Paste\tCtrl+V");
    edit->Append(wxID_SELECTALL, "Select &all\tCtrl+A");
    bar->Append(edit, "&Edit");
    auto* search = new wxMenu;
    search->Append(wxID_FIND, "&Find...\tCtrl+F");
    search->Append(wxID_REPLACE, "&Replace...\tCtrl+H");
    search->Append(IdFindNext, "Find &next\tF3");
    search->Append(IdFindPrevious, "Find &previous\tShift+F3");
    search->AppendSeparator();
    search->Append(IdGoToLine, "Go to &line...\tCtrl+G");
    bar->Append(search, "&Search");
    auto* view = new wxMenu;
    view->AppendCheckItem(IdWrap, "&Word wrap\tAlt+Z");
    view->AppendCheckItem(IdWhitespace, "Show &whitespace");
    view->AppendCheckItem(IdDark, "&Dark editor");
    view->AppendSeparator();
    view->Append(IdZoomIn, "Zoom &in\tCtrl++");
    view->Append(IdZoomOut, "Zoom &out\tCtrl+-");
    view->Append(IdZoomReset, "Reset &zoom\tCtrl+0");
    bar->Append(view, "&View");
    auto* format = new wxMenu;
    auto* encoding = new wxMenu;
    for (int value = 0; value < 4; ++value)
        encoding->AppendRadioItem(IdEncodingFirst + value, wxString::FromUTF8(EncodingName(static_cast<Encoding>(value))));
    format->AppendSubMenu(encoding, "&Encoding");
    auto* endings = new wxMenu;
    endings->AppendRadioItem(IdEolLF, "LF (Unix / macOS)");
    endings->AppendRadioItem(IdEolCRLF, "CRLF (Windows)");
    endings->AppendRadioItem(IdEolCR, "CR (classic Mac)");
    format->AppendSubMenu(endings, "Convert &line endings");
    bar->Append(format, "F&ormat");
    auto* language = new wxMenu;
    for (int value = 0; value < 10; ++value)
        language->AppendRadioItem(IdLanguageFirst + value, wxString::FromUTF8(EditorCtrl::LanguageName(static_cast<Language>(value))));
    bar->Append(language, "&Language");
    auto* help = new wxMenu;
    help->Append(wxID_ABOUT, "&About Notepad");
    bar->Append(help, "&Help");
    SetMenuBar(bar);
}

void MainFrame::BuildSearchBar(wxWindow* parent) {
    searchBar_ = new wxPanel(parent);
    auto* grid = new wxFlexGridSizer(2, FromDIP(6), FromDIP(8));
    grid->AddGrowableCol(1, 1);
    grid->Add(new wxStaticText(searchBar_, wxID_ANY, "Find"), 0, wxALIGN_CENTER_VERTICAL);
    auto* findRow = new wxBoxSizer(wxHORIZONTAL);
    findText_ = new wxTextCtrl(searchBar_, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    findText_->SetName("Find text");
    findRow->Add(findText_, 1, wxRIGHT, FromDIP(6));
    auto* next = new wxButton(searchBar_, wxID_ANY, "Next");
    auto* previous = new wxButton(searchBar_, wxID_ANY, "Previous");
    findRow->Add(previous, 0, wxRIGHT, FromDIP(4));
    findRow->Add(next, 0, wxRIGHT, FromDIP(8));
    matchCase_ = new wxCheckBox(searchBar_, wxID_ANY, "Match case");
    wholeWord_ = new wxCheckBox(searchBar_, wxID_ANY, "Whole word");
    findRow->Add(matchCase_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    findRow->Add(wholeWord_, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(findRow, 1, wxEXPAND);
    grid->Add(new wxStaticText(searchBar_, wxID_ANY, "Replace"), 0, wxALIGN_CENTER_VERTICAL);
    auto* replaceRow = new wxBoxSizer(wxHORIZONTAL);
    replaceText_ = new wxTextCtrl(searchBar_, wxID_ANY);
    replaceText_->SetName("Replacement text");
    replaceRow->Add(replaceText_, 1, wxRIGHT, FromDIP(6));
    auto* replace = new wxButton(searchBar_, wxID_ANY, "Replace");
    auto* all = new wxButton(searchBar_, wxID_ANY, "Replace all");
    auto* close = new wxButton(searchBar_, wxID_ANY, "Close");
    replaceRow->Add(replace, 0, wxRIGHT, FromDIP(4));
    replaceRow->Add(all, 0, wxRIGHT, FromDIP(4));
    replaceRow->Add(close, 0);
    grid->Add(replaceRow, 1, wxEXPAND);
    auto* padded = new wxBoxSizer(wxVERTICAL);
    padded->Add(grid, 1, wxEXPAND | wxALL, FromDIP(10));
    searchBar_->SetSizer(padded);
    next->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindNext(); });
    previous->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { FindNext(true); });
    replace->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceCurrent(); });
    all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceAll(); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { HideSearch(); });
    findText_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { FindNext(); });
}

void MainFrame::BindCommands() {
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { NewDocument(); }, wxID_NEW);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenDialog(); }, wxID_OPEN);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Save(); }, wxID_SAVE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Save(true); }, wxID_SAVEAS);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    for (const int command : {wxID_UNDO, wxID_REDO, wxID_CUT, wxID_COPY, wxID_PASTE, wxID_SELECTALL}) {
        Bind(wxEVT_MENU, [this](wxCommandEvent& event) { EditCommand(event.GetId()); }, command);
        Bind(wxEVT_UPDATE_UI, &MainFrame::UpdateEditCommand, this, command);
    }
    for (wxWindow* target : {static_cast<wxWindow*>(editor_), static_cast<wxWindow*>(findText_), static_cast<wxWindow*>(replaceText_)}) {
        target->Bind(wxEVT_SET_FOCUS, [this, target](wxFocusEvent& event) { editTarget_ = target; event.Skip(); });
    }
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowSearch(false); }, wxID_FIND);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowSearch(true); }, wxID_REPLACE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(); }, IdFindNext);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { FindNext(true); }, IdFindPrevious);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { GoToLine(); }, IdGoToLine);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { editor_->SetWrapMode(e.IsChecked() ? wxSTC_WRAP_WORD : wxSTC_WRAP_NONE); }, IdWrap);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        editor_->SetViewWhiteSpace(e.IsChecked() ? wxSTC_WS_VISIBLEALWAYS : wxSTC_WS_INVISIBLE);
        editor_->SetViewEOL(e.IsChecked());
    }, IdWhitespace);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { editor_->SetDark(e.IsChecked()); }, IdDark);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->ZoomIn(); }, IdZoomIn);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->ZoomOut(); }, IdZoomOut);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->SetZoom(0); editor_->RefreshLineNumberWidth(); }, IdZoomReset);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetEol(wxSTC_EOL_LF); }, IdEolLF);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetEol(wxSTC_EOL_CRLF); }, IdEolCRLF);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetEol(wxSTC_EOL_CR); }, IdEolCR);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) { ChangeEncoding(static_cast<Encoding>(e.GetId() - IdEncodingFirst)); }, IdEncodingFirst, IdEncodingLast);
    Bind(wxEVT_MENU, [this](wxCommandEvent& e) {
        editor_->SetLanguage(static_cast<Language>(e.GetId() - IdLanguageFirst), path_);
        UpdateStatus();
    }, IdLanguageFirst, IdLanguageLast);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        wxAboutDialogInfo info;
        info.SetName("Notepad");
        info.SetVersion("0.1.0 prototype");
        info.SetDescription("A small cross-platform editor built with C++ and wxWidgets / Scintilla.\n\nDiff highlighting adapts Notepad4's real lexer.\nThis prototype starts the migration; it is not a complete Notepad4 port.");
        info.SetWebSite("https://github.com/1298345095/Notepad");
        wxAboutBox(info, this);
    }, wxID_ABOUT);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& e) { e.Enable(editor_ && IsModified()); }, wxID_SAVE);
}

void MainFrame::EditCommand(int command) {
    if (auto* text = wxDynamicCast(editTarget_, wxTextCtrl)) {
        switch (command) {
        case wxID_UNDO: text->Undo(); break;
        case wxID_REDO: text->Redo(); break;
        case wxID_CUT: text->Cut(); break;
        case wxID_COPY: text->Copy(); break;
        case wxID_PASTE: text->Paste(); break;
        case wxID_SELECTALL: text->SelectAll(); break;
        default: break;
        }
    } else {
        switch (command) {
        case wxID_UNDO: editor_->Undo(); break;
        case wxID_REDO: editor_->Redo(); break;
        case wxID_CUT: editor_->Cut(); break;
        case wxID_COPY: editor_->Copy(); break;
        case wxID_PASTE: editor_->Paste(); break;
        case wxID_SELECTALL: editor_->SelectAll(); break;
        default: break;
        }
    }
}

void MainFrame::UpdateEditCommand(wxUpdateUIEvent& event) {
    if (!editor_) { event.Enable(false); return; }
    auto* text = wxDynamicCast(editTarget_, wxTextCtrl);
    long from = 0, to = 0;
    if (text) text->GetSelection(&from, &to);
    const bool selected = text ? from != to : editor_->GetSelectionStart() != editor_->GetSelectionEnd();
    switch (event.GetId()) {
    case wxID_UNDO: event.Enable(text ? text->CanUndo() : editor_->CanUndo()); break;
    case wxID_REDO: event.Enable(text ? text->CanRedo() : editor_->CanRedo()); break;
    case wxID_CUT: case wxID_COPY: event.Enable(selected); break;
    case wxID_PASTE: event.Enable(text ? text->CanPaste() : editor_->CanPaste()); break;
    default: event.Enable(true); break;
    }
}

bool MainFrame::IsModified() const { return formatChanged_ || editor_->GetModify(); }

bool MainFrame::ConfirmChanges() {
    if (!IsModified()) return true;
    const wxString name = path_.empty() ? "Untitled" : wxFileName(path_).GetFullName();
    const int answer = savePrompt_ ? savePrompt_(name) : automated_ ? wxNO
        : wxMessageBox("Save changes to " + name + "?", "Unsaved changes", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (answer == wxCANCEL) return false;
    return answer != wxYES || Save();
}

bool MainFrame::NewDocument() {
    if (!ConfirmChanges()) return false;
    editor_->SetTextRaw("");
    editor_->EmptyUndoBuffer();
    editor_->SetSavePoint();
    editor_->SetEOLMode(wxSTC_EOL_LF);
    editor_->SetLargeFileMode(false);
    editor_->SetLanguage(Language::Automatic, {});
    encoding_ = Encoding::Utf8;
    formatChanged_ = false;
    mixedEol_ = false;
    path_.clear();
    diskTime_.reset();
    GetMenuBar()->Check(IdLanguageFirst, true);
    GetMenuBar()->Check(IdEncodingFirst, true);
    GetMenuBar()->Check(IdEolLF, true);
    UpdateTitle();
    UpdateStatus();
    return true;
}

bool MainFrame::LoadPath(const wxString& path, bool confirmChanges) {
    try {
        if (confirmChanges && !ConfirmChanges()) return false;
        const auto native = NativePath(path);
        const auto beforeTime = std::filesystem::last_write_time(native);
        const auto beforeSize = std::filesystem::file_size(native);
        auto document = LoadDocument(native);
        const auto afterTime = std::filesystem::last_write_time(native);
        const auto afterSize = std::filesystem::file_size(native);
        if (beforeTime != afterTime || beforeSize != afterSize)
            throw std::runtime_error("The file changed while it was being read. Please open it again.");
        if (document.text.size() > static_cast<size_t>(INT_MAX - 1))
            throw std::runtime_error("This prototype supports documents smaller than 2 GiB of decoded UTF-8 text.");
        editor_->Freeze();
        {
            BulkLoadNotifications bulk(*editor_);
            editor_->SetLexer(wxSTC_LEX_NULL);
            editor_->Allocate(static_cast<int>(document.text.size()));
            editor_->SetTextRaw(document.text.c_str());
        }
        editor_->EmptyUndoBuffer();
        editor_->SetSavePoint();
        editor_->SetLargeFileMode(document.text.size() >= kLargeFileBytes);
        editor_->SetLanguage(Language::Automatic, path);
        const int ending = document.eol == Eol::CRLF ? wxSTC_EOL_CRLF : document.eol == Eol::CR ? wxSTC_EOL_CR : wxSTC_EOL_LF;
        editor_->SetEOLMode(ending);
        editor_->GotoPos(0);
        editor_->Thaw();
        path_ = path;
        encoding_ = document.encoding;
        mixedEol_ = document.eol == Eol::Mixed;
        formatChanged_ = false;
        lastError_.clear();
        diskTime_ = afterTime;
        diskSize_ = afterSize;
        GetMenuBar()->Check(IdLanguageFirst, true);
        GetMenuBar()->Check(IdEncodingFirst + static_cast<int>(encoding_), true);
        GetMenuBar()->Check(ending == wxSTC_EOL_CRLF ? IdEolCRLF : ending == wxSTC_EOL_CR ? IdEolCR : IdEolLF, true);
        UpdateTitle();
        UpdateStatus();
        editor_->SetFocus();
        return true;
    } catch (const std::exception& error) {
        if (editor_->IsFrozen()) editor_->Thaw();
        ReportError(wxString::FromUTF8(error.what()));
        return false;
    }
}

bool MainFrame::SavePath(const wxString& path) {
    try {
        if (path == path_ && DiskChanged()) {
            if (automated_) throw std::runtime_error("The file changed on disk since it was opened.");
            if (wxMessageBox("This file has changed on disk. Overwrite it with your current document?", "File changed", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) return false;
        }
        const auto buffer = editor_->GetTextRaw();
        const std::string_view text(buffer.data(), static_cast<size_t>(editor_->GetTextLength()));
        SaveDocument(NativePath(path), text, encoding_);
        mixedEol_ = DetectEol(text) == Eol::Mixed;
        const bool renamed = path_ != path;
        path_ = path;
        editor_->SetSavePoint();
        formatChanged_ = false;
        if (renamed && editor_->SelectedLanguage() == Language::Automatic) editor_->SetLanguage(Language::Automatic, path);
        RememberDiskState();
        lastError_.clear();
        UpdateTitle();
        UpdateStatus();
        SetStatusText("Saved " + wxFileName(path).GetFullName(), 0);
        return true;
    } catch (const std::exception& error) {
        ReportError(wxString::FromUTF8(error.what()));
        return false;
    }
}

void MainFrame::OpenDialog() {
    wxFileDialog dialog(this, "Open a text file", {}, {}, "Text and source files|*.txt;*.md;*.cpp;*.h;*.py;*.js;*.ts;*.json;*.diff;*.patch;*.html;*.css;*.log|All files|*", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) LoadPath(dialog.GetPath());
}

bool MainFrame::Save(bool saveAs) {
    if (!saveAs && !path_.empty()) return SavePath(path_);
    wxFileDialog dialog(this, "Save document", path_.empty() ? wxString{} : wxFileName(path_).GetPath(),
        path_.empty() ? "Untitled.txt" : wxFileName(path_).GetFullName(), "All files|*", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    return dialog.ShowModal() == wxID_OK && SavePath(dialog.GetPath());
}

void MainFrame::RememberDiskState() {
    std::error_code error;
    const auto native = NativePath(path_);
    const auto time = std::filesystem::last_write_time(native, error);
    diskTime_ = error ? std::nullopt : std::optional(time);
    diskSize_ = std::filesystem::file_size(native, error);
}

bool MainFrame::DiskChanged() const {
    if (!diskTime_) return false;
    std::error_code error;
    const auto native = NativePath(path_);
    const auto time = std::filesystem::last_write_time(native, error);
    if (error || time != *diskTime_) return true;
    const auto size = std::filesystem::file_size(native, error);
    return error || size != diskSize_;
}

void MainFrame::ChangeEncoding(Encoding encoding) {
    if (encoding_ == encoding) return;
    encoding_ = encoding;
    formatChanged_ = true;
    GetMenuBar()->Check(IdEncodingFirst + static_cast<int>(encoding), true);
    UpdateTitle();
    UpdateStatus();
}

void MainFrame::SetEol(int mode) {
    editor_->BeginUndoAction();
    editor_->ConvertEOLs(mode);
    editor_->EndUndoAction();
    editor_->SetEOLMode(mode);
    mixedEol_ = false;
    UpdateStatus();
}

void MainFrame::ShowSearch(bool replace) {
    const wxString selected = editor_->GetSelectedText();
    if (!selected.empty() && !selected.Contains('\n') && !selected.Contains('\r')) findText_->ChangeValue(selected);
    searchBar_->Show();
    searchBar_->GetParent()->Layout();
    (replace ? replaceText_ : findText_)->SetFocus();
    findText_->SelectAll();
}

void MainFrame::HideSearch() {
    searchBar_->Hide();
    searchBar_->GetParent()->Layout();
    editor_->SetFocus();
}

void MainFrame::ConfigureSearch(const wxString& needle, const wxString& replacement, bool matchCase, bool wholeWord) {
    findText_->ChangeValue(needle);
    replaceText_->ChangeValue(replacement);
    matchCase_->SetValue(matchCase);
    wholeWord_->SetValue(wholeWord);
}

int MainFrame::SearchFlags() const {
    return (matchCase_->IsChecked() ? wxSTC_FIND_MATCHCASE : 0) | (wholeWord_->IsChecked() ? wxSTC_FIND_WHOLEWORD : 0);
}

bool MainFrame::FindNext(bool backwards) {
    const wxString needle = findText_->GetValue();
    if (needle.empty()) { ShowSearch(false); return false; }
    const int length = editor_->GetTextLength();
    const int start = backwards ? editor_->GetSelectionStart() : editor_->GetSelectionEnd();
    editor_->SetSearchFlags(SearchFlags());
    editor_->SetTargetStart(start);
    editor_->SetTargetEnd(backwards ? 0 : length);
    int found = editor_->SearchInTarget(needle);
    bool wrapped = false;
    if (found < 0) {
        wrapped = true;
        editor_->SetTargetStart(backwards ? length : 0);
        editor_->SetTargetEnd(start);
        found = editor_->SearchInTarget(needle);
    }
    if (found < 0) { SetStatusText("No matches", 0); return false; }
    editor_->SetSelection(editor_->GetTargetStart(), editor_->GetTargetEnd());
    editor_->EnsureCaretVisible();
    SetStatusText(wrapped ? "Search wrapped" : "Match found", 0);
    return true;
}

bool MainFrame::ReplaceCurrent() {
    const wxString needle = findText_->GetValue();
    if (needle.empty()) { ShowSearch(true); return false; }
    const int start = editor_->GetSelectionStart();
    const int end = editor_->GetSelectionEnd();
    editor_->SetSearchFlags(SearchFlags());
    editor_->SetTargetStart(start);
    editor_->SetTargetEnd(end);
    if (start == end || editor_->SearchInTarget(needle) != start || editor_->GetTargetEnd() != end) return FindNext();
    editor_->BeginUndoAction();
    const int length = editor_->ReplaceTarget(replaceText_->GetValue());
    editor_->EndUndoAction();
    editor_->SetSelection(start + length, start + length);
    FindNext();
    return true;
}

int MainFrame::ReplaceAll() {
    const wxString needle = findText_->GetValue();
    if (needle.empty()) { ShowSearch(true); return 0; }
    const wxString replacement = replaceText_->GetValue();
    editor_->SetSearchFlags(SearchFlags());
    editor_->BeginUndoAction();
    int count = 0;
    int position = 0;
    while (position <= editor_->GetTextLength()) {
        editor_->SetTargetStart(position);
        editor_->SetTargetEnd(editor_->GetTextLength());
        const int found = editor_->SearchInTarget(needle);
        if (found < 0) break;
        const int inserted = editor_->ReplaceTarget(replacement);
        position = found + inserted;
        ++count;
    }
    editor_->EndUndoAction();
    SetStatusText(wxString::Format("Replaced %d occurrence(s)", count), 0);
    return count;
}

void MainFrame::GoToLine() {
    const long line = wxGetNumberFromUser("Enter a line number", "Line", "Go to line",
        editor_->GetCurrentLine() + 1, 1, editor_->GetLineCount(), this);
    if (line >= 1) { editor_->GotoLine(static_cast<int>(line - 1)); editor_->SetFocus(); }
}

void MainFrame::UpdateTitle() {
    const wxString name = path_.empty() ? "Untitled" : wxFileName(path_).GetFullName();
    SetTitle((IsModified() ? "* " : "") + name + " - Notepad");
}

void MainFrame::UpdateStatus() {
    if (!editor_) return;
    wxString mode = wxString::FromUTF8(EditorCtrl::LanguageName(editor_->ActiveLanguage()));
    if (editor_->IsLargeFileMode() && editor_->SelectedLanguage() == Language::Automatic)
        mode = "Large file - automatic highlighting off";
    SetStatusText(mode, 0);
    SetStatusText(wxString::Format("Ln %d, Col %d", editor_->GetCurrentLine() + 1, editor_->GetColumn(editor_->GetCurrentPos()) + 1), 1);
    SetStatusText(wxString::FromUTF8(EncodingName(encoding_)), 2);
    SetStatusText(mixedEol_ ? "Mixed line endings" : editor_->GetEOLMode() == wxSTC_EOL_CRLF ? "CRLF" : editor_->GetEOLMode() == wxSTC_EOL_CR ? "CR" : "LF", 3);
    SetStatusText(SizeLabel(editor_->GetTextLength()), 4);
}

void MainFrame::ReportError(const wxString& message) {
    lastError_ = message;
    if (!automated_) wxMessageBox(message, "Notepad", wxOK | wxICON_ERROR, this);
}

} // namespace notepad
