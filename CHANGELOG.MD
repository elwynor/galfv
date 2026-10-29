# Changelog

All notable changes to File Viewer are listed here. Versions follow
[Semantic Versioning](https://semver.org/).

## [1.3.4] - 2026-10-29

### Fixed
- With a header pinned at the top, MuffinTerm scrolled the header off the
  screen at each page prompt on a 24-line screen. The document's scrolling
  area now stops one row short of the bottom of the screen, which is left
  unused, so a page under a header is one line shorter.

## [1.3.3] - test build (not released)

### Fixed
- With a header pinned at the top, SyncTERM did not clear the previous
  page before showing the next one, so old text showed through. Its
  erase-to-end-of-screen command counts rows from the top of the scrolling
  area rather than the screen. The viewer now erases each row under the
  header itself. Other terminals (ZOC) were not affected.

## [1.3.2] - test build (not released)

### Fixed
- A menu link to a document that does not exist yet asked "Create it?",
  but answering Y went straight back to the menu. The answer was handed to
  the document editor's input routine, which did not know that question.
  It now reaches the library, and Y starts adding the document.
- An uploaded document was always saved as NAME.TXT, whatever its own
  extension (ABOUT.ANS became ABOUT.TXT). The upload offered only
  single-file protocols, which send no file name. It now offers the batch
  protocols (ZMODEM, YMODEM), which do, and the document keeps the
  extension of the file sent (TXT, ANS, ASC, NFO, DIZ, IBM, DOC, LOG or MD;
  anything else is still stored as .TXT). XMODEM and ASCII uploads are no
  longer offered: they cannot carry a name.

## [1.3.1] - test build (not released)

### Fixed
- ANSI headers came out scrambled: the BBS's own word wrap replaced spaces
  in the art with line breaks (seen in a capture of a board's output). The
  viewer now turns the BBS word wrap off while a document is open, as the
  BBS does itself when it shows an .ANS menu file, and restores it on
  leaving.
- The BBS's "(N)onstop, (Q)uit, or (C)ontinue?" pause could appear inside
  the viewer (seen during a keyword search): the BBS turns it back on
  between input cycles. The viewer now turns it off again on every input
  line and polling call, not only when the document opens.

## [1.3.0] - test build (not released)

### Added
- **Headers**: files shown at the top of documents, shared by any number of
  documents. H at the library manages them (add by upload, writing or
  link; change; delete). A document's new **Header** property names one.
  - ANSI callers: the header is drawn once and stays on screen while the
    document pages or scrolls beneath it (a terminal scrolling region).
  - Callers without ANSI: the header tops every page, and is shown once
    before a continuous display.
  - A header too tall to leave 5 document lines is shown once and then
    scrolls away.
  - The height is counted from the file, or set by hand (1-99) for ANSI art
    that moves the cursor.
- Document record layout 3 (`kind`, `header`, `rows`). Existing documents
  read as documents without a header.
- VIEWTEST covers the header modes.

## [1.2.3] - test build (not released)

### Fixed
- C (continuous) at a page prompt started over from the top of the page on
  screen, so on page 1 the whole file was sent again. It now carries on
  from just after that page.

## [1.2.2] - test build (not released)

### Added
- Document property **When finished**: `WAIT` (the end-of-document prompt,
  as before) or `RETURN`. With RETURN, a continuous display ends by going
  straight back to where the caller started (the menu, for a direct link)
  with no "Press Enter".
- Document property **Continuous messages**: `SHOW` (as before) or `HIDE`.
  HIDE leaves out the "(Continuous display - press Enter to stop)" header
  and the "Stopped." line, so with RETURN the caller sees only the file.
- Document record layout 2. Existing documents keep their 1.2.0 behaviour
  (both new fields read as zero).

## [1.2.1] - test build (not released)

### Fixed
- Page prompts and the "Searching..." line were sometimes shown twice. The
  engine's `outprf()` does not empty its output buffer, so a second send
  with nothing new in between repeated the last text. Every send now empties
  the buffer afterwards.

### Added
- `TESTS/VIEWTEST.C`: runs the viewer against a stand-in engine and checks
  that each prompt is sent exactly once.

## [1.2.0] - test build (not released)

The conversion of File Viewer 1.1 (`ELWFV`, Worldgroup 3.2) to The Major BBS
v10.

### Added
- Document library (`GALFVDOC.DAT`): named documents with a title, a key
  needed to view, display mode, content type, simulated baud rate, a hidden
  flag, owner, dates and a view count.
- Menu links: a module page's command string names the document to open.
  A link to a missing document offers to create it.
- Adding documents by upload (any protocol the board offers), by writing in
  the BBS editor, or (sysops) by linking a server file. Authors manage the
  documents they added; sysops manage all of them.
- Continuous display, from the start or from the current page.
- Simulated baud rate (110 to 115200), as a setting, per document, and with
  `B` while reading.
- Content types TEXT, ANSI and IBM, with automatic detection. ANSI and IBM
  line drawing is shown as plain ASCII to callers without ANSI. Ctrl-Z ends
  a file, and form feeds start a page.
- Go to a page by number, `T` for the first page, and the position shown as
  a percentage.
- Online settings editor (`GALFVSET.DAT`) with changes noted in the Audit
  Trail.
- Named text variables (`GALFV_*`) in every message.
- Windows version resource in the DLL, and the version shown in the Audit
  Trail at startup.

### Changed
- Module files renamed from `ELWFV` to `GALFV`.
- Built with Visual Studio 2022 against the MBBS v10 Module SDK.
- The page builder was rewritten. Pages are sized from the caller's screen
  settings, and prompts follow the page instead of being drawn at fixed row
  23. Word wrap no longer adds a space at the start of each line of a CR LF
  file.
- Ordinary callers can open only library documents. Viewing a file by path
  is limited to holders of the sysop key, and can be turned off.
- Up to 2000 pages can be tracked (was 1100).

### Removed
- The activation code and 14-day demo, including the `ACTCOD` option in the
  configuration editor.

### Fixed
- A `%` in a file could be read as a format code while displaying it.
- A file that failed to open was closed anyway (a null file handle).
- Hanging up could close another caller's file instead of your own.
- Leaving the viewer toggled the caller's page-blocking and global-command
  flags instead of restoring them.
