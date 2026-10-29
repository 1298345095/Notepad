#include "EditorCtrl.h"
#include "DiffLexer.h"

#include <algorithm>
#include <wx/filename.h>

namespace notepad {
namespace {
Language DetectLanguage(const wxString& filename) {
    const wxString ext = wxFileName(filename).GetExt().Lower();
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c" || ext == "h" || ext == "hpp" || ext == "hxx") return Language::Cpp;
    if (ext == "py" || ext == "pyw") return Language::Python;
    if (ext == "js" || ext == "jsx" || ext == "ts" || ext == "tsx" || ext == "mjs") return Language::JavaScript;
    if (ext == "json" || ext == "jsonc") return Language::Json;
    if (ext == "html" || ext == "htm" || ext == "xml" || ext == "svg") return Language::Html;
    if (ext == "css") return Language::Css;
    if (ext == "md" || ext == "markdown") return Language::Markdown;
    if (ext == "diff" || ext == "patch") return Language::Diff;
    return Language::PlainText;
}
}

EditorCtrl::EditorCtrl(wxWindow* parent) : wxStyledTextCtrl(parent, wxID_ANY) {
    SetCodePage(wxSTC_CP_UTF8);
    SetTabWidth(4);
    SetIndent(4);
    SetUseTabs(false);
    SetTabIndents(true);
    SetBackSpaceUnIndents(true);
    SetIndentationGuides(wxSTC_IV_LOOKBOTH);
    SetScrollWidth(1);
    SetScrollWidthTracking(true);
    SetMarginType(0, wxSTC_MARGIN_NUMBER);
    SetMarginWidth(1, 0);
    SetMarginType(2, wxSTC_MARGIN_SYMBOL);
    SetMarginMask(2, wxSTC_MASK_FOLDERS);
    SetMarginSensitive(2, true);
    SetMarginLeft(8);
    SetMarginRight(8);
    SetCaretLineVisible(true);
    SetCaretLineVisibleAlways(true);
    SetCaretWidth(2);
    SetWrapMode(wxSTC_WRAP_NONE);
    SetLayoutCache(wxSTC_CACHE_PAGE);
    SetMultipleSelection(true);
    SetAdditionalSelectionTyping(true);
    SetMultiPaste(wxSTC_MULTIPASTE_EACH);
    SetMouseDwellTime(500);
    SetProperty("fold", "1");
    SetProperty("fold.compact", "0");
    SetAutomaticFold(wxSTC_AUTOMATICFOLD_SHOW | wxSTC_AUTOMATICFOLD_CHANGE);
    Bind(wxEVT_STC_STYLENEEDED, &EditorCtrl::OnStyleNeeded, this);
    Bind(wxEVT_STC_MARGINCLICK, &EditorCtrl::OnMarginClick, this);
    Bind(wxEVT_STC_ZOOM, [this](wxStyledTextEvent&) { marginDigits_ = 0; RefreshLineNumberWidth(); });
    SetLanguage(Language::Automatic);
}

void EditorCtrl::SetLanguage(Language language, const wxString& filename) {
    selectedLanguage_ = language;
    if (!filename.empty() || language == Language::Automatic) filename_ = filename;
    activeLanguage_ = language == Language::Automatic ? DetectLanguage(filename_) : language;
    if (largeFile_ && language == Language::Automatic) activeLanguage_ = Language::PlainText;
    int lexer = wxSTC_LEX_NULL;
    switch (activeLanguage_) {
    case Language::Cpp: case Language::JavaScript: lexer = wxSTC_LEX_CPP; break;
    case Language::Python: lexer = wxSTC_LEX_PYTHON; break;
    case Language::Json: lexer = wxSTC_LEX_JSON; break;
    case Language::Html: lexer = wxSTC_LEX_HTML; break;
    case Language::Css: lexer = wxSTC_LEX_CSS; break;
    case Language::Markdown: lexer = wxSTC_LEX_MARKDOWN; break;
    case Language::Diff: lexer = wxSTC_LEX_CONTAINER; break;
    default: break;
    }
    SetLexer(lexer);
    for (int index = 0; index < 9; ++index) SetKeyWords(index, "");
    if (activeLanguage_ == Language::Cpp) {
        SetKeyWords(0, "alignas alignof and asm auto break case catch class concept const consteval constexpr constinit const_cast continue co_await co_return co_yield decltype default delete do dynamic_cast else enum explicit export extern false final for friend goto if inline mutable namespace new noexcept not nullptr operator or override private protected public register reinterpret_cast requires return sizeof static static_assert static_cast struct switch template this thread_local throw true try typedef typeid typename union using virtual volatile while xor");
        SetKeyWords(1, "bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t size_t string string_view vector array unique_ptr shared_ptr");
    } else if (activeLanguage_ == Language::JavaScript) {
        SetKeyWords(0, "async await break case catch class const continue debugger default delete do else enum export extends false finally for from function get if implements import in instanceof interface let new null of package private protected public return set static super switch this throw true try typeof undefined var void while with yield");
        SetKeyWords(1, "boolean number string symbol bigint any unknown never readonly abstract declare type");
    } else if (activeLanguage_ == Language::Python) {
        SetKeyWords(0, "False None True and as assert async await break case class continue def del elif else except finally for from global if import in is lambda match nonlocal not or pass raise return try while with yield");
        SetKeyWords(1, "abs all any bool bytes dict enumerate filter float int len list map max min object open print range repr reversed set sorted str sum super tuple type zip");
    } else if (activeLanguage_ == Language::Json) {
        SetKeyWords(0, "false true null");
        SetProperty("lexer.json.allow.comments", "1");
    } else if (activeLanguage_ == Language::Html) {
        SetKeyWords(0, "a article aside body button code div footer form h1 h2 h3 head header html img input label li link main meta nav ol p pre script section span style table tbody td textarea th thead title tr ul class href id lang name src type value");
    } else if (activeLanguage_ == Language::Css) {
        SetKeyWords(0, "background background-color border border-radius bottom box-shadow color display font font-family font-size font-weight gap grid height left line-height margin opacity overflow padding position right text-align top transform transition width z-index");
    }
    const bool folding = activeLanguage_ != Language::PlainText;
    SetProperty("fold", folding ? "1" : "0");
    SetMarginWidth(2, folding ? FromDIP(16) : 0);
    ApplyStyles();
    ClearDocumentStyle();
    Colourise(0, -1);
}

void EditorCtrl::SetLargeFileMode(bool enabled) {
    largeFile_ = enabled;
    SetLayoutCache(enabled ? wxSTC_CACHE_NONE : wxSTC_CACHE_PAGE);
}

void EditorCtrl::SetDark(bool dark) {
    dark_ = dark;
    ApplyStyles();
    Refresh();
}

void EditorCtrl::ApplyStyles() {
    const wxColour background(dark_ ? "#151B24" : "#FFFFFF");
    const wxColour foreground(dark_ ? "#D9E2EF" : "#203047");
    const wxColour muted(dark_ ? "#71829C" : "#8190A5");
    const wxColour accent(dark_ ? "#82AAFF" : "#175BD7");
    const wxColour stringColour(dark_ ? "#A8D8A2" : "#267043");
    const wxColour numberColour(dark_ ? "#F4BE7A" : "#995910");
    const wxColour special(dark_ ? "#CFABFF" : "#8747BD");
    wxFont font(wxFontInfo(11).Family(wxFONTFAMILY_TELETYPE));
#ifdef __WXMSW__
    font.SetFaceName("Consolas");
#elif defined(__WXOSX__)
    font.SetFaceName("Menlo");
#else
    font.SetFaceName("monospace");
#endif
    StyleResetDefault();
    StyleSetFont(wxSTC_STYLE_DEFAULT, font);
    StyleSetForeground(wxSTC_STYLE_DEFAULT, foreground);
    StyleSetBackground(wxSTC_STYLE_DEFAULT, background);
    StyleClearAll();
    StyleSetForeground(wxSTC_STYLE_LINENUMBER, muted);
    StyleSetBackground(wxSTC_STYLE_LINENUMBER, wxColour(dark_ ? "#1B2330" : "#F4F6F9"));
    StyleSetBackground(wxSTC_STYLE_BRACELIGHT, wxColour(dark_ ? "#354A68" : "#DCE9FF"));
    StyleSetForeground(wxSTC_STYLE_BRACELIGHT, accent);
    StyleSetForeground(wxSTC_STYLE_BRACEBAD, wxColour("#DA4254"));
    SetCaretForeground(foreground);
    SetCaretLineBackground(wxColour(dark_ ? "#202A39" : "#F5F8FD"));
    SetSelBackground(true, wxColour(dark_ ? "#354D70" : "#CFDFFA"));
    SetSelForeground(false, foreground);
    SetWhitespaceForeground(true, muted);
    SetFoldMarginColour(true, background);
    SetFoldMarginHiColour(true, background);
    for (int marker : {wxSTC_MARKNUM_FOLDER, wxSTC_MARKNUM_FOLDEROPEN, wxSTC_MARKNUM_FOLDERSUB,
                       wxSTC_MARKNUM_FOLDERTAIL, wxSTC_MARKNUM_FOLDEREND,
                       wxSTC_MARKNUM_FOLDEROPENMID, wxSTC_MARKNUM_FOLDERMIDTAIL}) {
        const int symbol = (marker == wxSTC_MARKNUM_FOLDER || marker == wxSTC_MARKNUM_FOLDEREND)
            ? wxSTC_MARK_BOXPLUS : (marker == wxSTC_MARKNUM_FOLDEROPEN || marker == wxSTC_MARKNUM_FOLDEROPENMID)
            ? wxSTC_MARK_BOXMINUS : wxSTC_MARK_EMPTY;
        MarkerDefine(marker, symbol, muted, background);
    }
    auto colour = [this](int style, const wxColour& c, bool bold = false) {
        StyleSetForeground(style, c);
        StyleSetBold(style, bold);
    };
    switch (activeLanguage_) {
    case Language::Cpp: case Language::JavaScript:
        for (int style : {wxSTC_C_COMMENT, wxSTC_C_COMMENTLINE, wxSTC_C_COMMENTDOC, wxSTC_C_COMMENTLINEDOC}) colour(style, muted);
        for (int style : {wxSTC_C_STRING, wxSTC_C_CHARACTER, wxSTC_C_STRINGEOL, wxSTC_C_STRINGRAW}) colour(style, stringColour);
        colour(wxSTC_C_WORD, accent, true); colour(wxSTC_C_WORD2, special);
        colour(wxSTC_C_NUMBER, numberColour); colour(wxSTC_C_PREPROCESSOR, special);
        break;
    case Language::Python:
        for (int style : {wxSTC_P_COMMENTLINE, wxSTC_P_COMMENTBLOCK}) colour(style, muted);
        for (int style : {wxSTC_P_STRING, wxSTC_P_CHARACTER, wxSTC_P_TRIPLE, wxSTC_P_TRIPLEDOUBLE}) colour(style, stringColour);
        colour(wxSTC_P_WORD, accent, true); colour(wxSTC_P_WORD2, special);
        colour(wxSTC_P_DEFNAME, special, true); colour(wxSTC_P_CLASSNAME, special, true);
        colour(wxSTC_P_NUMBER, numberColour); colour(wxSTC_P_DECORATOR, special);
        break;
    case Language::Json:
        colour(wxSTC_JSON_STRING, stringColour); colour(wxSTC_JSON_PROPERTYNAME, accent);
        colour(wxSTC_JSON_NUMBER, numberColour); colour(wxSTC_JSON_KEYWORD, special);
        colour(wxSTC_JSON_LINECOMMENT, muted); colour(wxSTC_JSON_BLOCKCOMMENT, muted);
        break;
    case Language::Html:
        colour(wxSTC_H_TAG, accent, true); colour(wxSTC_H_TAGUNKNOWN, accent);
        colour(wxSTC_H_ATTRIBUTE, special); colour(wxSTC_H_COMMENT, muted);
        colour(wxSTC_H_DOUBLESTRING, stringColour); colour(wxSTC_H_SINGLESTRING, stringColour);
        break;
    case Language::Css:
        colour(wxSTC_CSS_TAG, accent); colour(wxSTC_CSS_CLASS, special); colour(wxSTC_CSS_ID, special);
        colour(wxSTC_CSS_IDENTIFIER, accent); colour(wxSTC_CSS_VALUE, stringColour);
        colour(wxSTC_CSS_DOUBLESTRING, stringColour); colour(wxSTC_CSS_SINGLESTRING, stringColour);
        colour(wxSTC_CSS_COMMENT, muted);
        break;
    case Language::Markdown:
        for (int style = wxSTC_MARKDOWN_HEADER1; style <= wxSTC_MARKDOWN_HEADER6; ++style) colour(style, accent, true);
        colour(wxSTC_MARKDOWN_STRONG1, foreground, true); colour(wxSTC_MARKDOWN_STRONG2, foreground, true);
        colour(wxSTC_MARKDOWN_CODE, stringColour); colour(wxSTC_MARKDOWN_CODE2, stringColour);
        colour(wxSTC_MARKDOWN_CODEBK, stringColour); colour(wxSTC_MARKDOWN_LINK, special);
        break;
    case Language::Diff:
        colour(1, muted); colour(2, accent, true); colour(3, special, true); colour(4, accent);
        for (int style : {6, 8, 10}) {
            colour(style, wxColour(dark_ ? "#A5DBAE" : "#236A39"));
            StyleSetBackground(style, wxColour(dark_ ? "#19382B" : "#E9F6EC"));
            StyleSetEOLFilled(style, true);
        }
        for (int style : {5, 9, 11}) {
            colour(style, wxColour(dark_ ? "#F3A6AC" : "#AD3440"));
            StyleSetBackground(style, wxColour(dark_ ? "#42232C" : "#FCECEF"));
            StyleSetEOLFilled(style, true);
        }
        colour(7, numberColour);
        break;
    default: break;
    }
    marginDigits_ = 0;
    RefreshLineNumberWidth();
}

void EditorCtrl::RefreshLineNumberWidth() {
    int digits = 1;
    for (int count = GetLineCount(); count >= 10; count /= 10) ++digits;
    digits = std::max(4, digits);
    if (digits != marginDigits_) {
        marginDigits_ = digits;
        SetMarginWidth(0, TextWidth(wxSTC_STYLE_LINENUMBER, wxString('9', static_cast<size_t>(digits))) + FromDIP(18));
    }
}

void EditorCtrl::StyleDiff(int endPosition) {
    // One-line lookbehind restores a header previously cleared because it was
    // adjacent to another header. Keep logical levels separately from visible
    // fold flags: the preceding header may have been intentionally hidden.
    const int first = std::max(0, LineFromPosition(GetEndStyled()) - 1);
    const int last = LineFromPosition(std::min(endPosition, GetTextLength()));
    int previous = first > 0 ? GetLineState(first - 1) : wxSTC_FOLDLEVELBASE;
    for (int line = first; line < GetLineCount(); ++line) {
        const int start = PositionFromLine(line);
        const int next = line + 1 < GetLineCount() ? PositionFromLine(line + 1) : GetTextLength();
        // Notepad4 examines a 16-byte buffer (15 prefix bytes + terminator).
        // Avoid copying the full line, including for exceptionally long lines.
        const auto bytes = GetTextRangeRaw(start, std::min(next, start + 15));
        const std::string_view prefix(bytes.data(), static_cast<size_t>(std::min(next - start, 15)));
        const auto style = ClassifyDiffLine(prefix);
        const int oldLogicalLevel = GetLineState(line);
        StartStyling(start);
        SetStyling(next - start, static_cast<int>(style));
        const int level = DiffFoldLevel(style, prefix.empty() ? '\0' : prefix.front(), previous);
        if (line > 0 && (level & wxSTC_FOLDLEVELHEADERFLAG) && level == previous)
            SetFoldLevel(line - 1, previous & ~wxSTC_FOLDLEVELHEADERFLAG);
        SetFoldLevel(line, level);
        SetLineState(line, level);
        previous = level;
        // Past the requested style range, propagate only until the outgoing
        // folding state converges with the unchanged remainder of the file.
        // A header's visible flag also depends on its successor. Process that
        // successor before stopping, even if the logical level is unchanged.
        if (line >= last && oldLogicalLevel == level && !(level & wxSTC_FOLDLEVELHEADERFLAG)) break;
    }
}

void EditorCtrl::OnStyleNeeded(wxStyledTextEvent& event) {
    if (activeLanguage_ == Language::Diff) StyleDiff(event.GetPosition());
}

void EditorCtrl::OnMarginClick(wxStyledTextEvent& event) {
    if (event.GetMargin() == 2) ToggleFold(LineFromPosition(event.GetPosition()));
}

const char* EditorCtrl::LanguageName(Language language) {
    switch (language) {
    case Language::Automatic: return "Automatic";
    case Language::Cpp: return "C / C++";
    case Language::Python: return "Python";
    case Language::JavaScript: return "JavaScript / TypeScript";
    case Language::Json: return "JSON";
    case Language::Html: return "HTML / XML";
    case Language::Css: return "CSS";
    case Language::Markdown: return "Markdown";
    case Language::Diff: return "Diff (Notepad4)";
    default: return "Plain text";
    }
}

} // namespace notepad
