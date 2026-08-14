# App assets

## Brand fonts (`Fonts/`)

The four URnetwork brand faces, copied byte-for-byte from the URnetwork Windows
VPN client (`app/src/App/Assets/Fonts/`), which took them byte-for-byte from the
android repo. **These are licensed commercial faces** (ABC Gravity is Dinamo,
PP Neue Montreal / PP NeueBit are Pangram Pangram): they ship inside the app and
must not be redistributed on their own.

> **OPEN LICENSING QUESTION — BLOCKS SHIPPING, NOT DEVELOPMENT.**
> The licence was assessed for **one** product. URmessage is a **second,
> separate** product embedding the same four files, which doubles the exposure,
> and nothing in either repo answers whether the existing grant covers it.
> Building is not distributing, so this does not block checkpoint 1 — it blocks
> a release. Raised in the scaffold brief as owner question 1.

XAML references a bundled font as `ms-appx:///<path>#<family>`, where the family
is the font's **own internal family name** — the OpenType `name` table, id 1 —
and *not* the file name. Getting it wrong fails silently: the text just renders
in the fallback face. These were read out of the shipped files on 2026-08-13
with `[System.Drawing.Text.PrivateFontCollection]`, not copied on trust:

| File | Family name (name id 1) | Faces in file | Role here |
|---|---|---|---|
| `abcgravity_extended.otf` | `ABC Gravity Extended` | Regular | page titles, hero, `UrTitleTextStyle` |
| `abcgravity_extra_condensed.otf` | `ABC Gravity Extra Condensed` | Regular | reserved |
| `pp_neue_montreal_regular.ttf` | `PP Neue Montreal` | Regular | everything below Title |
| `pp_neue_bit_bold.ttf` | `PP NeueBit` | **Bold only** | the title-bar wordmark |

Two traps in that table. Both ABC Gravity files share the *typographic* family
`ABC Gravity` (name id 16) and differ only by subfamily, so the full id-1 name is
the unambiguous one to reference. And `PP NeueBit` has no space in `NeueBit`, and
ships only its Bold face — anything using it states `FontWeight="Bold"` so the
real face is selected rather than synthesised from it.

`App.xaml` defines the four `FontFamily` resources; `App.vcxproj` copies the
files to `$(OutDir)Assets\Fonts` so an unpackaged app can resolve `ms-appx:///`
against the exe's own folder.

### How to PROVE the wordmark is really PP NeueBit

A wrong family name produces no error and no warning, so looking at the log
cannot answer this and neither can reading the XAML. Measure the ink instead —
`app/tools/verify-render.ps1` captures the window, and the check is that the
wordmark's rendered ink box matches PP NeueBit's and no other candidate's:

| Face, "URmessage" at a 30px em | ink w × h |
|---|---|
| **as the app drew it** | **111 × 19** |
| PP NeueBit | 114 × 19 |
| PP Neue Montreal | 164 × 27 |
| ABC Gravity Extended | 219 × 28 |
| Segoe UI Bold — *what silent fallback looks like* | 160 × 28 |

The ~3% width difference against the reference is GDI+ vs DirectWrite spacing.
Every wrong answer is off by 40%+, so this test cannot be passed by accident.

Note for anyone eyeballing it: **PP NeueBit does not look pixelated at UI
sizes.** It reads as a compact grotesque with a small glyph-to-em ratio, which
is why the ink box is so much shorter than the others at the same em size. "It
doesn't look like a bitmap font, so it must have fallen back" is wrong.

### Coverage: these are Latin-only faces

Read out of each `cmap` in the VPN repo: PP Neue Montreal 596 codepoints,
PP NeueBit 592, both ABC Gravity cuts 464. Latin, punctuation, arrows, the
currency marks — and nothing else.

> **THIS MATTERS MORE HERE THAN IT DID FOR THE VPN CLIENT, AND IT IS UNDECIDED.**
> For a VPN client, font fallback affects chrome. For a messenger it affects
> **user-typed message content**, which is the product: every Arabic, Hebrew,
> Hindi, CJK or Cyrillic message a user sends or receives will render in
> whatever face DirectWrite happens to pick, with no design decision behind it.
> URmessage needs a **deliberate fallback stack** — a named face per script,
> chosen and checked — before it renders real messages. Scaffold brief, owner
> question 2. Nothing here implements it yet.

**If text renders in the fallback face**, the reference failed and said nothing —
that is the only failure mode fonts have here. Check, in order:

1. `Assets\Fonts\*` really are next to `URmessage.exe`.
2. The family name after `#`. Re-read it rather than trusting the table above.
3. The URI form. `ms-appx:///` is what WinUI documents and what this app uses,
   but an **unpackaged** app also accepts an exe-relative path —
   `/Assets/Fonts/pp_neue_bit_bold.ttf#PP NeueBit` — worth trying if
   `ms-appx:///` resolves images but not fonts.

## App icon

> **`app.ico` IS A PLACEHOLDER AND IT IS THE WRONG PRODUCT'S MARK.**
> It is the URnetwork **VPN client's** icon, copied verbatim so the build has an
> `ApplicationIcon` and the title bar's 20epx mark has something to draw. It
> renders as the URnetwork globe in the taskbar, in alt-tab and beside the
> URmessage wordmark, which is a brand error, not a stylistic choice.
> **Replace it before anyone outside the team sees a screenshot.**

`App.rc` maps it to `IDI_APP` in `resource.h`, and `App.vcxproj` also copies it
to `$(OutDir)Assets` so the title bar can resolve
`ms-appx:///Assets/app.ico`.

The VPN client's eight tray icons (light/dark × connect × provide) are
deliberately **not** copied: URmessage has no tray icon yet, and copying VPN
state art for a messenger would be worse than having none.
