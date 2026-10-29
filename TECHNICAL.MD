# File Viewer - Technical Notes

How File Viewer 1.3.3 (`GALFV`) works inside The Major BBS v10.

## Source map

| File | Purpose |
|------|---------|
| `GALFV.C` | `init__galfv`, module registration, input routing by state, hang-up, shutdown, the page/global-command guard |
| `FVLIB.C` | Arriving from a menu (command strings), the library list and prompt, permissions |
| `FVVIEW.C` | The viewer: output queue, baud simulation, polling, paged and continuous modes, search, page prompt |
| `FVPAGE.C` | The page builder, content-type detection and keyword matcher (no engine calls, so it can be unit tested) |
| `FVEDIT.C` | Adding, changing and deleting documents, upload handler, online editor |
| `FVSYSOP.C` | The online settings editor |
| `FVSET.C/.H` | The settings record and its table of defaults |
| `FVDOC.C/.H` | The document library file |
| `FVTVARS.C` | Named text variables |
| `FVDEFS.H` | Version, user states, the per-user work area, prototypes |
| `GALFV.MST` | Message template, compiled to `GALFV.MSG` and `GALFV.H` by `TOOLS/MKMSG.PY` |

## Entry and command strings

A module page reaches the module through the engine's `entmdl()`, which puts
`<select char><command string> <rest of the user's typed input>` in
`input[]` and runs `parsin()` (WG33 `MENUING.C`, `entmdl`). On the first call
(`substt == 0`) `lib_entry()` calls `rstrin()`, skips the select character,
and takes the first word:

1. A document name: open it. The user returns to the menu afterwards
   (`home = FVBACK_EXIT`).
2. A valid but unknown name, and the user may add documents: offer to create
   it (`ST_CREATEQ`).
3. A sysop, with `rawpath` on, and the whole string is an existing file: view
   that file.
4. Otherwise, show the library.

The engine cannot tell a page's command string from text the caller typed
after the selection, so paths are honoured only for sysops.

## States

`usrptr->substt` drives everything (`FVDEFS.H`). `fvinput()` routes:
1-11 are the library, the viewer and the header section (`FVLIB.C`,
`FVVIEW.C`), 20-31 the document dialogs (`FVEDIT.C`), and 40 and up the
settings (`FVSYSOP.C`). Whenever the
input routine returns FALSE, `vw_close()` runs first, so a file is never left
open.

## The viewer

### Page builder (`pg_build`)

`pg_build()` reads the open file from a byte offset and writes screen output
into the work area's `out[]` (4096 bytes). It stops after N screen lines, at
a form feed (in paged mode), or when the buffer is nearly full. It returns
the offset where the next part starts and whether only blank lines remain.

- CR, LF and CR LF each end one line. Output uses `\n` only, because the
  engine expands it to a newline.
- TEXT is word-wrapped at `min(scnwid, 80) - 2` columns. When a page fills
  in the middle of a wrapped line, the next page starts at the carried word
  (the offset just after the break space is recorded as it is written).
- ANSI and IBM are not wrapped. Columns are counted to the terminal width
  with VT100 pending-wrap rules, so an 80-column line followed by CR LF is
  one screen line.
- `ESC [ ... final` sequences take no width. They are kept for ANSI callers
  on ANSI documents and dropped otherwise.
- Callers without ANSI get bytes 128-255 mapped to ASCII look-alikes on ANSI
  and IBM documents (the `cp437[]` table).
- Ctrl-Z ends the file (SAUCE). Other control characters, including NUL and
  0x01 (the engine's text-variable marker), are dropped. Tabs expand to
  8-column stops.

Pages are `scnbrk - 2` lines (24 if the user's setting is unusable), which
leaves room for the prompt. `ppos[]` records the start of every page seen so
far (up to 2000 pages), so Previous and go-to-page simply seek.

### Output queue and polling

All sending goes through `fv_out()`, which is `outprf()` followed by
`clrprf()`. The engine's `outprf()` sends `prfbuf` and rewinds the write
pointer but does not empty the buffer (`outprf` in the v10 `WGSERVER.EXE`
has no terminating store; `clrprf` has one). A second `outprf()` with nothing
new written in between would re-send the last text; 1.2.0 showed prompts
twice because of this.

Output never goes straight to the channel. `queue()` marks `out[]` as
pending, and `qsend()` sends at most 512 bytes at a time while
`btuoba()` leaves at least 96 bytes free for prompts. Whatever is left is
sent by a polling routine (`begin_polling`). The routine stops itself when
the queue is empty, when the file is closed, or when the user is no longer
in the module (for example, a sysop moved them). Nothing ever waits, because
every channel runs on one thread.

While viewing, `fv_guard()` sets `NOINJO|NOGLOB` (pages from others and
global commands would break the screen or move the user away with the file
open) and saves the user's own bits. On close they are restored, not toggled.
`btuxnf(usrnum,0,19)` turns off the engine's page pause while viewing, and
`rstrxf()` restores it.

### Baud simulation

When a rate is set, `qsend()` meters output by credit. Each call adds
`elapsed_ms x baud / 10` thousandths of a character (8N1: 10 bits per
character), with the gap capped at one second and the stored credit at a
quarter second's worth. It sends at most that many whole characters. Time
comes from `GetTickCount()`. The rate resolves as: the document's own rate
if set (-1 means full speed), else the `defbaud` setting (0 means full
speed). `B` changes it for the current session only.

### Finishing a continuous display

The feed ends inside the polling routine, which cannot take the user out of
the module. With `FVO_RETURN` it sets `ST_DONE` and calls
`btuinj(usrnum, CRSTG)`. The engine then reads an (empty) input line and
calls the input routine (WG33 `MAJORBBS.C`, `CRSTG` -> `hdlcri`), which leaves,
and a FALSE return goes to the menu. The engine's file transfer uses the
same injection (`FILEXFER.C` `fupfin`). `FVO_QUIET` suppresses `FEEDBGN`
and `FEEDSTOP`.

### Headers

A document whose `header` names a header record (`kind` 1) is opened with a
`struct fvhead` (path, type, height). `vw_open()` measures the header by
building it with `pg_build()` (no line limit) in chunks of `FV_PGBUF` and
summing `pg_lines`, plus one if the last line has no ending (`pg_part`).
A height set by hand (`rows`, 1-99) is used instead; ANSI that moves the
cursor cannot be counted. A header that does not finish within
`FV_HMAXCHK` chunks counts as too tall. Then, with `rows` = `scnbrk`:

| Mode | When | Shown |
|------|------|-------|
| `FVH_PIN` | ANSI, `rows - h - 2 >= FV_HMINDOC` | Drawn once after `PGCLS`. Then `ESC[h+1;rows r` (DECSTBM) and the cursor to row h+1. Pages clear only the region, each row bottom up (`ESC[r;1H ESC[K`), never `ESC[J`, which SyncTERM applies relative to the region's top margin; a feed scrolls inside it. |
| `FVH_EACH` | no ANSI, same room | `PGSEP`, header, page, on every page; once before a feed. |
| `FVH_ONCE` | less room | Once before the first page or the feed, then pages as normal. |

`PIN` and `EACH` take `h` lines out of every page (`V->lines`), so page
offsets and search agree with what is shown. The header goes through the
same queue as a page (`FVAFT_HDR` chunks, then `pagebody()` or
`feedstart()`), so the baud simulation applies to it. `pg_build()` reads the
channel's file and the viewer's type, so `hbuild()` swaps in the header's
for the call; the header file is opened for each chunk and never stays
open.

While a region is set, `pinrows[usrnum]` holds the screen rows (outside the
work area, which another module may reuse). `vw_close()` resets the region
(`ESC[r`) and puts the cursor on the bottom row; the polling routine does
the same if the engine moved the user elsewhere. Hang-up just clears it.

### Search

Search runs from the polling routine, 25 pages per cycle. Each page is built
into `out[]` and scanned by `pg_match()`, which skips escape sequences and
compares whole alphanumeric words case-insensitively. A match shows that
page with `KWFOUND`. Input during a search cancels it.

## Data files

Both files are Btrieve files created with `dfaCreateSpec` on a 1024-byte page
with one key, a 16-byte ZSTRING at offset 0. Records are 512 bytes,
`#pragma pack(8)`, and the size is checked at compile time. Every lookup key
is first copied into a writable local buffer: the engine reads keys only
from writable memory.

### `GALFVDOC.DAT` - `struct fvdoc` (`FVDOC.H`)

| Field | Meaning |
|-------|---------|
| `name[16]` | key 0, upper case, 2-15 of `A-Z 0-9 - _` |
| `layout` | `FVD_LAYOUT` (3; layout 1 and 2 records read correctly) |
| `title[61]` | shown in the list and prompts |
| `file[128]` | a plain file name in the document folder, or a full path if `linked` |
| `key[16]` | key needed to view ("" = anyone) |
| `mode` | 0 default, 1 paged, 2 continuous, 3 ask |
| `type` | 0 auto, 1 text, 2 ANSI, 3 IBM |
| `hidden` | 1 = not in the list |
| `linked` | 1 = `file` is a sysop-linked path (never deleted by the module) |
| `owner[30]` | user-id that added it |
| `created`, `changed` | packed dates (`today()`) |
| `views` | times opened |
| `baud` | 0 default, -1 full speed, else 110-115200 |
| `quiet` | 1 = hide the continuous-display header and "Stopped." line (layout 2) |
| `autoret` | 1 = when a continuous display ends, return straight to the caller's start, no end prompt (layout 2) |
| `kind` | 0 document, 1 header (layout 3). Headers are never listed or opened as documents, and share the name space |
| `header[16]` | document: the name of its header, "" = none (layout 3) |
| `rows` | header: height in lines set by hand, 0 = count it (layout 3) |
| `spare[212]` | room for new fields |

### `GALFVSET.DAT` - `struct fvsetrec` (`FVSET.H`)

One record with the key `SETTINGS`: `cfgkey`, `addkey`, `docdir`, `defmode`,
`maxkb`, `defbaud` and `rawpath`. The defaults live in `setdefs[]` in
`FVSET.C`. They seed a new install and back **Reset**. An empty sysop key is
refused, so not every caller becomes a sysop.

## Uploads

`fileup("", "", fvupl)` hands the user to the engine's file transfer, which
calls `fvupl()` at each step (WG33 `FILEXFER.C`: `fileup`, `ftfrex`,
`ftfrop`, `ftfrcl`, `fupfin`):

- `FUPBEG` points the transfer at `<docdir>\<NAME>.UP$`, sets `maxbyt` from
  `maxkb`, and refuses a second file in a batch.
- `FUPEND` renames the temporary file to `<NAME>.<EXT>`. The extension is
  kept if it is TXT, ANS, ASC, NFO, DIZ, IBM, DOC, LOG or MD, and becomes
  TXT otherwise. The extension comes from the name the sender's protocol
  sends, which is why no proposed name is passed to `fileup()`: with one,
  the engine offers only single-file protocols (XMODEM, ASCII), which send
  no name, and the proposed name would be used instead.
- `FUPSKP`/`FUPHUP` delete the temporary file.
- `FUPFIN` puts the user back in the module (`usrptr->state = fvstt`),
  saves the record and shows the property menu, then returns 1.

A failed upload never touches a document's existing file. A replaced file is
deleted only once the new one is in place.

## Online editor

`bgnedt(FV_EDTSIZ, buf, 0, NULL, edone, 0)` with an 8192-byte buffer that
shares the work area with the viewer (they are never used at once). The
editor separates lines with `\r`; files on disk use CR LF. Linked files and
files too large for the buffer are not editable online.

## Text variables

`GALFV_DOC`, `TITLE`, `PAGE`, `PCT`, `NUM`, `ITEM`, `LABEL`, `VALUE`,
`LIMIT`, `FILE`, `WORD`, `COUNT`, `OWNER`,
`BAUD` and `VERSION`. Values are returned with `%` doubled, because
`prfmsg()` runs the expanded text through `vsprintf()`.

## Audit Trail entries

`FILE VIEWER v1.3.3` at startup, `GALFV SETTINGS CREATED`, `GALFV SETTING
CHANGED`, `GALFV SETTINGS RESET`, `GALFV DOCUMENT ADDED`, `GALFV DOCUMENT
LINKED`, `GALFV DOCUMENT DELETED` and `GALFV SERVER FILE VIEWED`.
