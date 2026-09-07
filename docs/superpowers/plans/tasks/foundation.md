# Foundation

> Part of [the URmessage demo UI plan](../2026-09-06-urmessage-demo-ui.md). Read that file's **Global Constraints** first — they apply to every task here.

---

## Task F1: verify-render.ps1 argument passthrough (-AppArgs)

**Files:**

Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/tools/verify-render.ps1

**Interfaces:**

- Consumes: Nothing. This is the first task in the plan; every later task's screenshot step calls the parameter it adds.
- Produces: `app/tools/verify-render.ps1` gains `[string]$AppArgs = ""`, forwarded by splat. Invocation contract for every later task: `powershell -ExecutionPolicy Bypass -File app\tools\verify-render.ps1 -AppArgs "--demo=<screen>"`. An empty/omitted value passes NO argument at all. The parameter is never named `-Args`.

# F1 — `verify-render.ps1` argument passthrough

Repo root: `C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows`. All commands run from there.

**Why first.** Measured: the param block is `[CmdletBinding()] param($Configuration, $Platform, $SettleMs)` (lines 30–35) and line 156 is `$proc = Start-Process -FilePath $exe -PassThru`. Two defects — no parameter to accept switches, and no forwarding if there were. `[CmdletBinding()]` makes an undeclared `-Args` a *terminating* binding error, so every pixel gate in this plan fails at the command line until this is fixed.

- [ ] **Step 1: Add the `-AppArgs` parameter.** Edit `app/tools/verify-render.ps1`. Replace lines 30–35 exactly:

  ```powershell
  [CmdletBinding()]
  param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64",
    [int]$SettleMs = 1200,
    # Switches handed to URmessage.exe, e.g. "--demo=thread". NEVER name this
    # $Args: that shadows PowerShell's automatic $args variable inside the
    # script and the shadowing is silent. Forwarded by SPLAT below, so an empty
    # value passes NOTHING - Start-Process rejects an empty -ArgumentList.
    [string]$AppArgs = ""
  )
  ```

- [ ] **Step 2: Forward it by splat.** Replace line 156 (`$proc = Start-Process -FilePath $exe -PassThru`) with:

  ```powershell
  # Splat rather than a ternary: -ArgumentList "" is a parameter-binding error,
  # so the argument has to be ABSENT, not empty, when there is nothing to pass.
  $extra = @{}
  if ($AppArgs) { $extra['ArgumentList'] = $AppArgs }
  $proc = Start-Process -FilePath $exe -PassThru @extra
  ```

- [ ] **Step 3: Run it with NO `-AppArgs` — the app must still launch normally.**

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\verify-render.ps1
  ```

  Expected (measured on this box: primary 2560×1600 physical, 125%, work area 2048×1194 DIP):

  ```
  launched pid <N>: ...\app\build\x64\Release\URmessage.exe

  class name  : WinUIDesktopWin32WindowClass
  window text : URmessage
  window rect : 600x950 at (<x>,<y>)  [PHYSICAL pixels, harness is PerMonitorV2]
  window dpi  : 120  (scale 1.25)
  dips        : 480x760
  ```

  `dips : 480x760` is the point — the non-demo default is **480×760**, not 1100 and not 1560. If this line says anything else, stop: something already changed `WindowShell.h`.

- [ ] **Step 4: Run it WITH `-AppArgs` — prove the argument actually reached the exe.**

  `--demo` does not exist yet, so it would prove nothing. `--diagnose` does exist and makes the process print and exit *without a window*, which the harness reports as a hard failure. That failure IS the proof of forwarding.

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\verify-render.ps1 -AppArgs "--diagnose"
  ```

  Expected — a thrown terminating error, exit code 1:

  ```
  launched pid <N>: ...\app\build\x64\Release\URmessage.exe
  the process exited with code 0 before showing a window
  ```

  Before Step 2 this run is byte-identical to Step 3's (a window at 480×760): the switch was dropped. After Step 2 it dies at the poll loop. Run it once *before* applying Step 2 if you want the negative control.

- [ ] **Step 5: Prove `-Args` still does not exist.** The old spelling must remain an error, so no later task can quietly reintroduce it:

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\verify-render.ps1 -Args "--diagnose"
  ```

  Expected, on stderr:

  ```
  verify-render.ps1 : A parameter cannot be found that matches parameter name 'Args'.
  ```

- [ ] **Step 6: Confirm nothing else in the script moved.**

  ```
  git diff --stat app/tools/verify-render.ps1
  ```

  Expected: `1 file changed, 12 insertions(+), 2 deletions(-)` (±1 on the comment lines). PerMonitorV2, `EnumWindows`+`GetClassNameW`, selection by executable path and the CopyFromScreen capture must all be untouched — `git diff` must show no hunk outside lines 30–35 and 156.

- [ ] **Step 7: Commit.** The index on this box has silently truncated before; count first.

  ```
  powershell -Command "(git ls-files | Measure-Object).Count"
  git status --short
  git add app/tools/verify-render.ps1
  git commit
  ```

  Expected: the count prints **63** and `git status --short` shows exactly `M app/tools/verify-render.ps1`. If the count is not 63, do not commit — the index is truncated. Commit message:

  ```
  verify: forward launch switches to the app under test

  The harness could not pass a single argument: no parameter to accept one,
  and nothing to forward it with. -AppArgs, splatted so an empty value passes
  nothing. Named AppArgs and not Args, which would shadow the automatic $args.

  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
  ```

**Deliverable:** `-AppArgs "--diagnose"` makes the harness throw `the process exited with code 0 before showing a window`; omitting it captures a 480×760 DIP window; `-Args` is still an error.

---

## Task F2: Demo/DemoWorld.h/.cpp — the seeded fabricated world

**Files:**

Create: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/DemoWorld.h
Create: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/DemoWorld.cpp
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.vcxproj

**Interfaces:**

- Consumes: F1 (only for the commit-count discipline; F2 builds and does not screenshot).
- Produces: Header `Demo/DemoWorld.h`, namespace `urmsg::demo`, PURE C++ (compiled `<PrecompiledHeader>NotUsing</PrecompiledHeader>`, so it cannot reach a winrt header by accident). Types verbatim from contract §1: `DeliveryState`, `RetentionClass`, `ConversationKind`, `RowKind`, `ConnectState`, `Seed`, `DeviceRef`, `MemberRef`, `MessageInspect`, `MessageRow`, `Conversation`, `ServerInfo`, `RelayNode`, `World`. Functions: `const World& GetWorld();` and `World& MutableWorld();`. One added constant, needed because contract §2 names a "designated inspect target" that §1 gives no field to carry: `inline constexpr wchar_t kInspectTargetRowId[] = L"c0-r12";`. Row ids are `c<conversationIndex>-r<rowIndex>`. Measured contents: 8 conversations (2 group / 6 direct), 66 rows, 22 members, 26 member devices, 3 myDevices, 3 relay nodes.

# F2 — `Demo/DemoWorld.h/.cpp`

**PURE C++.** `GetWorld()` is called from `CollectDiagnostics()`, which `wWinMain` runs at `main.cpp:168` — *before* `winrt::init_apartment()` at `main.cpp:183`. A winrt type in this file would be a crash, not a style opinion. Step 3 makes that a build property rather than a comment.

- [ ] **Step 1: Create `app/src/App/Demo/DemoWorld.h`.** Contract §1 verbatim, plus the file header and the one added constant.

  ```cpp
  // The entire fabricated demo world: one seeded generator, no runtime
  // randomness, identical bytes on every launch. Design doc section 5.
  //
  // PURE C++ AND IT HAS TO BE. GetWorld() is called from CollectDiagnostics(),
  // which wWinMain runs at main.cpp:168 - BEFORE winrt::init_apartment() at
  // main.cpp:183. A winrt type here would be constructed without an apartment.
  // App.vcxproj compiles the .cpp with PrecompiledHeader=NotUsing so the
  // project's pch (which pulls in every winrt/ header) cannot leak in either.
  //
  // The day a protocol arrives, the diff that deletes the demo is this
  // directory and one switch.
  //
  // SPDX-License-Identifier: MPL-2.0
  #pragma once

  #include <array>
  #include <cstdint>
  #include <string>
  #include <vector>

  namespace urmsg::demo {

  enum class DeliveryState { Pending, Sent, Delivered, Read, Failed, Expired };
  enum class RetentionClass { Permanent, Eph };
  enum class ConversationKind { Direct, Group };
  enum class RowKind { Message, DaySeparator, System };
  enum class ConnectState { Connected, Connecting, Offline };

  using Seed = std::array<uint8_t, 32>;   // identicon seed

  // A DEVICE. Distinct from a person - see MemberRef.
  struct DeviceRef {
    std::wstring id;              // stable: "dev-pixel9"
    std::wstring name;            // "Pixel 9"
    std::wstring ownerName;       // "Bo Nakamura"
    Seed ownerKey;                // identicon seed for the owner
    std::wstring lastSeenLabel;   // "now" | "2 min ago" | "3 days ago"
    bool online;
    bool isThisComputer;          // exactly one true in World::myDevices
  };

  // A PERSON. A group of 5 members has 5 MemberRefs and may have more devices.
  struct MemberRef {
    std::wstring id;
    std::wstring displayName;
    Seed identityKey;             // identicon seed
    bool admin;
    std::vector<DeviceRef> devices;
  };

  struct MessageInspect {
    uint64_t epoch;
    uint32_t senderLeafIndex;
    RetentionClass retention;
    std::wstring sizeBucket;          // "<= 1 KiB"
    uint32_t wireSizeBytes;           // Advanced Mode only
    bool attestationVerified;
    std::wstring cipher;              // "XChaCha20-Poly1305"
    std::wstring groupIdHex;          // Advanced Mode only
    std::wstring senderDisplayName;   // ALWAYS populated, even on run continuations
    std::wstring sentAtLabel;
    std::wstring receivedAtLabel;
    std::vector<DeviceRef> deliveredTo;
    std::vector<DeviceRef> readBy;
  };

  struct MessageRow {
    RowKind kind;
    std::wstring id;
    std::wstring senderName;              // empty in DMs and on run continuations
    Seed senderKey;
    std::wstring body;
    std::wstring timeLabel;               // "14:22"
    bool outgoing;
    DeliveryState state;
    std::wstring failureReason;           // non-empty only when state == Failed
    std::wstring systemText;              // used only when kind == System
    bool permanentRecord;                 // key-change record: non-dismissible
    MessageInspect inspect;
  };

  struct Conversation {
    std::wstring id;
    ConversationKind kind;
    std::wstring name;
    Seed identityKey;
    std::wstring groupIdHex;              // non-empty for groups
    std::wstring preview;                 // ALWAYS NON-EMPTY, including the EPH one
    std::wstring timeLabel;
    int unread;
    bool muted;
    bool disappearing;                    // row draws the timer glyph INSTEAD of preview
    std::vector<MemberRef> members;       // PEOPLE
    int memberCount;                      // MUST equal members.size()
    std::wstring retentionLabel;
    std::wstring mediaRetentionLabel;
    std::vector<MessageRow> rows;
  };

  struct ServerInfo {
    std::wstring host;            // "urmsg-01.ur.io"
    std::wstring jurisdiction;    // "Iceland"
    int latencyMs;
    bool keyVerified;
  };

  // One hop of the relay path. EXACTLY THREE in World::relayPath.
  struct RelayNode {
    std::wstring label;       // "This device" | "URnetwork" | "Message server"
    std::wstring subLabel;    // "This computer" | "3 hops" | "urmsg-01.ur.io"
    std::wstring glyph;       // Segoe Fluent codepoint, NEVER EMPTY
    int hopMs;                // Advanced per-hop timing
    bool healthy;
  };

  struct World {
    std::vector<Conversation> conversations;
    std::vector<DeviceRef> myDevices;     // myDevices[0].isThisComputer == true
    std::vector<RelayNode> relayPath;     // exactly 3
    ServerInfo server;
    uint64_t currentEpoch;
    ConnectState connectState;
    std::wstring sessionMode;             // "direct" - Advanced strip only
    int recordsPerSecond;                 // Advanced strip only
  };

  // Row ids are "c<conversationIndex>-r<rowIndex>", assigned in build order.
  //
  // ADDED BEYOND CONTRACT v2 SECTION 1, and flagged as such: section 2 defines
  // --demo=inspect as "select the message named by DemoWorld's designated
  // inspect target", but section 1 gives no field to carry that name and adding
  // one to a fixed struct is forbidden. A constant names it instead - it adds
  // no field, renames nothing and changes no signature. It points at
  // conversations[0].rows[12]: an OUTGOING message in state Read, so the rail's
  // message mode opens with a populated deliveredTo AND readBy list, which is
  // the state a screenshot needs.
  inline constexpr wchar_t kInspectTargetRowId[] = L"c0-r12";

  // Built once, seeded, deterministic. Same bytes on every launch.
  const World& GetWorld();

  // Ambient activity (design doc 9.2) is the ONLY caller. It appends a
  // MessageRow and advances one DeliveryState. Nothing else may mutate it.
  World& MutableWorld();

  }  // namespace urmsg::demo
  ```

- [ ] **Step 2: Create `app/src/App/Demo/DemoWorld.cpp` — part 1, the arithmetic.** Everything below the seed helpers is a table; nothing here reads a clock, an address or an RNG, which is what makes the fingerprint in F3 stable.

  ```cpp
  // SPDX-License-Identifier: MPL-2.0
  //
  // NO pch.h include, and App.vcxproj compiles this unit with
  // PrecompiledHeader=NotUsing: the project pch pulls in every winrt/ header,
  // and this file runs before init_apartment. See DemoWorld.h.
  #include "Demo/DemoWorld.h"

  #include <string_view>

  namespace urmsg::demo {
  namespace {

  // FNV-1a 64 over the utf-16 code units, then splitmix64 to fill 32 bytes.
  // Pure arithmetic: same id -> same seed in every process, on every machine,
  // for ever. That is the whole contract of this file.
  constexpr uint64_t Fnv1a64(std::wstring_view s) {
    uint64_t h = 1469598103934665603ull;
    for (wchar_t c : s) {
      h ^= static_cast<uint64_t>(static_cast<uint16_t>(c));
      h *= 1099511628211ull;
    }
    return h;
  }

  constexpr uint64_t SplitMix64(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  Seed SeedFrom(std::wstring_view id) {
    uint64_t state = Fnv1a64(id);
    Seed out{};
    for (size_t i = 0; i < out.size(); i += 8) {   // 32 / 8 = exactly 4 rounds
      const uint64_t v = SplitMix64(state);
      for (size_t b = 0; b < 8; ++b)
        out[i + b] = static_cast<uint8_t>((v >> (b * 8)) & 0xFFu);
    }
    return out;
  }

  std::wstring HexOf(Seed const& s, size_t bytes) {
    static constexpr wchar_t kDigits[] = L"0123456789abcdef";
    std::wstring out;
    out.reserve(bytes * 2);
    for (size_t i = 0; i < bytes && i < s.size(); ++i) {
      out.push_back(kDigits[(s[i] >> 4) & 0x0Fu]);
      out.push_back(kDigits[s[i] & 0x0Fu]);
    }
    return out;
  }

  constexpr uint64_t kCurrentEpoch = 4182;

  DeviceRef MakeDevice(std::wstring id, std::wstring name, std::wstring ownerName,
                       std::wstring lastSeen, bool online, bool thisComputer) {
    DeviceRef d;
    d.id = std::move(id);
    d.name = std::move(name);
    d.ownerName = std::move(ownerName);
    d.ownerKey = SeedFrom(d.ownerName);
    d.lastSeenLabel = std::move(lastSeen);
    d.online = online;
    d.isThisComputer = thisComputer;
    return d;
  }

  struct PersonSpec { const wchar_t* id; const wchar_t* name; bool admin; };

  MemberRef MakeMember(PersonSpec const& p) {
    MemberRef m;
    m.id = p.id;
    m.displayName = p.name;
    m.identityKey = SeedFrom(m.id);
    m.admin = p.admin;
    // One device each; admins get a second, so the rail's delivered-to and
    // read-by lists are longer than the member list and cannot be mistaken for
    // it. Spec C 5.3: delivered is a statement by a DEVICE, never by a person.
    m.devices.push_back(
        MakeDevice(m.id + L"-phone", L"Phone", m.displayName, L"now", true, false));
    if (p.admin)
      m.devices.push_back(
          MakeDevice(m.id + L"-laptop", L"Laptop", m.displayName, L"12 min ago", false, false));
    return m;
  }

  }  // namespace
  }  // namespace urmsg::demo
  ```

- [ ] **Step 3: Register both files in `app/vcxproj`, with the purity enforced by the build.** In `app/src/App/App.vcxproj`, add to the `<ItemGroup>` of `ClCompile` (after `<ClCompile Include="App.xaml.cpp">…`) and to the `ClInclude` group:

  ```xml
  <!-- NotUsing is load-bearing, not a preference: pch.h pulls in windows.h and
       every winrt/ header, and DemoWorld runs before winrt::init_apartment
       (main.cpp:168 vs :183). Compiling it standalone is what makes "pure C++"
       a property of the build rather than a comment someone can delete. -->
  <ClCompile Include="Demo\DemoWorld.cpp"><PrecompiledHeader>NotUsing</PrecompiledHeader></ClCompile>
  ```

  ```xml
  <ClInclude Include="Demo\DemoWorld.h" />
  ```

- [ ] **Step 4: `DemoWorld.cpp` part 2 — the row builder.** Append inside the anonymous namespace, above its closing brace:

  ```cpp
  struct RowSpec {
    RowKind kind;
    const wchar_t* senderId;   // member id; L"" for outgoing and non-message rows
    const wchar_t* body;       // message body, day label, or system sentence
    const wchar_t* time;       // L"14:22"; empty on a separator
    bool outgoing;
    DeliveryState state;       // meaningful only on an outgoing Message row
    const wchar_t* failure;    // non-empty ONLY when state == Failed
    bool permanent;            // the key-change record
  };

  MemberRef const* FindMember(Conversation const& c, std::wstring_view id) {
    for (auto const& m : c.members)
      if (m.id == id) return &m;
    return nullptr;
  }

  // Every device of every member, for the delivered-to / read-by lists.
  std::vector<DeviceRef> AllMemberDevices(Conversation const& c) {
    std::vector<DeviceRef> out;
    for (auto const& m : c.members)
      for (auto const& d : m.devices) out.push_back(d);
    return out;
  }

  void AppendRows(Conversation& c, size_t conversationIndex,
                  std::vector<DeviceRef> const& myDevices,
                  std::initializer_list<RowSpec> specs) {
    std::wstring day = L"Today";      // a conversation with no separator is today
    size_t index = 0;
    const size_t total = specs.size();
    std::wstring previousSender;      // for the run-continuation rule

    for (RowSpec const& s : specs) {
      MessageRow r{};
      r.kind = s.kind;
      r.id = L"c" + std::to_wstring(conversationIndex) + L"-r" + std::to_wstring(index);
      r.body = s.body;
      r.timeLabel = s.time;
      r.outgoing = s.outgoing;
      r.state = s.state;
      r.failureReason = s.failure;
      r.permanentRecord = s.permanent;

      if (s.kind == RowKind::DaySeparator) {
        day = s.body;
        previousSender.clear();
      } else if (s.kind == RowKind::System) {
        r.systemText = s.body;
        r.body.clear();
        previousSender.clear();
      }

      MemberRef const* sender = FindMember(c, s.senderId);
      const bool group = (c.kind == ConversationKind::Group);
      const std::wstring senderDisplay =
          s.outgoing ? std::wstring(L"You")
                     : (sender ? sender->displayName : std::wstring(L"System"));

      if (s.kind == RowKind::Message) {
        r.senderKey = s.outgoing ? SeedFrom(L"me-rowan-ashby")
                                 : (sender ? sender->identityKey : Seed{});
        // senderName is the BUBBLE's name line: empty on an outgoing row,
        // empty in a DM (there is only one other person), and empty on a run
        // continuation. inspect.senderDisplayName is always populated - the
        // rail must never say "unknown" for a message it is inspecting.
        const bool continuation = (!previousSender.empty() && previousSender == s.senderId);
        if (group && !s.outgoing && !continuation) r.senderName = senderDisplay;
        previousSender = s.outgoing ? std::wstring() : std::wstring(s.senderId);
      }

      MessageInspect& n = r.inspect;
      n.epoch = kCurrentEpoch - (static_cast<uint64_t>(total) - 1 - index);
      n.senderLeafIndex = 0;
      if (!s.outgoing && sender) {
        for (size_t i = 0; i < c.members.size(); ++i)
          if (c.members[i].id == sender->id) n.senderLeafIndex = static_cast<uint32_t>(i + 1);
      }
      n.retention = c.disappearing ? RetentionClass::Eph : RetentionClass::Permanent;
      const size_t len = std::wstring_view(s.body).size();
      n.sizeBucket = (len < 80) ? L"<= 1 KiB" : L"<= 4 KiB";
      n.wireSizeBytes = static_cast<uint32_t>(288 + len * 2);
      n.attestationVerified = (s.state != DeliveryState::Failed);
      n.cipher = L"XChaCha20-Poly1305";
      n.groupIdHex = c.groupIdHex;
      n.senderDisplayName = senderDisplay;
      n.sentAtLabel = day + L" " + std::wstring(s.time);

      if (s.outgoing) {
        // Nothing has been received until a DEVICE said so.
        const bool reached = (s.state == DeliveryState::Delivered ||
                              s.state == DeliveryState::Read);
        if (reached) {
          n.receivedAtLabel = n.sentAtLabel;
          n.deliveredTo = AllMemberDevices(c);
          if (s.state == DeliveryState::Read) n.readBy = n.deliveredTo;
        }
      } else if (s.kind == RowKind::Message) {
        n.receivedAtLabel = n.sentAtLabel;
        n.deliveredTo = myDevices;
        n.readBy = myDevices;
      }

      c.rows.push_back(std::move(r));
      ++index;
    }
  }
  ```

- [ ] **Step 5: `DemoWorld.cpp` part 3 — the eight conversations.** Append inside the anonymous namespace. Names are fabricated and neutral; no real people.

  ```cpp
  Conversation MakeConversation(const wchar_t* id, ConversationKind kind, const wchar_t* name,
                                std::initializer_list<PersonSpec> people,
                                const wchar_t* preview, const wchar_t* time, int unread,
                                bool muted, bool disappearing,
                                const wchar_t* retention, const wchar_t* mediaRetention) {
    Conversation c;
    c.id = id;
    c.kind = kind;
    c.name = name;
    c.identityKey = SeedFrom(c.id);
    c.groupIdHex = (kind == ConversationKind::Group) ? HexOf(c.identityKey, 8) : L"";
    c.preview = preview;                       // never empty, INCLUDING the EPH one
    c.timeLabel = time;
    c.unread = unread;
    c.muted = muted;
    c.disappearing = disappearing;
    for (PersonSpec const& p : people) c.members.push_back(MakeMember(p));
    c.memberCount = static_cast<int>(c.members.size());
    c.retentionLabel = retention;
    c.mediaRetentionLabel = mediaRetention;
    return c;
  }

  constexpr auto kSent = DeliveryState::Sent;
  constexpr auto kDeliv = DeliveryState::Delivered;
  constexpr auto kRead = DeliveryState::Read;
  constexpr auto kPend = DeliveryState::Pending;
  constexpr auto kFail = DeliveryState::Failed;
  constexpr auto kMsg = RowKind::Message;
  constexpr auto kSep = RowKind::DaySeparator;
  constexpr auto kSys = RowKind::System;
  ```

- [ ] **Step 6: `DemoWorld.cpp` part 4 — `BuildWorld()`, the two open conversations (42 rows).** Append after the anonymous namespace's helpers, still inside it:

  ```cpp
  World BuildWorld() {
    World w;
    w.currentEpoch = kCurrentEpoch;
    w.connectState = ConnectState::Connected;
    w.sessionMode = L"direct";
    w.recordsPerSecond = 12;

    // THREE devices, and myDevices[0] is this computer. Exactly one true.
    w.myDevices.push_back(MakeDevice(L"dev-desktop", L"Windows desktop", L"Rowan Ashby",
                                     L"now", true, true));
    w.myDevices.push_back(MakeDevice(L"dev-pixel9", L"Pixel 9", L"Rowan Ashby",
                                     L"2 min ago", true, false));
    w.myDevices.push_back(MakeDevice(L"dev-laptop", L"Linux laptop", L"Rowan Ashby",
                                     L"3 days ago", false, false));

    // c0 - Design team, 5 members, 24 rows.
    Conversation c0 = MakeConversation(
        L"conv-design-team", ConversationKind::Group, L"Design team",
        {{L"m-mira", L"Mira Okonkwo", true},
         {L"m-tobias", L"Tobias Lind", true},
         {L"m-saoirse", L"Saoirse Kelly", false},
         {L"m-ravi", L"Ravi Menon", false},
         {L"m-elena", L"Elena Vasquez", false}},
        L"Retrying the attachment.", L"12:11", 0, false, false,
        L"Kept until deleted", L"Media kept 30 days");
    AppendRows(c0, 0, w.myDevices, {
      {kSep, L"", L"Yesterday", L"", false, kSent, L"", false},
      {kMsg, L"m-mira", L"Pushed the rail spec - the message pane is the bit I want eyes on.", L"09:12", false, kSent, L"", false},
      {kMsg, L"m-mira", L"Two modes in one column, no navigation.", L"09:12", false, kSent, L"", false},
      {kMsg, L"m-tobias", L"Reading it now. The delivered-by device list is the part that sells it.", L"09:20", false, kSent, L"", false},
      {kMsg, L"", L"Agreed. Nobody else can show you which device decrypted a message.", L"09:26", true, kRead, L"", false},
      {kMsg, L"m-saoirse", L"Do we have a name for the key-change record yet?", L"09:31", false, kSent, L"", false},
      {kSys, L"", L"Ravi Menon was added to the group", L"09:40", false, kSent, L"", false},
      {kMsg, L"m-ravi", L"Thanks. Catching up on the thread.", L"09:44", false, kSent, L"", false},
      {kMsg, L"", L"Welcome. Start at the rail spec, section 3.", L"09:51", true, kRead, L"", false},
      {kMsg, L"m-elena", L"The 360 rail width holds at 1560 but not at 1280.", L"10:03", false, kSent, L"", false},
      {kSep, L"", L"Today", L"", false, kSent, L"", false},
      {kMsg, L"m-mira", L"Overnight build is green on both architectures.", L"11:02", false, kSent, L"", false},
      {kMsg, L"", L"Nice. I'll take the identicon pass this morning.", L"11:08", true, kRead, L"", false},
      {kMsg, L"m-tobias", L"One note on the palette: nothing that reads as a state colour.", L"11:15", false, kSent, L"", false},
      {kMsg, L"m-tobias", L"No green, no red, no amber.", L"11:15", false, kSent, L"", false},
      {kMsg, L"", L"That is the rule in the header. It fails the build otherwise.", L"11:22", true, kDeliv, L"", false},
      {kSys, L"", L"Disappearing messages set to 7 days", L"11:30", false, kSent, L"", false},
      {kMsg, L"m-saoirse", L"Good. The old 24 hours was losing decisions.", L"11:34", false, kSent, L"", false},
      {kMsg, L"", L"Rail mode swap is a crossfade now, not a slide.", L"11:41", true, kDeliv, L"", false},
      {kMsg, L"m-ravi", L"Slide implied navigation. Crossfade is right.", L"11:48", false, kSent, L"", false},
      {kMsg, L"", L"Pushing the branch in ten minutes.", L"11:55", true, kSent, L"", false},
      {kMsg, L"m-elena", L"I'll review it after standup.", L"12:02", false, kSent, L"", false},
      {kMsg, L"", L"Attaching the rail measurements now.", L"12:09", true, kFail,
              L"The message server did not acknowledge this message.", false},
      {kMsg, L"", L"Retrying the attachment.", L"12:11", true, kPend, L"", false},
    });

    // c1 - URnetwork core, 11 members, 18 rows, and the PERMANENT key-change
    // record. Spec C 7.4: non-dismissible.
    Conversation c1 = MakeConversation(
        L"conv-urnetwork-core", ConversationKind::Group, L"URnetwork core",
        {{L"m-mira", L"Mira Okonkwo", true},
         {L"m-hana", L"Hana Sato", true},
         {L"m-viktor", L"Viktor Halden", false},
         {L"m-noor", L"Noor Haddad", false},
         {L"m-jonas", L"Jonas Weber", false},
         {L"m-amara", L"Amara Diallo", false},
         {L"m-kai", L"Kai Lindqvist", false},
         {L"m-priya", L"Priya Raman", false},
         {L"m-oskar", L"Oskar Bremer", false},
         {L"m-lucia", L"Lucia Ferrari", false},
         {L"m-tobias", L"Tobias Lind", false}},
        L"Will do.", L"10:36", 2, false, false,
        L"Kept until deleted", L"Media kept 30 days");
    AppendRows(c1, 1, w.myDevices, {
      {kSep, L"", L"Today", L"", false, kSent, L"", false},
      {kMsg, L"m-hana", L"Relay path is answering on all three hops this morning.", L"08:41", false, kSent, L"", false},
      {kMsg, L"m-viktor", L"Latency on the middle hop is down to 41 ms.", L"08:47", false, kSent, L"", false},
      {kMsg, L"", L"That is the lowest it has been this week.", L"08:52", true, kRead, L"", false},
      {kMsg, L"m-noor", L"Iceland node came back after the maintenance window.", L"09:05", false, kSent, L"", false},
      {kMsg, L"m-noor", L"No key rotation needed on our side.", L"09:05", false, kSent, L"", false},
      {kSys, L"", L"Bo Nakamura's identity key changed. Verify this contact before sending anything sensitive.", L"09:14", false, kSent, L"", true},
      {kMsg, L"m-jonas", L"Seen. I'll verify Bo out of band before the release call.", L"09:19", false, kSent, L"", false},
      {kMsg, L"", L"Do not clear that record - it stays in the thread.", L"09:25", true, kDeliv, L"", false},
      {kMsg, L"m-amara", L"It is non-dismissible by design, per the spec.", L"09:33", false, kSent, L"", false},
      {kMsg, L"m-kai", L"Epoch advanced to 4182 an hour ago.", L"09:41", false, kSent, L"", false},
      {kMsg, L"", L"Confirmed on this device.", L"09:48", true, kRead, L"", false},
      {kMsg, L"m-priya", L"Records per second is holding at 12.", L"09:56", false, kSent, L"", false},
      {kMsg, L"m-oskar", L"That is the number the advanced strip shows.", L"10:04", false, kSent, L"", false},
      {kMsg, L"", L"I'll fold the strip figures into the demo build.", L"10:12", true, kSent, L"", false},
      {kMsg, L"m-lucia", L"Ping me when it is up.", L"10:20", false, kSent, L"", false},
      {kMsg, L"m-tobias", L"Same here.", L"10:29", false, kSent, L"", false},
      {kMsg, L"", L"Will do.", L"10:36", true, kDeliv, L"", false},
    });

    w.conversations.push_back(std::move(c0));
    w.conversations.push_back(std::move(c1));
  ```

- [ ] **Step 7: `DemoWorld.cpp` part 5 — the six DMs, the relay path and the server.** Continue `BuildWorld()` from Step 6 and close it. Every DM gets 4 rows so no conversation opens onto an empty thread (design doc §9.1: "a dead-looking button is a demo bug").

  ```cpp
    struct DmSpec {
      const wchar_t* id; const wchar_t* person; const wchar_t* name;
      const wchar_t* preview; const wchar_t* time;
      int unread; bool muted; bool disappearing;
      const wchar_t* retention; const wchar_t* mediaRetention;
      RowSpec rows[4];
    };

    const DmSpec kDms[] = {
      {L"conv-bo", L"d-bo", L"Bo Nakamura", L"Got it. Will confirm after standup.", L"09:58",
       0, false, false, L"Kept until deleted", L"Media kept 30 days", {
        {kMsg, L"d-bo", L"Rotated my key this morning - you will see the record in core.", L"09:31", false, kSent, L"", false},
        {kMsg, L"", L"Saw it. Verifying out of band before the call.", L"09:39", true, kRead, L"", false},
        {kMsg, L"d-bo", L"Fingerprint is in my profile.", L"09:50", false, kSent, L"", false},
        {kMsg, L"", L"Got it. Will confirm after standup.", L"09:58", true, kDeliv, L"", false}}},
      {L"conv-freya", L"d-freya", L"Freya Nilsson", L"Bring the rail screenshots.", L"11:44",
       3, false, false, L"Kept until deleted", L"Media kept 30 days", {
        {kMsg, L"", L"Yes, 15:00 works.", L"11:12", true, kRead, L"", false},
        {kMsg, L"d-freya", L"I moved it to 15:30 - the room was taken.", L"11:29", false, kSent, L"", false},
        {kMsg, L"d-freya", L"Same agenda.", L"11:40", false, kSent, L"", false},
        {kMsg, L"d-freya", L"Bring the rail screenshots.", L"11:44", false, kSent, L"", false}}},
      {L"conv-idris", L"d-idris", L"Idris Bello", L"I'll try it tonight.", L"10:47",
       0, false, false, L"Kept until deleted", L"Media kept 30 days", {
        {kMsg, L"d-idris", L"The Linux build finally links.", L"10:21", false, kSent, L"", false},
        {kMsg, L"", L"Good. Which toolchain?", L"10:30", true, kRead, L"", false},
        {kMsg, L"d-idris", L"Clang 19, nothing exotic.", L"10:38", false, kSent, L"", false},
        {kMsg, L"", L"I'll try it tonight.", L"10:47", true, kDeliv, L"", false}}},
      {L"conv-marta", L"d-marta", L"Marta Kowalska", L"Let me know before it goes.", L"09:12",
       0, false, true, L"Disappears after 24 hours", L"Media disappears with the message", {
        {kMsg, L"d-marta", L"Sending the draft - it disappears in 24 hours.", L"08:35", false, kSent, L"", false},
        {kMsg, L"", L"Reading it now.", L"08:49", true, kRead, L"", false},
        {kMsg, L"d-marta", L"Let me know before it goes.", L"09:04", false, kSent, L"", false},
        {kMsg, L"", L"Will do.", L"09:12", true, kDeliv, L"", false}}},
      {L"conv-sam", L"d-sam", L"Sam Ortega", L"No problem.", L"Tue",
       0, true, false, L"Kept until deleted", L"Media kept 30 days", {
        {kMsg, L"d-sam", L"Muting this one, I am on call all week.", L"14:02", false, kSent, L"", false},
        {kMsg, L"", L"Understood.", L"14:09", true, kRead, L"", false},
        {kMsg, L"d-sam", L"I will still read it, just not chime in.", L"14:18", false, kSent, L"", false},
        {kMsg, L"", L"No problem.", L"14:26", true, kDeliv, L"", false}}},
      {L"conv-yuki", L"d-yuki", L"Yuki Tanaka", L"Any time.", L"Mon",
       0, false, false, L"Kept until deleted", L"Media kept 30 days", {
        {kMsg, L"d-yuki", L"Do you have the jurisdiction list?", L"16:41", false, kSent, L"", false},
        {kMsg, L"", L"Iceland, Switzerland, Canada.", L"16:52", true, kRead, L"", false},
        {kMsg, L"d-yuki", L"Perfect, thanks.", L"16:58", false, kSent, L"", false},
        {kMsg, L"", L"Any time.", L"17:03", true, kDeliv, L"", false}}},
    };

    size_t ci = 2;
    for (DmSpec const& dm : kDms) {
      Conversation c = MakeConversation(dm.id, ConversationKind::Direct, dm.name,
                                        {{dm.person, dm.name, false}},
                                        dm.preview, dm.time, dm.unread, dm.muted,
                                        dm.disappearing, dm.retention, dm.mediaRetention);
      AppendRows(c, ci, w.myDevices,
                 {dm.rows[0], dm.rows[1], dm.rows[2], dm.rows[3]});
      w.conversations.push_back(std::move(c));
      ++ci;
    }

    // EXACTLY THREE relay nodes, and no glyph is ever empty. The three
    // codepoints were RENDERED in Segoe Fluent Icons and looked at before they
    // were written down, not read off a list: E977 is the monitor+phone
    // "Devices" pair, E774 is the globe, E968 is the server tower.
    w.relayPath.push_back({L"This device", L"This computer", L"", 0, true});   // Devices
    w.relayPath.push_back({L"URnetwork", L"3 hops", L"", 41, true});           // Globe
    w.relayPath.push_back({L"Message server", L"urmsg-01.ur.io", L"", 18, true}); // Server

    w.server = {L"urmsg-01.ur.io", L"Iceland", 59, true};
    return w;
  }

  }  // namespace

  const World& GetWorld() { return MutableWorld(); }

  World& MutableWorld() {
    // Built once. A const-ref-only accessor is what ambient activity cannot
    // use, so both accessors name the SAME object: a second copy would let the
    // rail cache pointers into storage the append invalidates.
    static World world = BuildWorld();
    return world;
  }

  }  // namespace urmsg::demo
  ```

- [ ] **Step 8: Build.**

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\build-local.ps1
  ```

  Expected last line: `OK in <n>s -> ...\app\build\x64\Release\`. A `C1083 'winrt/...'` here means the `NotUsing` metadata from Step 3 is missing or a winrt include crept in.

- [ ] **Step 9: Prove the file is genuinely pure.** No winrt token may appear in either file:

  ```
  powershell -Command "Select-String -Path app\src\App\Demo\DemoWorld.* -Pattern 'winrt|Microsoft\.UI|pch\.h' | Measure-Object | Select-Object -ExpandProperty Count"
  ```

  Expected: `0`.

- [ ] **Step 10: Commit.**

  ```
  powershell -Command "(git ls-files | Measure-Object).Count"
  git add app/src/App/Demo/DemoWorld.h app/src/App/Demo/DemoWorld.cpp app/src/App/App.vcxproj
  powershell -Command "(git ls-files | Measure-Object).Count"
  git commit
  ```

  Expected: **63** before `git add`, **65** after. Commit message: `demo: the seeded fabricated world (8 conversations, 66 rows, 3 devices)`, plus the standard trailers.

**Deliverable:** the solution builds with `Demo/DemoWorld.cpp` compiled standalone, and `Select-String` for winrt over both files returns 0. F3 is the task that proves the CONTENTS.

---

## Task F3: The ten --diagnose invariants for DemoWorld

**Files:**

Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Startup.cpp

**Interfaces:**

- Consumes: F2: `urmsg::demo::GetWorld()` and every type in `Demo/DemoWorld.h`.
- Produces: `CollectDiagnostics()` (declared `Startup.h:44`, unchanged signature) appends a `demo world` header line plus ten `I1..I10` lines, each `PASS`/`FAIL` with counts. Adds a file-local `WorldFingerprint(World const&)` and `constexpr uint64_t kExpectedWorldFingerprint` in Startup.cpp's anonymous namespace. Later tasks that change the world MUST update that constant in the same commit — I10 is the gate that makes an unreviewed world change visible.

# F3 — the `--diagnose` assertions

There is **no test framework in this repo and this task must not invent one.** The harness is `CollectDiagnostics()` + `URmessage.exe --diagnose`, exactly as it stands.

**On invariant 10.** Contract §1.1 asks that "two consecutive `GetWorld()` calls return identical content". `GetWorld()` hands back a function-local static, so that is satisfied by returning the same object and proves nothing — precisely the vacuous pass §1.1 forbids two paragraphs later. So I10 is implemented as a content **fingerprint** compared against a compiled-in expectation. That fails on a clock, an address, an RNG *or* an unreviewed edit, and it checks determinism **across processes**, which is the property the demo actually needs.

- [ ] **Step 1: Include the world and add the fingerprint.** In `app/src/App/Startup.cpp`, add `#include "Demo/DemoWorld.h"` to the include block (after `"Strings.h"`), then add to the existing anonymous namespace, above its closing `}  // namespace`:

  ```cpp
  // ---- the demo world's ten invariants (contract section 1.1) ----------------
  //
  // These run on EVERY launch, not only under --diagnose: CollectDiagnostics is
  // called at main.cpp:168 and LogDiagnostics writes every line to the log, so a
  // world that broke overnight leaves evidence in the log of the run that broke
  // it rather than waiting for someone to think to ask.

  void MixU64(uint64_t& h, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
      h ^= (v >> (i * 8)) & 0xFFull;
      h *= 1099511628211ull;
    }
  }
  void MixStr(uint64_t& h, std::wstring const& s) {
    MixU64(h, static_cast<uint64_t>(s.size()));
    for (wchar_t c : s) MixU64(h, static_cast<uint64_t>(static_cast<uint16_t>(c)));
  }
  void MixSeed(uint64_t& h, urmsg::demo::Seed const& s) {
    for (uint8_t b : s) MixU64(h, static_cast<uint64_t>(b));
  }

  uint64_t WorldFingerprint(urmsg::demo::World const& w) {
    uint64_t h = 1469598103934665603ull;
    auto mixDevice = [&h](urmsg::demo::DeviceRef const& d) {
      MixStr(h, d.id); MixStr(h, d.name); MixStr(h, d.ownerName);
      MixSeed(h, d.ownerKey); MixStr(h, d.lastSeenLabel);
      MixU64(h, d.online ? 1u : 0u); MixU64(h, d.isThisComputer ? 1u : 0u);
    };
    for (auto const& c : w.conversations) {
      MixStr(h, c.id); MixU64(h, static_cast<uint64_t>(c.kind)); MixStr(h, c.name);
      MixSeed(h, c.identityKey); MixStr(h, c.groupIdHex); MixStr(h, c.preview);
      MixStr(h, c.timeLabel); MixU64(h, static_cast<uint64_t>(c.unread));
      MixU64(h, c.muted ? 1u : 0u); MixU64(h, c.disappearing ? 1u : 0u);
      MixU64(h, static_cast<uint64_t>(c.memberCount));
      MixStr(h, c.retentionLabel); MixStr(h, c.mediaRetentionLabel);
      for (auto const& m : c.members) {
        MixStr(h, m.id); MixStr(h, m.displayName); MixSeed(h, m.identityKey);
        MixU64(h, m.admin ? 1u : 0u);
        for (auto const& d : m.devices) mixDevice(d);
      }
      for (auto const& r : c.rows) {
        MixU64(h, static_cast<uint64_t>(r.kind)); MixStr(h, r.id);
        MixStr(h, r.senderName); MixSeed(h, r.senderKey); MixStr(h, r.body);
        MixStr(h, r.timeLabel); MixU64(h, r.outgoing ? 1u : 0u);
        MixU64(h, static_cast<uint64_t>(r.state)); MixStr(h, r.failureReason);
        MixStr(h, r.systemText); MixU64(h, r.permanentRecord ? 1u : 0u);
        auto const& n = r.inspect;
        MixU64(h, n.epoch); MixU64(h, n.senderLeafIndex);
        MixU64(h, static_cast<uint64_t>(n.retention)); MixStr(h, n.sizeBucket);
        MixU64(h, n.wireSizeBytes); MixU64(h, n.attestationVerified ? 1u : 0u);
        MixStr(h, n.cipher); MixStr(h, n.groupIdHex); MixStr(h, n.senderDisplayName);
        MixStr(h, n.sentAtLabel); MixStr(h, n.receivedAtLabel);
        for (auto const& d : n.deliveredTo) mixDevice(d);
        for (auto const& d : n.readBy) mixDevice(d);
      }
    }
    for (auto const& d : w.myDevices) mixDevice(d);
    for (auto const& n : w.relayPath) {
      MixStr(h, n.label); MixStr(h, n.subLabel); MixStr(h, n.glyph);
      MixU64(h, static_cast<uint64_t>(n.hopMs)); MixU64(h, n.healthy ? 1u : 0u);
    }
    MixStr(h, w.server.host); MixStr(h, w.server.jurisdiction);
    MixU64(h, static_cast<uint64_t>(w.server.latencyMs));
    MixU64(h, w.server.keyVerified ? 1u : 0u);
    MixU64(h, w.currentEpoch); MixU64(h, static_cast<uint64_t>(w.connectState));
    MixStr(h, w.sessionMode); MixU64(h, static_cast<uint64_t>(w.recordsPerSecond));
    return h;
  }

  // Filled in by Step 4, from the value the first run prints. A task that
  // deliberately changes the world updates this in the SAME commit; anything
  // else that changes it is the bug this line exists to catch.
  constexpr uint64_t kExpectedWorldFingerprint = 0ull;

  std::wstring Verdict(bool ok) { return ok ? L"PASS" : L"FAIL"; }
  ```

- [ ] **Step 2: Add the ten assertions.** Still in the anonymous namespace, after `Verdict`:

  ```cpp
  std::vector<std::wstring> DemoWorldAssertions() {
    using namespace urmsg::demo;
    World const& w = GetWorld();
    std::vector<std::wstring> out;

    size_t rows = 0, members = 0, groups = 0, direct = 0;
    for (auto const& c : w.conversations) {
      rows += c.rows.size();
      members += c.members.size();
      (c.kind == ConversationKind::Group ? groups : direct) += 1;
    }
    out.push_back(std::format(
        L"  demo world       : {} conversations, {} rows, {} members, {} devices",
        w.conversations.size(), rows, members, w.myDevices.size()));

    // I1
    const bool i1 = (w.conversations.size() == 8 && groups == 2);
    out.push_back(std::format(L"    I1  conversation count   {}  {} total, {} group, {} direct",
                              Verdict(i1), w.conversations.size(), groups, direct));
    // I2
    size_t i2Agree = 0;
    for (auto const& c : w.conversations)
      if (c.memberCount == static_cast<int>(c.members.size())) ++i2Agree;
    out.push_back(std::format(L"    I2  memberCount agrees   {}  {}/{} conversations, {} members counted",
                              Verdict(i2Agree == w.conversations.size()), i2Agree,
                              w.conversations.size(), members));
    // I3 - the timer REPLACES a preview that exists, so the rule is falsifiable
    size_t i3Count = 0;
    bool i3Preview = false;
    for (auto const& c : w.conversations)
      if (c.disappearing) { ++i3Count; i3Preview = !c.preview.empty(); }
    out.push_back(std::format(L"    I3  one disappearing     {}  {} of {} disappearing, preview {}",
                              Verdict(i3Count == 1 && i3Preview), i3Count,
                              w.conversations.size(),
                              i3Preview ? L"non-empty" : L"EMPTY"));
    // I4
    size_t i4 = 0;
    for (auto const& c : w.conversations) if (!c.preview.empty()) ++i4;
    out.push_back(std::format(L"    I4  previews non-empty   {}  {}/{} non-empty",
                              Verdict(i4 == w.conversations.size()), i4, w.conversations.size()));
    // I5
    size_t pend = 0, sent = 0, deliv = 0, read = 0, failed = 0, expired = 0, reasons = 0;
    for (auto const& c : w.conversations)
      for (auto const& r : c.rows) {
        if (r.kind != RowKind::Message || !r.outgoing) continue;
        switch (r.state) {
          case DeliveryState::Pending:   ++pend; break;
          case DeliveryState::Sent:      ++sent; break;
          case DeliveryState::Delivered: ++deliv; break;
          case DeliveryState::Read:      ++read; break;
          case DeliveryState::Failed:    ++failed; if (!r.failureReason.empty()) ++reasons; break;
          case DeliveryState::Expired:   ++expired; break;
        }
      }
    const size_t outgoing = pend + sent + deliv + read + failed + expired;
    const bool i5 = (pend && sent && deliv && read && failed == 1 && reasons == 1);
    out.push_back(std::format(
        L"    I5  delivery spread     {}  {} outgoing: pending {} sent {} delivered {} "
        L"read {} failed {} expired {}; {} failure reason",
        Verdict(i5), outgoing, pend, sent, deliv, read, failed, expired, reasons));
    // I6
    size_t systemRows = 0, permanent = 0;
    for (auto const& c : w.conversations)
      for (auto const& r : c.rows)
        if (r.kind == RowKind::System) { ++systemRows; if (r.permanentRecord) ++permanent; }
    out.push_back(std::format(L"    I6  key-change record   {}  {} permanent of {} system rows",
                              Verdict(permanent >= 1), permanent, systemRows));
    // I7
    size_t emptyGlyphs = 0;
    for (auto const& n : w.relayPath) if (n.glyph.empty()) ++emptyGlyphs;
    out.push_back(std::format(L"    I7  relay path          {}  {} nodes, {} empty glyphs",
                              Verdict(w.relayPath.size() == 3 && emptyGlyphs == 0),
                              w.relayPath.size(), emptyGlyphs));
    // I8
    size_t thisComputer = 0;
    for (auto const& d : w.myDevices) if (d.isThisComputer) ++thisComputer;
    const bool i8 = (thisComputer == 1 && !w.myDevices.empty() && w.myDevices[0].isThisComputer);
    out.push_back(std::format(L"    I8  this computer       {}  {} of {} devices, index 0 is {}",
                              Verdict(i8), thisComputer, w.myDevices.size(),
                              (!w.myDevices.empty() && w.myDevices[0].isThisComputer)
                                  ? L"this computer" : L"NOT this computer"));
    // I9
    size_t withGroupId = 0;
    for (auto const& c : w.conversations)
      if (c.kind == ConversationKind::Group && !c.groupIdHex.empty()) ++withGroupId;
    out.push_back(std::format(L"    I9  group ids           {}  {}/{} groups carry a group id",
                              Verdict(withGroupId == groups), withGroupId, groups));
    // I10
    const uint64_t fp = WorldFingerprint(w);
    out.push_back(std::format(
        L"    I10 determinism         {}  fingerprint 0x{:016X}, expected 0x{:016X}",
        Verdict(fp == kExpectedWorldFingerprint), fp, kExpectedWorldFingerprint));
    return out;
  }
  ```

- [ ] **Step 3: Append them in `CollectDiagnostics()`.** In `Startup.cpp`, immediately before the closing `return lines;` of `CollectDiagnostics()` (currently `Startup.cpp:217`):

  ```cpp
    for (auto& line : DemoWorldAssertions()) lines.push_back(std::move(line));
  ```

- [ ] **Step 4: Build, run, and read the fingerprint out of the FAIL line.**

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\build-local.ps1
  powershell -Command "$r='.'; $env:URMESSAGE_APP_ROOT=(Resolve-Path .\.localstate-verify); Start-Process -FilePath .\app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -NoNewWindow -Wait -RedirectStandardOutput .\.verify\diagnose.txt -RedirectStandardError .\.verify\diagnose.err.txt; Get-Content .\.verify\diagnose.txt -Encoding UTF8"
  ```

  `-Encoding UTF8` is not optional: the app writes UTF-8 bytes to a redirected handle and PowerShell 5.1 otherwise renders the em dashes as `â€”`.

  Expected — I1..I9 already PASS, I10 FAILs and *names the value to paste*:

  ```
    demo world       : 8 conversations, 66 rows, 22 members, 3 devices
      I1  conversation count   PASS  8 total, 2 group, 6 direct
      I2  memberCount agrees   PASS  8/8 conversations, 22 members counted
      I3  one disappearing     PASS  1 of 8 disappearing, preview non-empty
      I4  previews non-empty   PASS  8/8 non-empty
      I5  delivery spread      PASS  24 outgoing: pending 1 sent 2 delivered 9 read 11 failed 1 expired 0; 1 failure reason
      I6  key-change record    PASS  1 permanent of 3 system rows
      I7  relay path           PASS  3 nodes, 0 empty glyphs
      I8  this computer        PASS  1 of 3 devices, index 0 is this computer
      I9  group ids            PASS  2/2 groups carry a group id
      I10 determinism          FAIL  fingerprint 0x<SIXTEEN HEX DIGITS>, expected 0x0000000000000000
  ```

  If any of I1..I9 says FAIL, the count on that line names the discrepancy — fix `DemoWorld.cpp`, not the assertion.

- [ ] **Step 5: Paste the fingerprint and re-run.** Set `kExpectedWorldFingerprint` to the literal I10 printed:

  ```cpp
  constexpr uint64_t kExpectedWorldFingerprint = 0x<SIXTEEN HEX DIGITS>ull;
  ```

  Rebuild and re-run the same two commands. Expected — the last line only:

  ```
      I10 determinism          PASS  fingerprint 0x<SAME>, expected 0x<SAME>
  ```

  The **FAIL → PASS transition is the proof the gate is not vacuous**: it was capable of failing, and one edit made it pass.

- [ ] **Step 6: Prove it is stable across processes.** Run the capture from Step 4 three more times and diff:

  ```
  powershell -Command "1..3 | ForEach-Object { Start-Process -FilePath .\app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -NoNewWindow -Wait -RedirectStandardOutput \".\.verify\d$_.txt\" -RedirectStandardError .\.verify\d.err.txt }; (Get-Content .\.verify\d1.txt -Encoding UTF8 | Select-String 'I10'), (Get-Content .\.verify\d2.txt -Encoding UTF8 | Select-String 'I10'), (Get-Content .\.verify\d3.txt -Encoding UTF8 | Select-String 'I10')"
  ```

  Expected: three byte-identical `I10 determinism          PASS  fingerprint 0x<SAME>, expected 0x<SAME>` lines. Three different pids, one fingerprint — that is what "same bytes on every launch" means.

- [ ] **Step 7: Commit.**

  ```
  powershell -Command "(git ls-files | Measure-Object).Count"
  git status --short
  git add app/src/App/Startup.cpp
  git commit
  ```

  Expected: **65**, and `git status --short` shows only `M app/src/App/Startup.cpp` (`.verify/` is gitignored). Message: `demo: assert the world's ten invariants in --diagnose`, plus trailers.

**Deliverable:** `URmessage.exe --diagnose` prints ten `I1..I10` lines, all `PASS`, each carrying a count; three consecutive runs print the same fingerprint.

---

## Task F4: Demo/DemoSwitches.h/.cpp — --demo parsing with a compile-time parse table

**Files:**

Create: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/DemoSwitches.h
Create: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/DemoSwitches.cpp
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.vcxproj
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Startup.cpp

**Interfaces:**

- Consumes: F1 (`-AppArgs`), F3 (the `CollectDiagnostics()` append point).
- Produces: `Demo/DemoSwitches.h`, namespace `urmsg::demo`: `enum class DemoScreen { None, Chats, Thread, Inspect, Network, Settings, Developer };`, `struct DemoOptions { bool enabled; DemoScreen screen; bool autoplay; bool advanced; bool watermark; };`, `DemoOptions ParseDemoOptions();`. Defaults: `{false, DemoScreen::None, false, false, true}`. Any demo switch implies `enabled`. `--demo=<unknown>` is NOT a demo switch and leaves `enabled` false. `CollectDiagnostics()` gains one `demo switches` line.

# F4 — `Demo/DemoSwitches.h/.cpp`

Shaped like `urnw::WantsDiagnose()` (`Startup.cpp:280`): `CommandLineToArgvW`, accepting `--x`, `-x`, `/x` and a bare `x`.

- [ ] **Step 1: Create `app/src/App/Demo/DemoSwitches.h`.**

  ```cpp
  // The --demo family, parsed in the shape of urnw::WantsDiagnose()
  // (Startup.cpp:280): CommandLineToArgvW, and --x / -x / /x / bare x all
  // accepted, because a switch that works one way and not another is a bug
  // report waiting to be filed.
  //
  // No winrt here either: ParseDemoOptions is called from CollectDiagnostics,
  // which runs before winrt::init_apartment.
  //
  // SPDX-License-Identifier: MPL-2.0
  #pragma once

  namespace urmsg::demo {

  enum class DemoScreen { None, Chats, Thread, Inspect, Network, Settings, Developer };

  struct DemoOptions {
    bool enabled;        // --demo, or implied by any other demo switch
    DemoScreen screen;
    bool autoplay;       // --demo-autoplay
    bool advanced;       // --demo-advanced, session-only, never written to prefs
    bool watermark;      // false only when --demo-watermark=off
  };

  // Inspect is a STATE, not a screen: it opens Chats, selects conversation 0,
  // selects DemoWorld's kInspectTargetRowId, and puts the rail in message mode.
  DemoOptions ParseDemoOptions();

  }  // namespace urmsg::demo
  ```

- [ ] **Step 2: Create `app/src/App/Demo/DemoSwitches.cpp` — the constexpr core.**

  ```cpp
  // SPDX-License-Identifier: MPL-2.0
  #include "pch.h"

  #include "Demo/DemoSwitches.h"

  #include <shellapi.h>  // CommandLineToArgvW

  #include <initializer_list>
  #include <string_view>

  namespace urmsg::demo {
  namespace {

  constexpr DemoOptions kDefaults{false, DemoScreen::None, false, false, true};

  // The switch BODY with its prefix removed, or the argument unchanged when it
  // carries no prefix (the bare form). Same four spellings WantsDiagnose takes.
  constexpr std::wstring_view SwitchBody(std::wstring_view arg) {
    if (arg.starts_with(L"--")) return arg.substr(2);
    if (arg.starts_with(L"-")) return arg.substr(1);
    if (arg.starts_with(L"/")) return arg.substr(1);
    return arg;
  }

  constexpr DemoScreen ScreenFromName(std::wstring_view name) {
    if (name == L"chats") return DemoScreen::Chats;
    if (name == L"thread") return DemoScreen::Thread;
    if (name == L"inspect") return DemoScreen::Inspect;
    if (name == L"network") return DemoScreen::Network;
    if (name == L"settings") return DemoScreen::Settings;
    if (name == L"developer") return DemoScreen::Developer;
    return DemoScreen::None;
  }

  constexpr void ApplyArg(DemoOptions& o, std::wstring_view arg) {
    const std::wstring_view body = SwitchBody(arg);
    if (body == L"demo") { o.enabled = true; return; }
    if (body.starts_with(L"demo=")) {
      const DemoScreen s = ScreenFromName(body.substr(5));
      // An unknown screen name is NOT a demo switch. Enabling the demo on a
      // typo would open the fabricated world when the operator asked for
      // something else, which is the one failure mode a demo build must not
      // have.
      if (s == DemoScreen::None) return;
      o.enabled = true;
      o.screen = s;
      return;
    }
    if (body == L"demo-autoplay") { o.enabled = true; o.autoplay = true; return; }
    if (body == L"demo-advanced") { o.enabled = true; o.advanced = true; return; }
    // Only "=off" turns the watermark off. Any other value leaves it ON: an
    // unrecognised argument must never be the thing that strips the mark from a
    // screenshot.
    if (body == L"demo-watermark=off") { o.enabled = true; o.watermark = false; return; }
  }

  constexpr DemoOptions ParseArgs(std::initializer_list<std::wstring_view> args) {
    DemoOptions o = kDefaults;
    for (std::wstring_view a : args) ApplyArg(o, a);
    return o;
  }

  }  // namespace
  ```

- [ ] **Step 3: The parse table, as `static_assert`.** Append after the anonymous namespace's closing brace, still inside `urmsg::demo`. Thirty-one cases the COMPILER checks — a regression here cannot reach a screenshot, because the build fails first.

  ```cpp
  // THE PARSE TABLE. Checked at compile time, so it costs nothing at runtime
  // and cannot rot: change ApplyArg and the build tells you which row moved.
  static_assert(!ParseArgs({}).enabled);
  static_assert(ParseArgs({}).screen == DemoScreen::None);
  static_assert(ParseArgs({}).watermark);
  static_assert(!ParseArgs({}).autoplay);
  static_assert(!ParseArgs({}).advanced);

  static_assert(ParseArgs({L"--demo"}).enabled);
  static_assert(ParseArgs({L"-demo"}).enabled);
  static_assert(ParseArgs({L"/demo"}).enabled);
  static_assert(ParseArgs({L"demo"}).enabled);
  static_assert(ParseArgs({L"--demo"}).screen == DemoScreen::None);

  static_assert(ParseArgs({L"--demo=chats"}).screen == DemoScreen::Chats);
  static_assert(ParseArgs({L"--demo=thread"}).screen == DemoScreen::Thread);
  static_assert(ParseArgs({L"--demo=inspect"}).screen == DemoScreen::Inspect);
  static_assert(ParseArgs({L"--demo=network"}).screen == DemoScreen::Network);
  static_assert(ParseArgs({L"--demo=settings"}).screen == DemoScreen::Settings);
  static_assert(ParseArgs({L"--demo=developer"}).screen == DemoScreen::Developer);
  static_assert(ParseArgs({L"/demo=inspect"}).screen == DemoScreen::Inspect);
  static_assert(ParseArgs({L"demo=network"}).screen == DemoScreen::Network);
  static_assert(ParseArgs({L"--demo=inspect"}).enabled);

  static_assert(ParseArgs({L"--demo=nope"}).screen == DemoScreen::None);
  static_assert(!ParseArgs({L"--demo=nope"}).enabled);

  static_assert(ParseArgs({L"--demo-autoplay"}).autoplay);
  static_assert(ParseArgs({L"--demo-autoplay"}).enabled);
  static_assert(!ParseArgs({L"--demo"}).autoplay);

  static_assert(ParseArgs({L"--demo-advanced"}).advanced);
  static_assert(ParseArgs({L"--demo-advanced"}).enabled);
  static_assert(!ParseArgs({L"--demo"}).advanced);

  static_assert(!ParseArgs({L"--demo-watermark=off"}).watermark);
  static_assert(ParseArgs({L"--demo-watermark=off"}).enabled);
  static_assert(ParseArgs({L"--demo-watermark=maybe"}).watermark);

  static_assert(!ParseArgs({L"--diagnose"}).enabled);

  DemoOptions ParseDemoOptions() {
    DemoOptions out = kDefaults;
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return out;
    for (int i = 1; i < argc; ++i) ApplyArg(out, std::wstring_view{argv[i]});
    ::LocalFree(argv);
    return out;
  }

  }  // namespace urmsg::demo
  ```

- [ ] **Step 4: Register in `App.vcxproj`.** Beside the F2 entries:

  ```xml
  <ClCompile Include="Demo\DemoSwitches.cpp" />
  ```
  ```xml
  <ClInclude Include="Demo\DemoSwitches.h" />
  ```

- [ ] **Step 5: Print the live parse in `--diagnose`.** In `Startup.cpp`, add `#include "Demo/DemoSwitches.h"` beside the F3 include, then insert this in `CollectDiagnostics()` immediately BEFORE the `DemoWorldAssertions()` loop from F3:

  ```cpp
    {
      const urmsg::demo::DemoOptions o = urmsg::demo::ParseDemoOptions();
      constexpr const wchar_t* kScreens[] = {L"none", L"chats", L"thread", L"inspect",
                                             L"network", L"settings", L"developer"};
      lines.push_back(std::format(
          L"  demo switches    : enabled={} screen={} autoplay={} advanced={} watermark={}",
          o.enabled ? L"yes" : L"no", kScreens[static_cast<size_t>(o.screen)],
          o.autoplay ? L"yes" : L"no", o.advanced ? L"yes" : L"no",
          o.watermark ? L"on" : L"off"));
    }
  ```

- [ ] **Step 6: Build. The static_asserts are the first gate.**

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\build-local.ps1
  ```

  Expected: `OK in <n>s -> ...`. A `static_assert failed` here names the exact table row that broke.

- [ ] **Step 7: Run the table through the real command line.** The static_asserts test `ApplyArg`; this tests `CommandLineToArgvW` and the shell quoting with it. Six invocations:

  ```
  powershell -Command "$exe='.\app\build\x64\Release\URmessage.exe'; $cases=@('','--demo','--demo=inspect','/demo=network','--demo-advanced --demo-autoplay','--demo --demo-watermark=off'); foreach ($c in $cases) { $a=@('--diagnose'); if ($c) { $a += $c.Split(' ') }; Start-Process -FilePath $exe -ArgumentList $a -NoNewWindow -Wait -RedirectStandardOutput .\.verify\sw.txt -RedirectStandardError .\.verify\sw.err.txt; '{0,-38} {1}' -f \"[$c]\", ((Get-Content .\.verify\sw.txt -Encoding UTF8 | Select-String 'demo switches').ToString().Trim()) }"
  ```

  Expected, exactly six lines:

  ```
  []                                     demo switches    : enabled=no screen=none autoplay=no advanced=no watermark=on
  [--demo]                               demo switches    : enabled=yes screen=none autoplay=no advanced=no watermark=on
  [--demo=inspect]                       demo switches    : enabled=yes screen=inspect autoplay=no advanced=no watermark=on
  [/demo=network]                        demo switches    : enabled=yes screen=network autoplay=no advanced=no watermark=on
  [--demo-advanced --demo-autoplay]      demo switches    : enabled=yes screen=none autoplay=yes advanced=yes watermark=on
  [--demo --demo-watermark=off]          demo switches    : enabled=yes screen=none autoplay=no advanced=no watermark=off
  ```

  The first line is the one that matters most: a plain `--diagnose` must leave the demo **off**.

- [ ] **Step 8: Commit.**

  ```
  powershell -Command "(git ls-files | Measure-Object).Count"
  git add app/src/App/Demo/DemoSwitches.h app/src/App/Demo/DemoSwitches.cpp app/src/App/App.vcxproj app/src/App/Startup.cpp
  powershell -Command "(git ls-files | Measure-Object).Count"
  git commit
  ```

  Expected: **65** before, **67** after. Message: `demo: --demo switch parsing, with the parse table checked at compile time`, plus trailers.

**Deliverable:** the build enforces 31 parse cases, and six real invocations print the six expected `demo switches` lines.

---

## Task F5: Identicon.h/.cpp — deterministic, own CornerRadius(8), palette proved non-state

**Files:**

Create: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Identicon.h
Create: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Identicon.cpp
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.vcxproj
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Startup.cpp

**Interfaces:**

- Consumes: F2 (`urmsg::demo::Seed`), F3 (the `CollectDiagnostics()` append point).
- Produces: `Identicon.h`, namespace `urmsg`: `winrt::Microsoft::UI::Xaml::Controls::Border MakeIdenticon(urmsg::demo::Seed const& seed, double size = 40);` — applies its OWN `CornerRadius(8)`, callers must not set one. Plus one added pure declaration, needed because `MakeIdenticon` constructs WinRT and so cannot be called from `CollectDiagnostics()`: `struct IdenticonPattern { std::array<bool,25> cells; size_t colorIndex; };` and `IdenticonPattern MakeIdenticonPattern(urmsg::demo::Seed const& seed);`. `CollectDiagnostics()` gains four `identicon` assertion lines.

# F5 — `Identicon.h/.cpp`

Spec C §W9. Deterministic from a 32-byte identity key; 40×40; corner radius 8.

**The palette constraint is stated in code, not in a comment.** Every entry is a `constexpr` function of an existing `UrColors.h` colour, and a `static_assert` proves none of them equals `kDanger`, `kUrGreen`, `kToggleAccent`, `kStatusConnecting` or `kProGold`. `kProGold` appears there only as the thing the palette is proved **not** to be, which is the opposite of using it (contract §6 rule 4).

**One addition to contract §3, flagged.** `MakeIdenticon` constructs a `Border`, so it cannot run before `winrt::init_apartment()` — and `CollectDiagnostics()` is the only test harness this repo has. Splitting the pure derivation out is the same gap contract §1 already closed for `DemoWorld`. `MakeIdenticon`'s signature is untouched.

- [ ] **Step 1: Create `app/src/App/Identicon.h`.**

  ```cpp
  // Deterministic identicons (Spec C W9): a 5x5 mirrored pattern and one
  // palette colour, derived from a 32-byte identity key by arithmetic alone.
  // The property this buys the product for free: when a contact's key changes,
  // their avatar visibly changes.
  //
  // SPDX-License-Identifier: MPL-2.0
  #pragma once

  #include <array>
  #include <cstddef>

  #include <winrt/Microsoft.UI.Xaml.Controls.h>

  #include "Demo/DemoWorld.h"

  namespace urmsg {

  // The PURE half. Added beyond contract v2 section 3 and flagged as such:
  // MakeIdenticon below constructs a Border, so it cannot be called from
  // CollectDiagnostics(), which runs before winrt::init_apartment - and that is
  // the only harness this repo has. This is the same pre-apartment gap contract
  // section 1 already closed for DemoWorld. MakeIdenticon's signature is
  // unchanged; nothing was renamed and no struct gained a field.
  struct IdenticonPattern {
    std::array<bool, 25> cells{};  // row-major 5x5, mirrored left-to-right
    size_t colorIndex = 0;
  };
  IdenticonPattern MakeIdenticonPattern(urmsg::demo::Seed const& seed);

  // Applies its own CornerRadius(8) - callers must NOT set one.
  // Palette is constrained to hues that cannot be mistaken for state: it NEVER
  // emits kDanger, kUrGreen, kToggleAccent, kStatusConnecting or kProGold, and
  // the .cpp proves it with a static_assert rather than promising it.
  winrt::Microsoft::UI::Xaml::Controls::Border MakeIdenticon(
      urmsg::demo::Seed const& seed, double size = 40);

  }  // namespace urmsg
  ```

- [ ] **Step 2: Create `app/src/App/Identicon.cpp` — the palette and its proof.**

  ```cpp
  // SPDX-License-Identifier: MPL-2.0
  #include "pch.h"

  #include "Identicon.h"

  #include "UrColors.h"

  using namespace winrt::Microsoft::UI::Xaml;
  using namespace winrt::Microsoft::UI::Xaml::Controls;

  namespace urmsg {
  namespace {

  using Color = winrt::Windows::UI::Color;

  constexpr uint8_t Mix(uint8_t a, uint8_t b, uint8_t m) {
    return static_cast<uint8_t>((static_cast<int>(a) * (255 - m) + static_cast<int>(b) * m) / 255);
  }

  // A brand colour folded toward the card surface. constexpr, so the palette
  // below is a compile-time FUNCTION of UrColors.h and introduces no new brand
  // colour of its own - there is still exactly one palette in this app.
  constexpr Color Tint(Color c, uint8_t mix) {
    return Color{255, Mix(c.R, urnw::colors::kCard.R, mix),
                      Mix(c.G, urnw::colors::kCard.G, mix),
                      Mix(c.B, urnw::colors::kCard.B, mix)};
  }

  constexpr std::array<Color, 6> kPalette{
      Tint(urnw::colors::kUrPink, 48),          // orchid
      Tint(urnw::colors::kUrElectricBlue, 96),  // indigo, lifted off near-black
      Tint(urnw::colors::kUrAmber, 96),         // sand, pulled well off kProGold
      Tint(urnw::colors::kStatusIdle, 96),      // slate
      Tint(urnw::colors::kUrMutedCoral, 96),    // clay, pulled well off kDanger
      Tint(urnw::colors::kAccent, 128),         // pale olive, not the accent
  };

  constexpr bool SameColor(Color a, Color b) {
    return a.A == b.A && a.R == b.R && a.G == b.G && a.B == b.B;
  }

  // THE CONSTRAINT, AS CODE. An avatar that happens to be the danger red, the
  // "on" green, the toggle blue, the connecting yellow or the Pro gold is a
  // colour that MEANS something else in this product, and a person cannot tell
  // a decorative use from a semantic one. kProGold is named here only as a
  // thing the palette is proved NOT to be - the opposite of spending it.
  constexpr bool PaletteAvoidsStateColors() {
    for (Color c : kPalette) {
      if (SameColor(c, urnw::colors::kDanger)) return false;
      if (SameColor(c, urnw::colors::kUrGreen)) return false;
      if (SameColor(c, urnw::colors::kToggleAccent)) return false;
      if (SameColor(c, urnw::colors::kStatusConnecting)) return false;
      if (SameColor(c, urnw::colors::kProGold)) return false;
    }
    return true;
  }
  static_assert(PaletteAvoidsStateColors(),
                "an identicon hue collides with a colour that carries state");

  }  // namespace
  ```

- [ ] **Step 3: `Identicon.cpp` — the pure derivation.** Append inside `urmsg`, above the closing brace:

  ```cpp
  IdenticonPattern MakeIdenticonPattern(urmsg::demo::Seed const& seed) {
    IdenticonPattern out;
    // 15 independent cells (columns 0..2), mirrored into columns 3..4. The
    // mirror is what makes an identicon read as a MARK rather than as noise.
    // One seed byte per cell, so a one-byte key change moves at least one cell.
    for (int row = 0; row < 5; ++row) {
      for (int col = 0; col < 3; ++col) {
        const size_t byte = static_cast<size_t>(row * 3 + col);
        const bool on = (seed[byte] & 0x01u) != 0;
        out.cells[static_cast<size_t>(row * 5 + col)] = on;
        out.cells[static_cast<size_t>(row * 5 + (4 - col))] = on;
      }
    }
    // An all-on or all-off identicon is a bug, not a rare draw: force the
    // centre column of the middle row on, and the two corners off, so every
    // pattern has between 1 and 23 set cells whatever the key is.
    out.cells[12] = true;
    out.cells[0] = false;
    out.cells[4] = false;
    out.colorIndex = static_cast<size_t>(seed[31] % kPalette.size());
    return out;
  }
  ```

- [ ] **Step 4: `Identicon.cpp` — the XAML half.** Append after Step 3, still inside `urmsg`:

  ```cpp
  Border MakeIdenticon(urmsg::demo::Seed const& seed, double size) {
    const IdenticonPattern pattern = MakeIdenticonPattern(seed);
    const auto hue = kPalette[pattern.colorIndex];

    Border root;
    root.Width(size);
    root.Height(size);
    // ITS OWN radius. Callers must not set one, so that every identicon in the
    // app is the same shape whatever surface it lands on. Border clips its
    // Child to this radius, which is what keeps the corner cells inside it.
    root.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
    root.Background(urnw::colors::MakeBrush(urnw::colors::WithAlpha(hue, 0x33)));

    Grid grid;
    for (int i = 0; i < 5; ++i) {
      RowDefinition r;
      r.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
      grid.RowDefinitions().Append(r);
      ColumnDefinition c;
      c.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
      grid.ColumnDefinitions().Append(c);
    }
    auto fill = urnw::colors::MakeBrush(hue);
    for (int row = 0; row < 5; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (!pattern.cells[static_cast<size_t>(row * 5 + col)]) continue;
        Border cell;
        cell.Background(fill);
        Grid::SetRow(cell, row);
        Grid::SetColumn(cell, col);
        grid.Children().Append(cell);
      }
    }
    root.Child(grid);
    return root;
  }

  }  // namespace urmsg
  ```

- [ ] **Step 5: Register in `App.vcxproj`.**

  ```xml
  <ClCompile Include="Identicon.cpp" />
  ```
  ```xml
  <ClInclude Include="Identicon.h" />
  ```

- [ ] **Step 6: Assert the pure half in `--diagnose`.** In `Startup.cpp`, add `#include "Identicon.h"`, and insert after the `DemoWorldAssertions()` loop in `CollectDiagnostics()`:

  ```cpp
    {
      using namespace urmsg::demo;
      World const& w = GetWorld();
      std::vector<Seed> seeds;
      for (auto const& c : w.conversations) {
        seeds.push_back(c.identityKey);
        for (auto const& m : c.members) seeds.push_back(m.identityKey);
      }
      size_t stable = 0, symmetric = 0, inRange = 0;
      std::vector<std::array<bool, 25>> patterns;
      for (auto const& s : seeds) {
        const auto a = urmsg::MakeIdenticonPattern(s);
        const auto b = urmsg::MakeIdenticonPattern(s);
        if (a.cells == b.cells && a.colorIndex == b.colorIndex) ++stable;
        bool mirrored = true;
        size_t on = 0;
        for (int row = 0; row < 5; ++row)
          for (int col = 0; col < 5; ++col) {
            const bool v = a.cells[static_cast<size_t>(row * 5 + col)];
            if (v) ++on;
            if (v != a.cells[static_cast<size_t>(row * 5 + (4 - col))]) mirrored = false;
          }
        if (mirrored) ++symmetric;
        if (1 <= on && on <= 23) ++inRange;
        patterns.push_back(a.cells);
      }
      size_t distinct = 0;
      for (size_t i = 0; i < patterns.size(); ++i) {
        bool seen = false;
        for (size_t j = 0; j < i; ++j) if (patterns[j] == patterns[i]) seen = true;
        if (!seen) ++distinct;
      }
      const size_t n = seeds.size();
      lines.push_back(std::format(L"  identicons       : {} seeds from the demo world", n));
      lines.push_back(std::format(L"    P1  deterministic       {}  {}/{} seeds identical on a second call",
                                  n && stable == n ? L"PASS" : L"FAIL", stable, n));
      lines.push_back(std::format(L"    P2  mirrored            {}  {}/{} patterns symmetric",
                                  n && symmetric == n ? L"PASS" : L"FAIL", symmetric, n));
      lines.push_back(std::format(L"    P3  density in range    {}  {}/{} have 1..23 cells set",
                                  n && inRange == n ? L"PASS" : L"FAIL", inRange, n));
      lines.push_back(std::format(L"    P4  distinguishable     {}  {} distinct of {} patterns",
                                  distinct * 4 >= n * 3 ? L"PASS" : L"FAIL", distinct, n));
    }
  ```

  P4's bar is "at least three quarters distinct": with 25 cells and 30 seeds, a generator that collapsed to a handful of marks would fail it, and an honest one will not.

- [ ] **Step 7: Build and run.**

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\build-local.ps1
  powershell -Command "Start-Process -FilePath .\app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -NoNewWindow -Wait -RedirectStandardOutput .\.verify\diagnose.txt -RedirectStandardError .\.verify\diagnose.err.txt; Get-Content .\.verify\diagnose.txt -Encoding UTF8 | Select-String 'identicon|P1|P2|P3|P4'"
  ```

  Expected — 30 seeds (8 conversation keys + 22 member keys):

  ```
    identicons       : 30 seeds from the demo world
      P1  deterministic       PASS  30/30 seeds identical on a second call
      P2  mirrored            PASS  30/30 patterns symmetric
      P3  density in range    PASS  30/30 have 1..23 cells set
      P4  distinguishable     PASS  <n> distinct of 30 patterns
  ```

  If the build fails with `an identicon hue collides with a colour that carries state`, the palette regressed — change the hue, never the assert.

- [ ] **Step 8: Prove the assert can fail.** Temporarily replace the last palette entry with `urnw::colors::kProGold,` and rebuild. Expected: `error C2338: static_assert failed: 'an identicon hue collides with a colour that carries state'`. **Revert the line** and rebuild green before committing. A constraint that has never been observed to fail is a comment with extra syntax.

- [ ] **Step 9: Commit.**

  ```
  powershell -Command "(git ls-files | Measure-Object).Count"
  git status --short
  git add app/src/App/Identicon.h app/src/App/Identicon.cpp app/src/App/App.vcxproj app/src/App/Startup.cpp
  powershell -Command "(git ls-files | Measure-Object).Count"
  git commit
  ```

  Expected: **67** before, **69** after, and `git status --short` must NOT list `Identicon.cpp` as modified beyond the intended hunks (Step 8's edit reverted). Message: `identicon: deterministic 5x5 marks, with the non-state palette proved at compile time`, plus trailers.

**Deliverable:** the build enforces the palette constraint (demonstrated to fail and then pass), and `--diagnose` prints four identicon assertions over 30 real seeds. The *visual* check — 40×40, radius 8, corners clipped — belongs to the first task that puts one on screen (`ConversationListView`), which must screenshot it.

---

## Task F6: Demo/AdvancedMode.h/.cpp — the advanced_mode preference and its session-only override

**Files:**

Create: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/AdvancedMode.h
Create: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Demo/AdvancedMode.cpp
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.vcxproj
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/main.cpp
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/Startup.cpp

**Interfaces:**

- Consumes: F4 (`urmsg::demo::ParseDemoOptions().advanced`), F3 (the `CollectDiagnostics()` append point).
- Produces: `Demo/AdvancedMode.h`, namespace `urmsg`: `bool AdvancedModeEnabled();`, `void SetAdvancedModeEnabled(bool on);` (persists via `urnw::SaveAppPref("advanced_mode", on)`), `void InitAdvancedMode(bool sessionOverride);` (session-only, never persisted), `void OnAdvancedModeChanged(std::function<void(bool)> fn);` (called on the UI thread, synchronously, in registration order). `main.cpp` calls `urmsg::InitAdvancedMode(...)` once, right after `StartupLogInit()`. `CollectDiagnostics()` gains one `advanced mode` line. Preference key: `advanced_mode`, boolean, in `<StorageRoot>/app_prefs.json`.

# F6 — `Demo/AdvancedMode.h/.cpp`

Measured: `git grep advanced_mode` returns **one** hit in this repo, a provenance comment at `AppPrefs.h:5` describing the *VPN client's* `SdkHost.cpp`. The key does not exist here. This task introduces it, keeping the name identical to the VPN client's so the two products read one preference vocabulary.

One owner, per contract §5: `MainWindow` registers a single subscriber; **no view reads the preference directly.**

- [ ] **Step 1: Create `app/src/App/Demo/AdvancedMode.h`.**

  ```cpp
  // Advanced Mode: ONE owner for a state five surfaces change density on.
  //
  // Contract v2 section 5. Before this, every surface would have read the
  // preference itself, which is five readers of one truth and five chances to
  // disagree about it. MainWindow registers one subscriber and calls each
  // view's Set*Advanced; no view touches the preference.
  //
  // The preference key is advanced_mode, and it DOES NOT EXIST in this repo
  // yet - git grep returns one hit, a provenance comment in AppPrefs.h about
  // the VPN client. The name is kept identical to that client's on purpose.
  //
  // SPDX-License-Identifier: MPL-2.0
  #pragma once

  #include <functional>

  namespace urmsg {

  bool AdvancedModeEnabled();

  // Persists via urnw::SaveAppPref("advanced_mode", on). The ONLY write.
  void SetAdvancedModeEnabled(bool on);

  // Reads the stored preference and applies --demo-advanced over the top.
  // The override is SESSION-ONLY and is never written: a launch switch is how
  // someone asked to SEE a state, not a statement about what they want kept.
  void InitAdvancedMode(bool sessionOverride);

  // Subscribers are called on the UI thread, synchronously, in registration
  // order.
  void OnAdvancedModeChanged(std::function<void(bool)> fn);

  }  // namespace urmsg
  ```

- [ ] **Step 2: Create `app/src/App/Demo/AdvancedMode.cpp`.**

  ```cpp
  // SPDX-License-Identifier: MPL-2.0
  #include "pch.h"

  #include "Demo/AdvancedMode.h"

  #include <vector>

  #include "AppPrefs.h"
  #include "Log.h"

  namespace urmsg {
  namespace {

  constexpr const char* kPrefKey = "advanced_mode";

  bool g_enabled = false;
  bool g_sessionOverride = false;
  bool g_stored = false;
  std::vector<std::function<void(bool)>> g_subscribers;

  }  // namespace

  bool AdvancedModeEnabled() { return g_enabled; }

  void InitAdvancedMode(bool sessionOverride) {
    g_sessionOverride = sessionOverride;
    const nlohmann::json prefs = urnw::LoadAppPrefs();
    // is_boolean() and not just contains(): a hand-edited or half-written
    // prefs file must not decide the UI density by throwing.
    g_stored = prefs.contains(kPrefKey) && prefs[kPrefKey].is_boolean() &&
               prefs[kPrefKey].get<bool>();
    g_enabled = g_stored || sessionOverride;
    // NOTHING IS WRITTEN HERE. That is the whole point of the override.
    urnw::LogInfo("advanced: init -> {} (stored {}, session override {})",
                  g_enabled, g_stored, sessionOverride);
  }

  void SetAdvancedModeEnabled(bool on) {
    if (g_enabled == on) return;   // no change, so no write and no fan-out
    g_enabled = on;
    // A person who toggles this in Settings during a --demo-advanced run means
    // it, so this writes even then: the override is a LAUNCH state, not a lock.
    urnw::SaveAppPref(kPrefKey, on);
    g_stored = on;
    urnw::LogInfo("advanced: set -> {} (persisted; {} subscriber(s))", on,
                  g_subscribers.size());
    for (auto const& fn : g_subscribers)
      if (fn) fn(on);
  }

  void OnAdvancedModeChanged(std::function<void(bool)> fn) {
    if (fn) g_subscribers.push_back(std::move(fn));
  }

  }  // namespace urmsg
  ```

- [ ] **Step 3: Register in `App.vcxproj`.**

  ```xml
  <ClCompile Include="Demo\AdvancedMode.cpp" />
  ```
  ```xml
  <ClInclude Include="Demo\AdvancedMode.h" />
  ```

- [ ] **Step 4: Initialise it once, in `wWinMain`.** In `app/src/App/main.cpp`, add the two includes beside `"Startup.h"`:

  ```cpp
  #include "Demo/AdvancedMode.h"
  #include "Demo/DemoSwitches.h"
  ```

  and inside the first `try` block, between `urnw::StartupLogInit();` and `diagnostics = urnw::CollectDiagnostics();`:

  ```cpp
      // Here, and not in OnLaunched: --diagnose returns before OnLaunched ever
      // runs, so initialising there would leave the diagnostic reporting a
      // state the app never entered. It touches only the filesystem, so it is
      // safe before init_apartment.
      urmsg::InitAdvancedMode(urmsg::demo::ParseDemoOptions().advanced);
  ```

- [ ] **Step 5: Report it in `--diagnose`.** In `Startup.cpp`, add `#include "Demo/AdvancedMode.h"`, and insert immediately after the `demo switches` block from F4:

  ```cpp
    {
      const nlohmann::json prefs = urnw::LoadAppPrefs();
      const bool present = prefs.contains("advanced_mode") && prefs["advanced_mode"].is_boolean();
      lines.push_back(std::format(
          L"  advanced mode    : {}  (pref advanced_mode: {}; session override: {})",
          urmsg::AdvancedModeEnabled() ? L"ON " : L"off",
          present ? (prefs["advanced_mode"].get<bool>() ? L"true" : L"false") : L"absent",
          urmsg::demo::ParseDemoOptions().advanced ? L"yes" : L"no"));
    }
  ```

  Add `#include "AppPrefs.h"` to `Startup.cpp` alongside `"Paths.h"`.

- [ ] **Step 6: Build, then prove the baseline — no pref, mode off.**

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\build-local.ps1
  powershell -Command "$env:URMESSAGE_APP_ROOT=(Resolve-Path .\.localstate-verify); Remove-Item .\.localstate-verify\app_prefs.json -ErrorAction SilentlyContinue; Start-Process -FilePath .\app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -NoNewWindow -Wait -RedirectStandardOutput .\.verify\am.txt -RedirectStandardError .\.verify\am.err.txt; Get-Content .\.verify\am.txt -Encoding UTF8 | Select-String 'advanced mode'"
  ```

  Expected:

  ```
    advanced mode    : off  (pref advanced_mode: absent; session override: no)
  ```

- [ ] **Step 7: Prove `--demo-advanced` turns it on AND writes nothing.**

  ```
  powershell -Command "$env:URMESSAGE_APP_ROOT=(Resolve-Path .\.localstate-verify); Start-Process -FilePath .\app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose','--demo-advanced' -NoNewWindow -Wait -RedirectStandardOutput .\.verify\am.txt -RedirectStandardError .\.verify\am.err.txt; Get-Content .\.verify\am.txt -Encoding UTF8 | Select-String 'advanced mode'; 'prefs file exists: ' + (Test-Path .\.localstate-verify\app_prefs.json)"
  ```

  Expected — both lines:

  ```
    advanced mode    : ON   (pref advanced_mode: absent; session override: yes)
  prefs file exists: False
  ```

  `False` is the assertion. A session override that wrote a preference would leave the machine in Advanced Mode after the demo ended.

- [ ] **Step 8: Prove the stored preference is actually read.** Seed the file by hand — this is the documented prefs file (`Paths.h`), not a hack — and check both values:

  ```
  powershell -Command "$env:URMESSAGE_APP_ROOT=(Resolve-Path .\.localstate-verify); foreach ($v in @('true','false')) { Set-Content .\.localstate-verify\app_prefs.json -Value \"{`\"advanced_mode`\":$v}\" -Encoding UTF8 -NoNewline; Start-Process -FilePath .\app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -NoNewWindow -Wait -RedirectStandardOutput .\.verify\am.txt -RedirectStandardError .\.verify\am.err.txt; 'seeded ' + $v + ' -> ' + ((Get-Content .\.verify\am.txt -Encoding UTF8 | Select-String 'advanced mode').ToString().Trim()) }"
  ```

  Expected:

  ```
  seeded true -> advanced mode    : ON   (pref advanced_mode: true; session override: no)
  seeded false -> advanced mode    : off  (pref advanced_mode: false; session override: no)
  ```

- [ ] **Step 9: Prove the override does not overwrite a stored `false`.**

  ```
  powershell -Command "$env:URMESSAGE_APP_ROOT=(Resolve-Path .\.localstate-verify); Start-Process -FilePath .\app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose','--demo-advanced' -NoNewWindow -Wait -RedirectStandardOutput .\.verify\am.txt -RedirectStandardError .\.verify\am.err.txt; (Get-Content .\.verify\am.txt -Encoding UTF8 | Select-String 'advanced mode').ToString().Trim(); 'file still: ' + (Get-Content .\.localstate-verify\app_prefs.json -Raw)"
  ```

  Expected:

  ```
  advanced mode    : ON   (pref advanced_mode: false; session override: yes)
  file still: {"advanced_mode":false}
  ```

  On for this session, `false` on disk. That is the whole contract of `--demo-advanced` in two lines.

- [ ] **Step 10: Clean the scratch prefs and commit.**

  ```
  powershell -Command "Remove-Item .\.localstate-verify\app_prefs.json -ErrorAction SilentlyContinue; (git ls-files | Measure-Object).Count"
  git status --short
  git add app/src/App/Demo/AdvancedMode.h app/src/App/Demo/AdvancedMode.cpp app/src/App/App.vcxproj app/src/App/main.cpp app/src/App/Startup.cpp
  powershell -Command "(git ls-files | Measure-Object).Count"
  git commit
  ```

  Expected: **69** before, **71** after; `git status --short` lists no untracked file under `.localstate-verify/` (it is gitignored). Message: `advanced: introduce the advanced_mode preference and its session-only override`, plus trailers.

**Deliverable:** four measured states — pref absent/off, override on with no file written, seeded `true`→ON and `false`→off, and override-over-`false` leaving the file at `false`.

**Not proved here, on purpose:** `SetAdvancedModeEnabled`'s write and the subscriber fan-out have no caller yet. The Settings task is the first to call them and must screenshot the live re-render and re-read `app_prefs.json` after the toggle.

---

## Task F7: The --demo window size (1560x900) and the DEMO watermark chip

**Files:**

Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/UrComponents.h
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/WindowShell.h
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/WindowShell.cpp
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/App.xaml.cpp
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.h
Modify: C:/Users/ryanm/Downloads/claude_sandbox_message/message-windows/app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: F1 (`-AppArgs`), F4 (`urmsg::demo::ParseDemoOptions()`).
- Produces: `inline constexpr double urnw::kit::kMessageThirdPaneDip = 1500.0;` in `app/src/App/UrComponents.h`, beside `kWideBreakpointDip` and `kUltraWideDip` — the header every breakpoint this app reads already lives in, and the one `MainWindow` already includes. `inline constexpr int urnw::shell::kDemoWidthDips = 1560; kDemoHeightDips = 900;` in `WindowShell.h`. `ApplyNativeShell` gains two defaulted parameters: `bool ApplyNativeShell(Window const&, HWND, int forcedWidthDips = 0, int forcedHeightDips = 0);` — 0/0 is today's behaviour byte for byte. `MainWindow.xaml`'s title-bar StackPanel is named `TitleBarContent`; `MainWindow::BuildDemoWatermark()` appends exactly one `Border` chip to it when `enabled && watermark`.

# F7 — the demo window size and the DEMO watermark chip

**Measured, and this is the fact three earlier drafts got wrong:** the default window is **480×760 DIP**, minimum **400×480** (`WindowShell.h:21-25`). Not 1100. Not 1560. D7 changes only what `--demo` does; a normal launch is left exactly as it is.

- [ ] **Step 1: Add `kMessageThirdPaneDip`.** In `app/src/App/UrComponents.h`, immediately after `inline constexpr double kUltraWideDip = 1800.0;`:

  ```cpp
  // ---- the third-pane breakpoint (URmessage only) -----------------------------
  //
  // At or above this width the inspector rail sits BESIDE the list and the
  // thread; below it the rail collapses and message inspect is unavailable
  // (design doc 6.5a - a deliberate demo limitation, not a defect: Spec C's
  // answer below 1500 is a ContentDialog sheet, which this work does not build).
  //
  // The name carries the product because this constant is URmessage's, unlike
  // the two above it, which came verbatim from the VPN client along with the
  // rest of this file. A future diff against that repo shows one added block
  // that is obviously ours.
  //
  // 1560x900 is what --demo opens at (WindowShell.h) precisely so this
  // breakpoint is on the right side of it from the first frame - the rail has
  // to be live at launch, both for the demo and because it is the only way an
  // agent that may not synthesise input can see that state at all.
  inline constexpr double kMessageThirdPaneDip = 1500.0;
  ```

- [ ] **Step 2: Declare the demo size and the override.** In `app/src/App/WindowShell.h`, after `kMinHeightDips`:

  ```cpp
  // What --demo opens at, and ONLY --demo. 60 DIP of headroom over
  // kit::kMessageThirdPaneDip, so the rail is present before anyone touches an
  // edge; 900 tall so a thread, a composer and the status strip all fit.
  inline constexpr int kDemoWidthDips = 1560;
  inline constexpr int kDemoHeightDips = 900;
  ```

  and change the `ApplyNativeShell` declaration (currently line 51) to:

  ```cpp
  // `forcedWidthDips` / `forcedHeightDips`: a size to open at INSTEAD of the
  // compact default and instead of any saved placement. 0/0 - the default, and
  // what every existing call site passes - is today's behaviour byte for byte.
  // The saved placement is skipped when a size is forced: a demo that restored
  // yesterday's dragged-narrow window would open with the rail missing, which
  // is the one state the demo exists to show.
  bool ApplyNativeShell(winrt::Microsoft::UI::Xaml::Window const& window, HWND hwnd,
                        int forcedWidthDips = 0, int forcedHeightDips = 0);
  ```

- [ ] **Step 3: Honour it in `WindowShell.cpp`.** Change the definition at line 222 to match the new signature, then replace lines 267–269 and 275–277.

  Definition:
  ```cpp
  bool ApplyNativeShell(winrtx::Window const& window, HWND hwnd,
                        int forcedWidthDips, int forcedHeightDips) {
  ```

  Replace `const int defaultW = ...` / `const int defaultH = ...`:
  ```cpp
    const bool forced = (0 < forcedWidthDips && 0 < forcedHeightDips);
    const int defaultW =
        static_cast<int>((forced ? forcedWidthDips : kDefaultWidthDips) * scale);
    const int defaultH =
        static_cast<int>((forced ? forcedHeightDips : kDefaultHeightDips) * scale);
  ```

  Replace `auto saved = LoadPlacement();`:
  ```cpp
    // Nothing calls SaveWindowPlacement yet (no tray, no quit path), so this
    // never fires today - which is exactly why it is written now rather than
    // rediscovered the day one of them lands.
    std::optional<Placement> saved;
    if (!forced) saved = LoadPlacement();
  ```

  And in the no-saved-placement branch, change the log line so it names its source rather than always claiming "compact default":
  ```cpp
      LogInfo("shell: no saved placement - {} {}x{} centred at ({},{}) (dpi scale {:.2f})",
              forced ? "forced demo size" : "compact default", p.width, p.height, p.x, p.y, scale);
  ```

- [ ] **Step 4: Pass it from `App::OnLaunched`.** In `app/src/App/App.xaml.cpp`, add `#include "Demo/DemoSwitches.h"`, then replace the `ApplyNativeShell` call:

  ```cpp
      if (auto native = window_.try_as<::IWindowNative>()) {
        HWND hwnd = nullptr;
        native->get_WindowHandle(&hwnd);
        if (hwnd) {
          // --demo, and only --demo, opens wide enough for the third pane.
          // A normal launch is unchanged: 480x760, centred, WindowShell.h:21.
          const auto demo = urmsg::demo::ParseDemoOptions();
          const int w = demo.enabled ? urnw::shell::kDemoWidthDips : 0;
          const int h = demo.enabled ? urnw::shell::kDemoHeightDips : 0;
          urnw::shell::ApplyNativeShell(window_, hwnd, w, h);
        }
      }
  ```

- [ ] **Step 5: Name the title-bar panel.** In `app/src/App/MainWindow.xaml`, line 55, add `x:Name`:

  ```xml
              <StackPanel x:Name="TitleBarContent" Orientation="Horizontal" Margin="16,0" Spacing="8"
                          VerticalAlignment="Center" IsHitTestVisible="False">
  ```

  Nothing else in the markup changes. The chip is built in code because this file's own header says *"No user-facing text lives in this file"* — and `DEMO` is demo scaffolding that must never reach the generated `Resources.resw` (contract §0.1).

- [ ] **Step 6: Build the chip.** In `app/src/App/MainWindow.xaml.h`, add to the private section beside `BuildConversationList()`:

  ```cpp
    // The DEMO watermark chip: a small inert chip at the right of the wordmark,
    // present under --demo and suppressed by --demo-watermark=off. It exists so
    // an unpatched screenshot cannot be mistaken for a shipping product (D2).
    void BuildDemoWatermark();
  ```

  In `app/src/App/MainWindow.xaml.cpp`, add the includes:

  ```cpp
  #include "Demo/DemoSwitches.h"
  ```

  Add to the file's anonymous namespace, after `Loc`:

  ```cpp
  // A style out of the app dictionary by key, or null. Same three lines as
  // UrComponents.cpp's StyleByKey, which is file-local there; a missing key must
  // not throw a title bar away.
  Style StyleByKey(wchar_t const* key) {
    auto app = Application::Current();
    if (!app) return nullptr;
    auto boxed = winrt::box_value(winrt::hstring{key});
    if (!app.Resources().HasKey(boxed)) return nullptr;
    return app.Resources().Lookup(boxed).try_as<Style>();
  }
  ```

  and the method:

  ```cpp
  void MainWindow::BuildDemoWatermark() {
    const auto demo = urmsg::demo::ParseDemoOptions();
    if (!demo.enabled || !demo.watermark) return;

    // ZERO new tokens: the card surface, the border hairline, and
    // UrGroupHeaderTextStyle - the app's existing 11px letterspaced caption
    // voice, which is what a chip is. The parent StackPanel is
    // IsHitTestVisible=False, so the chip cannot swallow the drag region and
    // is visibly inert, which is what a watermark should be.
    Border chip;
    chip.Background(urnw::colors::CardBrush());
    chip.BorderBrush(urnw::colors::BorderBrush());
    chip.BorderThickness(ThicknessHelper::FromUniformLength(1));
    chip.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
    chip.Padding(ThicknessHelper::FromLengths(6, 1, 6, 2));
    chip.VerticalAlignment(VerticalAlignment::Center);

    TextBlock label;
    // English literal, deliberately. Strings/en/Resources.resw is GENERATED
    // from the urnetwork/localizations repo and no task may add a key to it.
    label.Text(L"DEMO");
    if (auto style = StyleByKey(L"UrGroupHeaderTextStyle")) label.Style(style);
    chip.Child(label);

    TitleBarContent().Children().Append(chip);
    urnw::LogInfo("window: DEMO watermark chip added ({} title-bar children)",
                  TitleBarContent().Children().Size());
  }
  ```

  Call it from the constructor, immediately after `ApplyStrings();`:

  ```cpp
    BuildDemoWatermark();
  ```

- [ ] **Step 7: Build, then capture the NON-demo launch — it must be untouched.**

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\build-local.ps1
  powershell -ExecutionPolicy Bypass -File app\tools\verify-render.ps1
  ```

  Expected (this box: 2560×1600 physical, 125%, work area 2048×1194 DIP):

  ```
  window rect : 600x950 at (<x>,<y>)  [PHYSICAL pixels, harness is PerMonitorV2]
  window dpi  : 120  (scale 1.25)
  dips        : 480x760
  ```

  Then **look at** `.verify/urmessage-wordmark-4x.png`: it must show the globe icon and `URmessage` in the PP NeueBit pixel face and **no chip** — the title bar has exactly **2** children. Grep the log to confirm the code never ran:

  ```
  powershell -Command "Select-String -Path .\.localstate-verify\logs\urmessage-app.log -Pattern 'DEMO watermark' | Measure-Object | Select-Object -ExpandProperty Count"
  ```

  Expected: `0`.

- [ ] **Step 8: Capture the demo launch — 1560×900 and the chip.**

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\verify-render.ps1 -AppArgs "--demo"
  ```

  Expected:

  ```
  window rect : 1950x1125 at (<x>,<y>)  [PHYSICAL pixels, harness is PerMonitorV2]
  window dpi  : 120  (scale 1.25)
  dips        : 1560x900
  ```

  `dips : 1560x900` is the assertion — 1560 DIP is 60 above `kMessageThirdPaneDip`, so the third pane is live from the first frame. (On a display whose work area is smaller than 1560×900 DIP, `ClampToWorkArea` shrinks it and the shell log line names the result; this box's work area is 2048×1194 DIP, so it does not clamp.)

  Then **look at** `.verify/urmessage-wordmark-4x.png` — the crop is the leading 260×48 DIP of the title bar at 4×, and the wordmark ends at roughly 142 DIP, so a chip 8 DIP to its right lands inside it. Expected: the globe, `URmessage`, and to its right a bordered chip reading `DEMO` in muted grey small caps. Confirm the child count:

  ```
  powershell -Command "Select-String -Path .\.localstate-verify\logs\urmessage-app.log -Pattern 'DEMO watermark chip added'"
  ```

  Expected: `window: DEMO watermark chip added (3 title-bar children)` — 3, not 2. That is the countable check.

- [ ] **Step 9: Capture the suppressed watermark.**

  ```
  powershell -ExecutionPolicy Bypass -File app\tools\verify-render.ps1 -AppArgs "--demo --demo-watermark=off"
  ```

  Expected: `dips : 1560x900` again, `.verify/urmessage-wordmark-4x.png` shows the icon and wordmark with **no chip**, and no new `DEMO watermark chip added` line is appended to the log. Wide *and* clean is the state a presenter captures from.

- [ ] **Step 10: Confirm the shell diff is only what was intended.**

  ```
  git diff --stat app/src/App/WindowShell.h app/src/App/WindowShell.cpp app/src/App/UrComponents.h
  ```

  Expected: three files, roughly `+24 -6`. `WindowShell.cpp`'s Mica removal note, the `IsVisibleOnAnyMonitor` check and `ClampToWorkArea` must show no hunks at all — `git diff` must touch only the signature, the two size lines, the `saved` initialisation and the one log string.

- [ ] **Step 11: Commit.**

  ```
  powershell -Command "(git ls-files | Measure-Object).Count"
  git status --short
  git add app/src/App/UrComponents.h app/src/App/WindowShell.h app/src/App/WindowShell.cpp app/src/App/App.xaml.cpp app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
  git commit
  ```

  Expected: **71**, and `git status --short` shows seven `M` lines and nothing else. Message:

  ```
  demo: open at 1560x900 under --demo, and mark the window DEMO

  The third pane exists only at or above 1500 DIP, so the rail has to be live
  at launch - it is the only state an agent that may not synthesise input can
  see. A normal launch is untouched at 480x760. The chip is inert, uses no new
  token, and its text is an English literal because Resources.resw is generated.

  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_012JK9oBf1Buso65V74EPXq5
  ```

**Deliverable:** three captures — no switch → `dips : 480x760`, 2 title-bar children; `--demo` → `dips : 1560x900`, 3 children with a visible `DEMO` chip in the 4× wordmark crop; `--demo --demo-watermark=off` → `dips : 1560x900`, chip gone.
