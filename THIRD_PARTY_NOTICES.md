# Third-party notices

New application code is distributed under the MIT license in `LICENSE`.

## Notepad4 / Scintilla Diff rules

`src/DiffLexer.cpp` adapts `scintilla/lexers/LexDiff.cxx` from
[zufuliu/notepad4 at 89b8b36e0ee2c23f73be922315f17261ccfbbe1a](https://github.com/zufuliu/notepad4/blob/89b8b36e0ee2c23f73be922315f17261ccfbbe1a/scintilla/lexers/LexDiff.cxx).
The original author notice is retained in the source. The applicable Scintilla
permission notice is reproduced in `third_party/scintilla/License.txt`.
See `docs/NOTEPAD4_MIGRATION.md` for the exact adaptation and scope.

## wxWidgets and its embedded Scintilla

The application links to wxWidgets `core`, `base` and `stc`. The pinned source
build uses wxWidgets 3.2.12 under the wxWindows Library Licence; its `stc`
component embeds Scintilla under the Scintilla license. The build retrieves
upstream source separately; it is not vendored in this repository.

Packages created by the pinned build include the upstream license notices
under `licenses/` (or `share/doc/notepad-prototype/licenses/` on Linux), including
the bundled wx dependency notices. Distribution builds using a system wxWidgets
installation also depend on that distribution's library packages and notices.
