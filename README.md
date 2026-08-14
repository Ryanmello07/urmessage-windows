# URmessage — Windows client

A private messenger on URnetwork, as a native Windows desktop app. It is meant
to look and feel like the URnetwork Windows VPN client, and it inherits that
app's brand layer, component kit and window shell verbatim.

**This is checkpoint 1: a shell.** It builds, launches, and renders the brand.
There is no protocol, no store, no network, no account and no cryptography — the
conversation list is ten hard-coded placeholder rows declared in one block at the
top of `MainWindow.xaml.cpp`. Nothing here sends or receives anything.

## Build

MSVC + the Windows 10/11 SDK. Windows App SDK 2.2.0 and C++/WinRT come from
NuGet (`msbuild /restore` does it; the packages are already in the local cache
on the development box).

```
powershell -ExecutionPolicy Bypass -File app\tools\build-local.ps1
```

`pwsh` does **not** exist on the development box; use `powershell`. The script
detects what is actually installed and overrides the pins in
`Directory.Build.props` (which name the CI reference box, v143 + SDK
10.0.22621.0): it maps MSVC 14.5x to `v145` and 14.4x to `v143`, and picks the
newest Windows SDK that really has `um\windows.h`. Output:

```
app\build\x64\Release\URmessage.exe
```

## Run, and prove it rendered

```
powershell -ExecutionPolicy Bypass -File app\tools\verify-render.ps1
```

That launches the exe, finds its window, captures it, probes the brand colours
and writes `.verify\*.png`. It is DPI-aware, finds windows with
`EnumWindows` + `GetClassNameW` rather than `FindWindow`, and selects processes
by **executable path**. Read the header of that file before changing it; each of
those is a trap this project has already paid for.

A fast smoke test that needs no window:

```
app\build\x64\Release\URmessage.exe --diagnose
```

It prints the App Runtime path, `resources.pri`, the fonts, the single-instance
key, and whether MRT actually **resolves** a string — the last one is the
difference between "resources.pri exists" and "the UI will not render its own
key ids".

## Layout

```
app/
  Directory.Build.props     C++20, version tokens, /utf-8
  URmessage.sln             two projects, no service, no driver, no installer
  src/Common/               identities, paths, logging, prefs (static lib)
    Ids.h                   READ THIS FIRST — the identities that must not
                            collide with the VPN client
  src/App/                  the WinUI 3 app
    App.xaml                the brand: 1122 lines, copied verbatim
    UrColors.h              the same palette for C++; keep the two in sync
    UrComponents.*          the component kit (pane rows, empty states, ...)
    UrMotion.*              durations, easings, the reduce-motion gate
    WindowShell.*           size, placement, caption colours. NO backdrop.
    WindowReveal.*          the open animation
    MainWindow.xaml*        written fresh: title bar, nav, conversation panes
  tools/                    build-local.ps1, verify-render.ps1
  third_party/vendor-include/nlohmann/
```

## Things that will bite you

- **`.gitattributes` is load-bearing.** `core.autocrlf=true` is set at system
  scope on the development box, and a font that goes through CRLF translation is
  corrupt with **no diagnostic at all** — DirectWrite just fails to register the
  family and XAML silently falls back to a system face. The top-level
  `.gitattributes` defaults to `-text` and names every binary explicitly. Check
  `git ls-files --eol` after any clone.
- **Do not add Mica or any system backdrop.** Its removal from the VPN client
  was deliberate and is load-bearing: Mica draws *behind* XAML, so showing it
  means clearing the opaque `#101010` root, at which point the desktop wallpaper
  *replaces* the brand colour rather than tinting it. The long version is in
  `WindowShell.cpp`.
- **Pane model, not card model.** `App.xaml` carries two incompatible layout
  vocabularies. Cards are rounded islands with margins; panes are
  floor-to-ceiling columns divided by 1px rules, with one row height per list.
  A conversation list beside a thread is a pane layout. Do not mix them.
- **Pro gold (`#FFC400`) is reserved** for the Pro entitlement and nothing else.
  Lime is earnings/brand, blue is action.
- **`%URMESSAGE_APP_ROOT%`** overrides the per-user state root. Set it per
  worktree, or two concurrent builds corrupt each other's prefs and logs.

## Open questions that are not code

1. **Font licensing.** The four faces are commercial and were licensed for one
   product. This is a second product embedding the same files. Building is not
   distributing, so this blocks shipping, not development.
2. **Non-Latin coverage.** All four faces are Latin-only. For a messenger that
   affects user-typed *message content*, not just chrome. A deliberate fallback
   face per script is owed before real messages render.
3. **`app.ico` is the VPN client's mark.** Placeholder. Replace it before any
   screenshot leaves the team.
4. **Privacy policy URL** is mandatory under Store policy 10.5.1, and for a
   messenger that is unavoidable rather than optional.
