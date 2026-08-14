# URmessage Windows scaffold — checkpoint 1 report

**Date:** 2026-08-13
**Repo:** `C:\Users\ryanm\Downloads\claude_sandbox_message\message-windows`
**Brand/shell source (read-only, never written to):**
`C:\Users\ryanm\Downloads\claude_sandbox_windows`, `beta/algorithm-dpi` @ `eacf4de`
**Contract:** `claude_sandbox_message\msgrepo\docs\specs\2026-08-13-windows-client-scaffold-brief.md`

Throughout this report, **[RAN]** means a command was executed and the quoted
output is its real output. **[TRACED]** means it was worked out by reading the
source and was *not* executed. They are never mixed.

---

## 1. Does it build?

**Yes.** Clean from an empty tree, x64 Release, 0 errors.

**[RAN]** After deleting `app\build`, `app\src\App\x64`, `app\src\Common\x64`
and the generated `XamlMetaDataProvider.*`:

```
toolset  : v145 (MSVC 14.50.35717)
win sdk  : 10.0.26100.0
building : x64 Release
...
    12 Warning(s)
    0 Error(s)
Time Elapsed 00:00:40.53
OK in 41s -> ...\app\build\x64\Release\
```

**[RAN]** x64 **Debug** also builds: `0 Error(s)`, `OK in 39s`.

**[RAN]** **ARM64 does not build on this box**, and it is an environment gap
rather than a code problem:

```
Microsoft.CppBuild.targets(473,5): error MSB8020: The build tools for v145
(Platform Toolset = 'v145') cannot be found.
```

VS18 Build Tools here has the v145 **x64** toolset only; no ARM64 cross tools
are installed. The `.sln` still declares ARM64 configurations. This is the same
shape as the brief's CI note (`windows-latest` moved to VS18/v180 and lost the
v143 ARM64 cross tools) and is unresolved for the same reason — nobody has
installed the component. **ARM64 is unverified, not known-good.**

Warnings, all inherited from the VPN sources and all benign:

- `C4005 'WIN32_LEAN_AND_MEAN': macro redefinition` — `Directory.Build.props`
  defines it globally and `pch.h` / `Log.cpp` / `Strings.cpp` / `Paths.cpp`
  define it again. Identical in the VPN repo; the sources note it.
- `C4651 '/DMICROSOFT_WINDOWSAPPSDK_SELFCONTAINED=1' specified for precompiled
  header but not for current compile` — on the generated `*.g.cpp` units, which
  the hand-written `UrmCompileGeneratedXamlImpl` target adds to `@(ClCompile)`
  after the SDK targets set that define. Cosmetic.
- `CS1668 Invalid search path ... in 'LIB environment variable'` — Roslyn's
  inline-task compiler complaining about `atlmfc` paths that VS18 Build Tools
  does not lay down. Nothing to do with this project.

## 2. Does it launch and render?

**Yes**, separately verified.

**[RAN]** Headless smoke test first, `URmessage.exe --diagnose`, exit code 0:

```
URmessage startup diagnostics
  build            : x64 (Aug 13 2026 21:16:58)
  windows          : 10.0.26200
  executable       : ...\app\build\x64\Release\URmessage.exe
  log file         : ...\.localstate-verify\logs\urmessage-app.log
  storage root     : ...\.localstate-verify
  single-inst key  : URmessage.Desktop
  built against    : Windows App SDK 2.2.0
  app runtime      : present: ...\app\build\x64\Release\Microsoft.WindowsAppRuntime.dll
  resources.pri    : present (1378200 bytes)
  fonts            : present (197556 bytes)
  resources (mrt)  : resolving (app_name -> "URmessage")
  app instance     : no instance holds the key — the app is not running
```

The two lines that matter: the App Runtime resolves to the **app-local**
self-contained copy, and MRT actually **resolves** `app_name` rather than the
UI rendering its own key ids.

**[RAN]** GUI launch via `app\tools\verify-render.ps1`:

```
class name  : WinUIDesktopWin32WindowClass
window text : URmessage
window rect : 600x950 at (980,318)  [PHYSICAL pixels, harness is PerMonitorV2]
window dpi  : 120  (scale 1.25)
dips        : 480x760
```

**480×760 DIPs** — the compact default, not the screen. The app's own log agrees
independently: `shell: no saved placement - compact default 600x950 centred at
(980,318) (dpi scale 1.25)`.

**[RAN]** Single-instance, with the new key: a second launch of the same exe
exited 0 without opening a second window, and exactly one process remained.

```
running instances before: 1
second launch exited: True  exit code: 0
running instances after : 1
startup: single instance key 'URmessage.Desktop': this process is a second launch
startup: second launch exiting (redirected)
```

The VPN client was never launched, never signalled, and its repo was never
written to. `urnetworkd` was not touched.

## 3. Screenshots, and what is actually in them

| File | What it is |
|---|---|
| `.verify\urmessage-window.png` | 480×760 dip default, `PrintWindow(PW_RENDERFULLCONTENT)` |
| `.verify\urmessage-window-screen.png` | the same window, `CopyFromScreen` |
| `.verify\urmessage-window-wide.png` | resized to 1200×800 dip, past the breakpoint |
| `.verify\urmessage-wordmark-4x.png` | the title-bar wordmark at 4× nearest-neighbour |
| `.verify\font-reference.png` | the same string in all four brand faces + Segoe UI |

**[RAN]** What I see in the default capture: a near-black window; the URnetwork
globe icon and the word **URmessage** in the title bar; a hamburger toggle; the
page title **Chats** in a very heavy extended display face; a
**CONVERSATIONS** header strip with `10` right-aligned; a search row with a
magnifier glyph and *Search conversations*; a **RECENT** group header; and ten
uniform two-line rows, each with a right-aligned time and a chevron, separated
by hairlines. No rounded islands, no page margins, no shadows — the pane model.

**[RAN]** In the wide capture: the NavigationView pane expanded to labelled
items (Chats, Contacts, and Settings pinned to the footer) with a lime selection
indicator on Chats; the conversation list as a 320dip column; a 1px vertical
rule; and the thread pane with *Select a conversation to read it.* centred in it.

### Colours, measured

**[RAN]** Pixel probes, physical px from the window's top-left:

| probe | PrintWindow | screen | expected |
|---|---|---|---|
| page, 75%/75% | `#101010` | `#202020` | `#101010` |
| pane header strip | `#151515` | `#0C0C0C` | `#151515` |
| group header strip | `#151515` | `#202020` | `#151515` |
| title bar (4,4) | `#000000` | `#313131` | — |

The page is `#101010` and the header strips are `#151515` — the brand page and
the sheet one Oklab step above it. Two caveats, both stated rather than papered
over:

- The **`title bar (4,4)` `#000000` is a probe artifact, not a colour.** That
  coordinate falls in the DWM rounded-corner / border region, which PrintWindow
  leaves untouched (a fresh `Bitmap` reads as `000000` there). The title bar
  interior is `#101010` in the image.
- **The screen column is dimmed and should not be read as colour.** **[RAN]**
  The harness detected **23 `Shell_SystemDialogProxy` windows** on screen — a
  stack of Windows Firewall prompts belonging to an unrelated process
  (`extender.test.exe`), which dim the whole desktop. They are the user's
  dialogs; I did not click, dismiss or answer any of them. That is why
  `CopyFromScreen` reads `#101010` as `#202020`.

  Per the brief's trap 7, PrintWindow is normally the untrustworthy capture. Its
  one blind spot is a **system backdrop**, which it never composites — and this
  app deliberately has none, so for this window PrintWindow is faithful. The two
  captures agree on every element and differ only by the uniform dim. Had this
  been a backdrop change, neither capture would have been admissible and I would
  have said so.

### The font check, which cannot be done by eye

The brief says to confirm "the wordmark is in PP NeueBit and not a fallback
face". **Looking at it does not answer that**, and my first read of the
screenshot was wrong: I judged the wordmark a fallback because it did not look
like a bitmap font. PP NeueBit is not pixelated at UI sizes.

**[RAN]** Family names read out of the four shipped files with
`System.Drawing.Text.PrivateFontCollection` — these are the `name` table id 1
strings that must appear after `#` in `App.xaml`:

```
abcgravity_extended.otf            -> 'ABC Gravity Extended'
abcgravity_extra_condensed.otf     -> 'ABC Gravity Extra Condensed'
pp_neue_bit_bold.ttf               -> 'PP NeueBit'
pp_neue_montreal_regular.ttf       -> 'PP Neue Montreal'
```

All four match `App.xaml` exactly.

**[RAN]** Then the decisive test — the rendered ink box of "URmessage" at the
same 30px em (24dip at 125%), measured out of the capture and out of a reference
render of each candidate:

| face | ink w × h |
|---|---|
| **as the app drew it** | **111 × 19** |
| PP NeueBit | 114 × 19 |
| PP Neue Montreal | 164 × 27 |
| ABC Gravity Extended | 219 × 28 |
| Segoe UI Bold — *the silent-fallback face* | 160 × 28 |

The wordmark is PP NeueBit. The 3% width gap is GDI+ vs DirectWrite spacing;
every wrong answer is off by 40%+, so this cannot pass by accident. The method
is written into `app\src\App\Assets\README.md` so the next person does not have
to rediscover it.

**[RAN]** The `Chats` page title matches the ABC Gravity Extended line of
`font-reference.png` by shape. Not measured — **[TRACED]** it inherits
`UrTitleTextStyle` from the verbatim-copied `App.xaml`.

## 4. Where the brief was wrong or incomplete

The brief is accurate on the things it emphasises. Everything below is either an
error inherited from the source's own comments, or a gap.

### 4.1 The identity list is incomplete — three more collisions

The brief's `Ids.h` table names five values. **[TRACED]** by reading the sources
I kept, three more identities are hard-coded to `URnetwork` in files the brief
lists as "copy essentially unchanged":

| Where | VPN value | Why it collides |
|---|---|---|
| `WindowShell.cpp:36` | `HKCU\Software\URnetwork\Window` | the placement blob carries a magic + version, so a second app reading the same key would **accept** it: same magic, same version, wrong window |
| `Common/Paths.cpp:52,60` | `%LOCALAPPDATA%\URnetwork\app`, `URNETWORK_APP_ROOT` | two unsynchronised writers on one prefs file and one log |
| `Startup.cpp:168` | `urnetwork-app.log` | same |

The brief calls `WindowShell.*` "the cleanest reusable file in the repo — zero
VPN awareness". 382 lines is right and the *behaviour* is VPN-free, but the
registry path is not. All three are now routed through `Ids.h` in this repo, so
the collision question has one file to read. **[RAN]** confirmed live: the
diagnose output shows the URmessage storage root, and the app logs
`shell: no saved placement` on first run — it did not find the VPN's.

### 4.2 `App.vcxproj`'s own comment about `App.g.cpp` is false

The VPN's `UrnCompileGeneratedXamlImpl` comment states: *"App.g.cpp<-App.xaml.cpp
(we added the missing `#if __has_include("App.g.cpp")` there, matching
MainWindow.xaml.cpp)"*. **[RAN]** `grep -n 'App.g.cpp' App.xaml.cpp` in the
source finds only prose, no include — and **[TRACED]** it must not be there: the
same comment, three lines earlier, explains that cppwinrt emits no
`factory_implementation::App` for an `Application` subclass, so `App.g.cpp`
references symbols `App.g.h` never declares. I wrote the include in, then removed
it before building; the note is now in this repo's `App.xaml.cpp`.

The brief reproduces the three-target list faithfully and so inherits this.

### 4.3 An XML comment cannot contain `--`

Not a brief error, but the brief tells you to copy the vcxproj comments
verbatim, and those comments discuss `--diagnose`. **[RAN]** The first build
failed hard:

```
App.vcxproj(16,11): error MSB4025: The project file could not be loaded.
An XML comment cannot contain '--', and '-' cannot be the last character.
```

### 4.4 `Startup.*` needs more removed than `ServicePipeProbe`

**[TRACED]** `Startup.cpp` also carries `PreviewUiDestination` (~45 lines whose
destination tags are the VPN NavigationView's: connect / wallet / leaderboard /
seedphrase) and a `URnetworkSdk.dll` probe in `CollectDiagnostics`. Both had to
go with the pipe probe.

### 4.5 The copy list omits a hard dependency

**[TRACED]** `Common/AppPrefs.h` opens with `#include <nlohmann/json.hpp>`,
resolved through `Directory.Build.props`'s
`third_party\vendor-include`. The brief lists `AppPrefs` as copy-clean but does
not list the vendored header, and the VPN repo **gitignores**
`third_party/vendor-include/`, so a fresh clone of the source does not contain
it either. `nlohmann/json.hpp` (932 KB) is copied and committed here.

### 4.6 `UrComponents` needs an adapter that the brief does not mention

The brief is right that `MakePaneSearchRow` + `MakePaneTwoLineRowButton` "are
almost literally a conversation list". **[RAN]** They are not directly callable
with localized strings:

```
MainWindow.xaml.cpp(131,24): error C2664: 'urnw::kit::PaneSearchRow
urnw::kit::MakePaneSearchRow(const winrt::hstring &)': cannot convert argument 1
from 'std::wstring' to 'const winrt::hstring &'
```

`Localized()` returns `std::wstring`; the kit takes `winrt::hstring const&`, not
`winrt::param::hstring`, so there is no implicit conversion (unlike
`TextBlock().Text()`, which takes the param type and works). One named `Loc()`
adapter in `MainWindow.xaml.cpp`.

### 4.7 `app.ico` is the wrong product's mark, and the brief does not flag it

The brief excludes the 8 VPN tray icons from the `Assets/**` copy but not
`app.ico`. **[RAN]** It is visibly the URnetwork globe in the title bar, the
taskbar and alt-tab of a product called URmessage. Kept as the only buildable
placeholder and flagged loudly in `Assets/README.md`.

### 4.8 Self-contained makes one inherited diagnostic cry wolf

**[RAN]** With `WindowsAppSDKSelfContained=true` the bootstrapper is never used,
so the VPN's `LoadedModule(kBootstrapDll, ...)` line printed
`NOT LOADED (unexpected for a load-time import)` on a perfectly healthy launch.
**[TRACED]** the VPN client sets the same property, so the same false alarm is
presumably in its logs. Reworded here to
`not loaded (expected: this build is self-contained)`.

### 4.9 Minor

- `build-local.ps1`'s own header documents `pwsh -File ...`; **[RAN]** `pwsh`
  does not exist on this box. Rewritten for `powershell`.
- `SetForegroundWindow` is refused from a script that does not own the
  foreground, so the harness reports `foreground : NO`. Irrelevant to colour
  here (no backdrop, so nothing is focus-dependent), and `HWND_TOPMOST` is what
  actually guarantees an unoccluded capture.
- `AlwaysShowHeader="False"` (copied from the VPN) means the ABC Gravity page
  title is shown in narrow/compact modes and hidden once the pane expands.
  Native NavigationView behaviour, visible in the two screenshots, called out
  here so it is not later mistaken for a regression.

## 5. Copied vs written fresh

**Copied byte-for-byte** (only the noted edits):

| File | Edit |
|---|---|
| `App.xaml` (1122 lines) | `x:Class` → `URmessage.App`, header comment |
| `UrColors.h`, `UrComponents.{h,cpp}`, `UrMotion.{h,cpp}`, `WindowReveal.{h,cpp}`, `Localization.{h,cpp}`, `pch.{h,cpp}` | none |
| `WindowShell.{h,cpp}` | placement subkey → `Ids.h` |
| `Assets/Fonts/*.otf,*.ttf`, `Assets/app.ico`, `Assets/.gitattributes` | none; **[RAN]** byte-identical to source and to the build output |
| Common `Log.*`, `Strings.*`, `ThreadGuard.*`, `AppPrefs.*`, `Version.h`, `VersionGrammar.h` | none |
| Common `CrashDumps.{h,cpp}` | dump-folder path, `service:`→`app:`, the Go/cgo rationale |
| `third_party/vendor-include/nlohmann/json.hpp` | none |

The namespace stays `urnw` everywhere, deliberately: it is what lets those files
be literal copies, so a future diff against the VPN repo shows only what
URmessage changed.

**Written fresh:** `Common/Ids.h`, `Common/Paths.{h,cpp}`, `Common/Common.vcxproj`,
`App/Ids`-consuming `Startup.{h,cpp}`, `main.cpp`, `App.xaml.{h,cpp}`,
`MainWindow.xaml{,.h,.cpp}`, `App.idl`, `MainWindow.idl`, `App.rc`, `resource.h`,
`app.manifest`, `Strings/en/Resources.resw` (16 keys, not 1206),
`Directory.Build.props`, `App.vcxproj`, `URmessage.sln`, `tools/build-local.ps1`,
`tools/verify-render.ps1`, `.gitattributes`, `.gitignore`, `README.md`.

`MainWindow.xaml*` and `AppController.*` were **not** copied, as instructed.
There is no `AppController` at all: `App::OnLaunched` creates the window, applies
the native shell and activates it directly.

## 6. Left as stubs — read this before assuming anything works

- **No messaging.** No protocol, no store, no network, no account, no
  cryptography. The conversation list is 10 hard-coded rows in one
  `kSampleConversations` table at the top of `MainWindow.xaml.cpp`, named
  "Sample conversation N" precisely so a screenshot cannot be mistaken for a
  working messenger.
- **The search box filters nothing.** It is a real `TextBox` with no handler.
- **Rows are not clickable.** They are `Button`s (so they are focusable and
  hover correctly) with no `Click` handler; selecting one does not open a thread.
- **Contacts and Settings** are one line of text each.
- **No tray icon**, no `TrayIcon.cpp`, no hide-to-tray. `Ids.h` reserves the GUID
  and window class for when it lands.
- **Window placement is never saved.** `WindowShell::SaveWindowPlacement` is
  compiled and callable, but nothing calls it — in the VPN client `AppController`
  does it on hide and on quit, and there is no `AppController` here. Restore
  works; there will never be anything to restore.
- **The window reveal is armed with `nullopt` origin** (plain centred scale)
  because there is no tray icon to anchor to.
- **`app.ico` is the VPN client's icon.**
- **One locale.** `Strings/en` only.
- **No CI**, no installer, no signing, no update checker.
- **ARM64 and any non-x64 target are unverified.** Debug and Release x64 both
  build; only Release x64 has been run.
- **`.gitattributes` was verified in place**, not by re-cloning. **[RAN]**
  `git ls-files --eol` reports `i/-text w/-text attr/-text` for all four fonts
  and `app.ico`, so neither the index nor the working tree carries a conversion.
  A fresh clone on a `core.autocrlf=true` box has not been done.

## 7. Repo state

**[RAN]** Two commits, 60 tracked files, clean tree:

```
4c2e6da scaffold: URmessage Windows client shell that builds, launches and renders
23f3fb2 gitattributes: line-ending and binary policy, before any other file
```

`.gitattributes` is commit 1 and was committed **before any other file was
added**, per the contract. No remote is configured.

**[RAN]** Nothing of mine is left running: 0 processes at the built exe path and
0 named `URmessage` anywhere.
