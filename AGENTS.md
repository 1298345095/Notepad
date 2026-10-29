# Working on Notepad

This repository implements a small C++20/wxWidgets desktop prototype for a
gradual Notepad4 migration. Start with README.md and docs/NOTEPAD4_MIGRATION.md.

## Boundaries

- `DocumentIO` and `DiffLexer` are portable, GUI-free C++20 modules.
- Keep Scintilla/wx display logic in `EditorCtrl`; window commands in `MainFrame`.
- Preserve the UTF-8 byte offsets Scintilla uses. Do not confuse them with Unicode
  code points or `wxString` indexes.
- Reject unsupported encodings instead of silently replacing characters.
- Preserve original file bytes and the in-memory document on failed save/open.
- Keep attribution for migrated code. A migrated lexer is not a claim that the
  entire Notepad4 Scintilla fork has been ported.

## Validation

Core only: configure with `-DNOTEPAD_BUILD_GUI=OFF`, build, and run CTest.
Desktop: build the GUI and run `Notepad --smoke-test --smoke-output smoke.json`
in a display session; Linux CI uses Xvfb. A smoke failure must exit nonzero.
Inspect the corresponding GitHub Actions run before claiming a platform works.

Use `tools/generate_fixture.py` and `--benchmark` for reproducible loading
measurements. Distinguish load/paint timing, startup timing, input latency and
memory. Report environment and build configuration; don't extrapolate a single
run to all machines or compare package size while excluding runtime libraries.

For changes to file handling, search, or editing, add checks at the public
behavior boundary. Real IME/candidate positioning and desktop accessibility
still need target-platform interaction, beyond programmatic smoke tests.
