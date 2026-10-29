# Building File Viewer

File Viewer is a Win32 DLL module for The Major BBS v10. It is built with
Microsoft Visual Studio against the MBBS v10 Module SDK.

## Prerequisites

- **Microsoft Visual Studio 2022** (or Build Tools 2022) with the C++
  desktop workload. Platform toolset **v143**.
- **Windows 10 SDK** (`WindowsTargetPlatformVersion` 10.0).
- **The Major BBS v10 Module SDK**
  (https://github.com/TheMajorBBS/MBBS-v10-module-SDK). It provides the
  headers (`inc/`) and import libraries (`lib/importlibs`, `lib/v10`).
- **Python 3**, for the message and test scripts.

> **Build for x86 (Win32) only. Never x64.** The Major BBS v10 is a 32-bit
> program and cannot load a 64-bit module.

## Setup

Copy `MBBSSDK.props.example` to `MBBSSDK.props` and set `MbbsSdkDir` to your
SDK folder (trailing backslash required). `MBBSSDK.props` is not committed.

## Message file

The module's text is written in `SRC/GALFV/GALFV.MST`, a readable template
in which ANSI escapes are spelled `<ESC>` and named text variables
`<TV:NAME>` (or `<TV:NAME:J:W>` to justify to a width). Generate the real
message file and its header with:

```
python TOOLS/MKMSG.PY
```

This writes `SRC/GALFV/GALFV.MSG` (CRLF line endings, raw ESC and 0x01
bytes) and `SRC/GALFV/GALFV.H` (message numbers). Never edit those two by
hand. Message names are at most 8 characters, a literal `%` is written `%%`,
and the file has a single `LEVEL6` block. Rebuild the DLL whenever the
message numbers change, and deploy the DLL and `.MSG` together. The BBS
compiles `GALFV.MCV` from the `.MSG` itself.

## Build

In Visual Studio, open `GALFV.sln`, choose **Release** / **Win32**, and
build. Or from a command prompt:

```
MSBuild.exe GALFV.sln -p:Configuration=Release -p:Platform=Win32
```

The DLL is written to `BUILD/Release/GALFV.dll`.

## Tests

Two stand-alone test programs need no BBS. `TESTS/PAGETEST.C` tests the
page builder (`SRC/GALFV/FVPAGE.C`). `TESTS/VIEWTEST.C` runs the viewer
(`FVVIEW.C`) against a small stand-in for the engine and checks what the
caller is sent. Build and run both with:

```
python TOOLS/RUNTESTS.PY
```

The script prints the number of checks and failures, and exits non-zero if
any check fails. The rest of the module is tested by hand on a test board.

## Packaging a release

1. Set the new version in `SRC/GALFV/FVDEFS.H` (`GALFV_VER_*`,
   `GALFV_VERSION`) and `SRC/GALFV/GALFV.rc`, and update `CHANGELOG.MD`,
   `README.MD` and `GALFV.RLN`.
2. Run `python TOOLS/MKDIST.PY`. It regenerates the message file, rebuilds
   the Release configuration, checks that the DLL's version matches
   `GALFV_VERSION`, and copies the deployable files into `DIST/<version>/`
   with a `GALFV-<version>.zip`.

`MKDIST.PY` refuses to write into a `DIST/<version>/` folder that already
exists. Never rebuild under a version number that has already been shared.
Bump the version instead.

## Release checklist

- Project builds with no errors or warnings, and the tests pass.
- Version numbers updated (`FVDEFS.H`, `GALFV.rc`, `CHANGELOG.MD`).
- `README.MD` and `GALFV.RLN` updated.
- `DIST/<version>/` holds only `GALFV.DLL`, `GALFV.MDF`, `GALFV.MSG` and
  `GALFV.RLN`, with CRLF line endings in the text files.
- DLL properties show the right product, version, company and copyright.
- Installation tested on a test board.
- No credentials, internal paths or internal notes in published files.
