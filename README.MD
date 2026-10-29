# File Viewer for The Major BBS v10

**File Viewer** (`GALFV`) shows text files to your callers on The Major BBS
v10: plain text, ANSI art and IBM PC (code page 437) art, page by page or as
one continuous feed. Files are kept in a library of named documents that
callers pick from a list or reach directly from a menu link. Documents can be
uploaded or written online, and output can be held to the speed of an old
modem.

File Viewer was first written in 1995 and was ported to Worldgroup 3.2 by
Elwynor Technologies in 2006 (version 1.1, `ELWFV`). Version 1.2.0 is the
conversion for The Major BBS v10. It adds the document library, menu links,
uploads and online writing, continuous display and the simulated baud rate,
and it no longer needs an activation code.

## Features

- **Paged viewing.** Next, previous, first page, go to a page number, and the
  position shown as a percentage. Pages fit each caller's screen size.
- **Continuous display.** The whole document without pauses, from the start
  or from the current page. Enter stops it.
- **Keyword search.** Up to six words, forward or backward, whole words in
  any case. Enter finds the next match.
- **Text, ANSI and IBM files.** Text is word-wrapped. ANSI and IBM art is
  shown as drawn, and callers without ANSI get plain ASCII line drawing. The
  type can be chosen per document or detected automatically. A Ctrl-Z hides
  SAUCE records, and form feeds start new pages.
- **Simulated baud rate.** Output can be held to any rate from 110 to 115200
  baud. A default is set for the board, each document can override it, and
  callers can change it while reading (`B 2400`, `B OFF`).
- **Document library.** Named documents, each with a title, an optional key
  needed to view it, a display mode, a content type, a baud rate and a
  "hidden" flag.
- **Headers.** A banner, logo or title bar kept at the top of the screen
  while the document scrolls or pages beneath it. One header can be shared
  by any number of documents. ANSI or plain text, one line or most of the
  screen. Callers without ANSI see it at the top of each page.
- **File-only display.** A document can hide the viewer's own messages around
  a continuous display and return the caller straight to where they started
  when it ends. A menu link then shows nothing but the file, which suits ANSI
  screens and bulletins.
- **Menu links.** Put a document's name in a module page's command string to
  open it directly. A link to a document that does not exist yet offers to
  create it on the spot.
- **Upload or write documents.** Callers holding the add-documents key can
  upload a file with a batch protocol (ZMODEM, YMODEM), keeping its
  extension, or write one in the BBS editor. Sysops can also link a document to any file on the server.
- **Online settings.** Every setting is changed from inside the module while
  the board runs, takes effect immediately and is noted in the Audit Trail.
- **Fully editable text.** Everything callers see is in `GALFV.MSG`. Changing
  values are named text variables (`GALFV_DOC`, `GALFV_PAGE`, ...).

## Installation

1. With the BBS down, copy `GALFV.DLL`, `GALFV.MDF`, `GALFV.MSG` and
   `GALFV.RLN` from the release folder into your BBS directory.
2. Add File Viewer to your menu with the Menu Tree editor. Leave the command
   string empty for the library, or give a document name for a direct link.
3. Start the BBS. File Viewer creates `GALFVDOC.DAT`, `GALFVSET.DAT` and a
   `GALFV` folder for documents on first start.

**Upgrading from ELWFV 1.1:** with the BBS down, delete `ELWFV.DLL`,
`ELWFV.MDF`, `ELWFV.MSG` and `ELWFV.MCV`, install the `GALFV` files, and point
your menu pages at File Viewer again.

## Configuration

Enter File Viewer with the sysop key (`SYSOP` by default) and choose **S** at
the library prompt. The settings cover:

- the sysop key and the key to add documents
- the folder for uploaded documents
- the default display mode (paged, continuous or ask)
- the largest upload size
- the default simulated baud rate
- whether sysops may view any server file by path

There are no configuration editor options. A new install starts from
built-in defaults, and **R** on the settings page restores them. `GALFV.RLN`
(the sysop manual) describes every setting and document property.

## Usage

- **Callers:** pick a document by number or name, then use `N`, `P`, `T`, a
  page number, `C`, `K`, `B` and `X` at the page prompt.
- **Authors:** `A` adds a document (upload or write), `E` changes one you
  added and `D` deletes one.
- **Sysops:** everything authors can do, for every document, plus `F` to view
  any server file by path and `S` for the settings.

## Version Information

Current version: **1.3.3**. See [CHANGELOG.MD](CHANGELOG.MD) for changes and
[BUILDING.MD](BUILDING.MD) to build from source.

## License

File Viewer is licensed under the
[GNU Affero General Public License v3.0](LICENSE). See
[CONTRIBUTING.MD](CONTRIBUTING.MD) to contribute.

Copyright © Elwynor Technologies. Originally © High Velocity Software, Inc.

## Support

Please report problems and suggestions through the GitHub issue tracker for
this repository.

---

File Viewer converted for The Major BBS v10 by Mark Laudenbach - https://github.com/laudenbachm
