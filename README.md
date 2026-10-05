# URmessage — Windows client

A private messenger on URnetwork, as a native Windows desktop app. It is meant
to look and feel like the URnetwork Windows VPN client, and it inherits that
app's brand layer, component kit and window shell verbatim.

**This is the alpha, and an ordinary launch is live.** The app reaches the
message server through the URnetwork SDK and sends and receives real end-to-end
encrypted messages in MLS groups:
- text, replies and reactions;
- roles, removal and ownership transfer;
- delete for everyone;
- "Delete for me and leave".

Media, recovery, multi-device, receipts and a contact identity layer are not
here. Where they would be, the app says so rather than pretending.

The design is Spec C, in
[urnetwork/message-server](https://github.com/urnetwork/message-server)'s
`docs/specs`.

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

## The SDK

The live path is the URnetwork SDK's messaging C ABI, in `URnetworkSdk.dll`,
built from [urnetwork/sdk](https://github.com/urnetwork/sdk)'s `cgo` directory.
Three files of it are committed in `app\third_party\vendor-include`:
- `urnetwork_sdk.h`;
- `urnetwork_message.h`;
- `urnetwork_sdk.def`, the dll's export list.

Copy all three from one SDK build's `cgo\include`, never one without the others.

The build makes the import library from the `.def`, and the dll is delay-loaded,
so the app builds and runs without it. Only the live path needs it. To go live,
build the dll from the same SDK commit and stage it where the build copies it
next to the exe:

```
cd sdk\cgo
make build_windows_amd64        (or the go build line it runs, with a mingw-w64 gcc on PATH)
copy build\windows\amd64\URnetworkSdk.dll <this repo>\app\third_party\urnetwork-sdk\bin\x64\
```

A build that is going to ship passes `/p:UrmRequireSdkDll=true`, which turns
the build's "URnetworkSdk.dll is not staged" warning into an error.

## Installer

`app\installer` packages the Release output folder as a per-user MSI with WiX
v5, which `/restore` fetches from NuGet. Build the solution first, then, with
`msbuild` on PATH (a Developer PowerShell):

```
cd app
msbuild installer\Installer.wixproj /restore /p:Configuration=Release /p:Platform=x64
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify-msi-payload.ps1 -MsiPath installer\bin\x64\Release\URmessage.msi
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify-msi-ice03.ps1 -MsiPath installer\bin\x64\Release\URmessage.msi
```

It installs for the current user only, into `%LOCALAPPDATA%\Programs\URmessage`;
Microsoft documents that a per-user install of this kind shows no UAC prompt. A
launch condition refuses a per-machine install. The MSI is version 0.0.1 unless
`/p:UrMsiVersion=` stamps it, and two builds of one version replace each other.

What an install puts on the machine:
- The package adds the app folder, one Start-menu shortcut, and the
  `HKCU\Software\URmessage\Installer` value Windows Installer tracks that
  shortcut by.
- Windows Installer itself also registers the product for Settings > Apps, under
  `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\{ProductCode}`, and
  caches the package in `C:\Windows\Installer`, even for this per-user install.
  That was measured on a GitHub-hosted windows-2022 runner, installing as an
  elevated administrator. A second user installing the same build on the same
  machine has not been tested.
- An uninstall removes the app folder, the shortcut, the HKCU value and the
  Settings > Apps entry. The app keeps its state in `%LOCALAPPDATA%\URmessage`,
  which is not part of the install, so an uninstall leaves it alone.

The VC++ runtime is app-local. A Release build copies the CRT dlls of the
Visual Studio installation's default redist (`$(VCToolsRedistInstallDir)`; in
CI, VS 2022's 14.44.35112) next to `URmessage.exe`, and the MSI installs them
with the app, so it needs neither admin rights nor the VC++ Redistributable.
Windows Update does not service these copies. They are not KnownDLLs, so Windows
loads them from the app's folder first, even where a newer Redistributable is
installed system-wide. A runtime fix reaches users only through a new URmessage
build.

The MSI installs whatever the build left in `app\build\<platform>\Release`.
`verify-msi-payload.ps1` fails an MSI that lacks any of ten files the app
needs, at their paths (the exe, `resources.pri`, the compiled XAML, the Windows
App Runtime, the body face and the app-local VC++ runtime), and it asks Windows
Installer how the package resolves, without installing it: that a default
install installs those files and the shortcut, into a folder under
`%LOCALAPPDATA%\Programs`, and that a per-machine install is refused.
`verify-msi-ice03.ps1` runs ICE03, which the MSI build suppresses for the
harvested runtime (see `Installer.wixproj`), on its own, and fails on any row
but the eight known ones. Without a staged `URnetworkSdk.dll` the app's
ordinary launch cannot go live, and `verify-msi-payload.ps1 -Release` fails such
an MSI. CI builds the x64 MSI and runs both checks, the first without
`-Release`, and does not upload it: it carries the four brand faces (open
question 1, below), it has no SDK dll, and it is unstamped.

## Run

**A launch with no arguments is live.**
1. It reads a URnetwork credential, a `by_client_jwt`, from
   `%LOCALAPPDATA%\URmessage\dev\user1.jwt`, or from the file
   `%URMESSAGE_LIVE_JWT%` names. Nothing here mints one.
2. It reaches the message server and publishes this device's key package.

By default it goes through a URnetwork exit to the server's own pinned endpoint.
Settings switches that to direct, and `%URMESSAGE_ROUTE%` overrides both for one
launch.

To join a group, copy this device's join code to whoever is setting the group up.
When they add you, paste back the invitation they send.

**Any argument other than `--live`, or `%URMESSAGE_LIVE%=0`, keeps a launch
offline.** That is how a development build runs beside a real one. Two live
clients under one credential are two devices of one person, and the alpha
supports one. `--demo` draws a fabricated world instead; `--demo=thread`,
`=chats`, `=inspect` and `=network` open on one screen.

`%URMESSAGE_APP_ROOT%` moves the per-user state root, which defaults to
`%LOCALAPPDATA%\URmessage\app`. Give each concurrent run its own, and its own
credential.

To prove it rendered:

```
powershell -ExecutionPolicy Bypass -File app\tools\verify-render.ps1
```

That launches the exe offline (`-Live` launches it live), finds its window,
captures it, probes the brand colours and writes `.verify\*.png`. It is DPI-aware, finds windows with
`EnumWindows` + `GetClassNameW` rather than `FindWindow`, and selects processes
by **executable path**. Read the header of that file before changing it; each of
those is a trap this project has already paid for.

A fast smoke test that needs no window, and the one CI gates on:

```
app\build\x64\Release\URmessage.exe --diagnose
```

It prints the App Runtime path, `resources.pri`, the fonts, the single-instance
key, whether MRT actually **resolves** a string, and the app's PASS/FAIL
assertions. MRT matters because "resources.pri exists" and "the UI will not
render its own key ids" can both be true at once.

## Layout

```
app/
  Directory.Build.props     C++20, version tokens, /utf-8
  URmessage.sln             two projects, no service, no driver
  installer/                the per-user MSI (WiX v5), built apart; see Installer
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
    Views/                  the conversation list, thread, inspector rail, status
                            strip, and the settings and network pages
    Live/                   the live path: the SDK worker (LiveMesh) and the
                            world it builds for the views (LiveWorld)
    Demo/                   the fabricated demo world (--demo) and developer switches
  tools/                    build-local.ps1, verify-render.ps1, verify-msi-payload.ps1,
                            verify-msi-ice03.ps1
  third_party/vendor-include/
    nlohmann/
    urnetwork_sdk.h, urnetwork_message.h, urnetwork_sdk.def   the SDK's ABI
  third_party/urnetwork-sdk/bin/x64/   URnetworkSdk.dll, staged, not committed
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
