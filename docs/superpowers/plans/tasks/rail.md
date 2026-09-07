# Inspector rail

> Part of [the URmessage demo UI plan](../2026-09-06-urmessage-demo-ui.md). Read that file's **Global Constraints** first — they apply to every task here.

---

## Task R1: Inspector rail — the field model (pure C++, asserted in `--diagnose`)

**Files:**

Create app/src/App/Views/InspectRailFields.h; Create app/src/App/Views/InspectRailFields.cpp; Modify app/src/App/App.vcxproj; Modify app/src/App/Startup.cpp

**Interfaces:**

- Consumes: The foundation task that owns `app/src/App/Demo/DemoWorld.h` + `DemoWorld.cpp` (fixed contract §1): `urmsg::demo::GetWorld()`, `World`, `Conversation`, `MessageRow`, `MessageInspect`, `MemberRef`, `DeviceRef`, `RetentionClass`, `ConversationKind`, `RowKind`, `DeliveryState`. Nothing else — this task touches no XAML, no view and no window.
- Produces: namespace urmsg::views: `struct InspectField { std::wstring key; std::wstring value; };` `inline constexpr size_t kInspectValueMaxChars = 28;` `std::wstring RetentionClassLabel(demo::RetentionClass);` `std::wstring AttestationLabel(bool);` `std::wstring DeliveryLabel(demo::DeliveryState);` `std::wstring SenderLabel(demo::MessageRow const&);` `std::wstring ShortHex(std::wstring const&, size_t keep = 8);` `std::vector<InspectField> BuildMessageFields(demo::Conversation const&, demo::MessageRow const&, bool advanced);` `std::vector<InspectField> BuildConversationFields(demo::Conversation const&, bool advanced);` `demo::MessageRow const* PickInspectMessage(demo::Conversation const&);` `demo::Conversation const* FindConversation(demo::World const&, std::wstring const& id);` `demo::MessageRow const* FindMessageRow(demo::Conversation const&, std::wstring const& id);` `bool ReadByIsSubsetOfDeliveredTo(demo::MessageInspect const&);` `size_t OnlineDeviceCount(demo::MemberRef const&);` `std::wstring InspectRailFieldsProbe();` `std::wstring InspectRailDeviceProbe();`

# Task R1: Inspector rail — the field model (pure C++, asserted in `--diagnose`)

Everything the rail *says* is decided here, in functions that touch no XAML at all. That split is the whole reason the rail is testable in a repo with **no test project**: the view walks a `std::vector<InspectField>` and calls `kit::MakePaneKeyValueRow` on each entry, so the field list — its order, its count at each density, and the values' widths — is asserted in `--diagnose` before a single pixel exists.

Two functions are here rather than in the view for a second reason. `PickInspectMessage` must pre-select the SAME message in the thread (the selection outline) and in the rail (message mode); one pure function called by both is what makes that agree by construction. `FindConversation` / `FindMessageRow` exist because the rail holds **ids, never pointers** — design §9.2's ambient activity appends a `MessageRow` to the open conversation's `rows`, and a `std::vector` append invalidates every `MessageRow*` into it, including the one `--demo=inspect` is sitting on.

- [ ] **Step 1: Create `app/src/App/Views/InspectRailFields.h`.**

```cpp
// The inspector rail's CONTENT, with no XAML in it.
//
// The rail (design 6.3, D3) renders exactly what these functions return: a list
// of key/value pairs, in order, at one of two densities. Keeping that here is
// what makes the rail's content assertable in --diagnose - this repo has no
// test project - and leaves the view file below it holding nothing but layout.
//
// DEMO COPY IS LITERAL, NOT LOCALIZED, AND THAT IS A DECISION.
// Strings/en/Resources.resw is GENERATED from the separate urnetwork/
// localizations repo (Localization.h:3-4), so a demo-only label cannot be added
// to it. Design 2 scopes localization out and design 9.4 records the ruling.
// The demo surfaces are therefore the one place in this window that does not go
// through Localization.h - MainWindow.xaml's "no user-facing text lives in this
// file" rule is deliberately narrowed here, not broken by accident - and they go
// back through it the day the demo becomes product.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

struct InspectField {
  std::wstring key;
  std::wstring value;
};

// The widest a value may be before kit::MakePaneKeyValueRow stops behaving. Its
// value column is Auto and its key column is Star (UrComponents.cpp:172-176), so
// a long value takes the width it wants and the KEY is what gets squeezed - the
// row does not ellipsize, it overflows the 360 DIP column. 28 characters of Neue
// Montreal at 13 is ~196 DIP, which leaves 110 for the widest key ("Sender leaf
// index"), 8 for the grid's ColumnSpacing and the row's own 24 of padding:
// 338 of 360. Asserted in InspectRailFieldsProbe rather than hoped for.
inline constexpr size_t kInspectValueMaxChars = 28;

// The rail's retention vocabulary for a MESSAGE. Deliberately NOT the same row
// as conversation mode's: a message has a retention CLASS (a two-valued enum),
// a conversation has a retention POLICY that DemoWorld carries as a display
// string. Two questions, two keys - "Retention class" and "Retention" - so one
// 360 DIP column never shows two vocabularies under one word.
std::wstring RetentionClassLabel(demo::RetentionClass retention);
std::wstring AttestationLabel(bool verified);

// Named here rather than in the thread because the rail's --diagnose probe has
// to PRINT it (a verification step cannot check a delivery state it has no way
// to read), and because the thread's delivery glyph needs the same word for its
// accessible name - an unnamed glyph is a defect UrComponents.cpp has paid for
// twice.
std::wstring DeliveryLabel(demo::DeliveryState state);

// Who the rail names as the sender. MessageInspect::senderDisplayName is ALWAYS
// populated (DemoWorld.h), including on run continuations, so there is no
// fallback chain here and an empty one is a DemoWorld defect that the probe
// reports by key name.
std::wstring SenderLabel(demo::MessageRow const& row);

// "a1b2c3d4...9f0e": the first `keep` characters, one horizontal ellipsis, the
// last four. Returns `hex` unchanged when it already fits. Shortening happens
// HERE, where the result is a string a probe can measure, rather than in the
// view where it would depend on a trimming this row shape does not do.
std::wstring ShortHex(std::wstring const& hex, size_t keep = 8);

// Message mode. Normal is design 6.3's eight fields, in its order. Advanced adds
// design 6.6's four: the hex group id, the wire size and the two raw ids. The
// leaf index is NOT one of them - 6.3 lists it among the normal fields and 6.6's
// summary table lists it again; 6.3 is the field list and wins, so the counts
// are 8 and 12 and the probe asserts both.
std::vector<InspectField> BuildMessageFields(demo::Conversation const& conv,
                                             demo::MessageRow const& row, bool advanced);

// Conversation mode's retention block. The member list is not here: it is a list
// of person rows, not key/value pairs. Advanced adds the conversation id always,
// and the hex group id only for a GROUP - Conversation::groupIdHex is documented
// non-empty for groups, so emitting it for a direct message is a blank row.
std::vector<InspectField> BuildConversationFields(demo::Conversation const& conv,
                                                  bool advanced);

// The message --demo=inspect opens on: the last OUTGOING row whose state is
// Read, else the last message row, else nullptr.
//
// The fixed contract (2) says --demo=inspect opens "the message named by
// DemoWorld's designated inspect target", but DemoWorld.h's World and
// Conversation carry NO such field - there is nothing to read. So the
// designation is THIS function: one pure, deterministic choice the thread
// surface calls for its selection outline and the rail calls for its message
// mode, which is what makes the two agree by construction rather than by luck.
demo::MessageRow const* PickInspectMessage(demo::Conversation const& conv);

// Re-resolve a subject by id. The rail holds IDS, never pointers into
// GetWorld()'s storage: design 9.2's ambient activity APPENDS a MessageRow to
// the open conversation's `rows`, and a vector append invalidates every
// MessageRow* into it. The pointers these return are for IMMEDIATE use inside
// one render call and must not be stored across one.
demo::Conversation const* FindConversation(demo::World const& world, std::wstring const& id);
demo::MessageRow const* FindMessageRow(demo::Conversation const& conv, std::wstring const& id);

// Delivered is a statement by a DEVICE, so a device that read a message must
// have received it first. Compared by DeviceRef::id, which DemoWorld.h documents
// as stable ("dev-pixel9"); comparing by name would pass on two different
// devices that happen to share one. If this is ever false the rail is rendering
// an impossible claim, so it is asserted rather than assumed.
bool ReadByIsSubsetOfDeliveredTo(demo::MessageInspect const& inspect);

// How many of a person's devices are online. The member row's right-hand meta
// says "2/3 online" in WORDS, which is the only condition under which
// BuildPaneListRowParts is entitled to mark that row's presence dot Raw
// (UrComponents.cpp:246-248), and the only way the row obeys "colour is never
// the only carrier of state".
size_t OnlineDeviceCount(demo::MemberRef const& member);

// The two --diagnose lines, already in CollectDiagnostics()'s column format
// (two-space indent, 17-character label, " : "). They live here so Startup.cpp's
// edit is one guard and two push_backs - that file is a collision point for
// every surface task.
std::wstring InspectRailFieldsProbe();
std::wstring InspectRailDeviceProbe();

}  // namespace urmsg::views
```

- [ ] **Step 2: Create `app/src/App/Views/InspectRailFields.cpp` with every function UNIMPLEMENTED.** This is the failing half of the cycle: the probes must compile and print `FAIL` before any of this works.

```cpp
// SPDX-License-Identifier: MPL-2.0
// The project compiles with /Yu"pch.h" (App.vcxproj:110-111), so pch.h is first
// in every translation unit. The HEADER above stays pure C++ - it is included
// by Startup.cpp's CollectDiagnostics(), which runs before init_apartment().
#include "pch.h"

#include "Views/InspectRailFields.h"

#include <format>

namespace urmsg::views {

std::wstring RetentionClassLabel(demo::RetentionClass) { return {}; }
std::wstring AttestationLabel(bool) { return {}; }
std::wstring DeliveryLabel(demo::DeliveryState) { return {}; }
std::wstring SenderLabel(demo::MessageRow const&) { return {}; }
std::wstring ShortHex(std::wstring const&, size_t) { return {}; }
size_t OnlineDeviceCount(demo::MemberRef const&) { return 0; }

std::vector<InspectField> BuildMessageFields(demo::Conversation const&,
                                             demo::MessageRow const&, bool) {
  return {};
}

std::vector<InspectField> BuildConversationFields(demo::Conversation const&, bool) { return {}; }

demo::MessageRow const* PickInspectMessage(demo::Conversation const&) { return nullptr; }
demo::Conversation const* FindConversation(demo::World const&, std::wstring const&) {
  return nullptr;
}
demo::MessageRow const* FindMessageRow(demo::Conversation const&, std::wstring const&) {
  return nullptr;
}
bool ReadByIsSubsetOfDeliveredTo(demo::MessageInspect const&) { return false; }

}  // namespace urmsg::views
```

- [ ] **Step 3: Add both files to `app/src/App/App.vcxproj`, and give the build an object-file path that mirrors the source tree.** Insert the `ClCompile` line immediately after `<ClCompile Include="UrMotion.cpp" />` (line 183) and the `ClInclude` line immediately after `<ClInclude Include="UrMotion.h" />` (line 195):

```xml
    <ClCompile Include="Views\InspectRailFields.cpp" />
```
```xml
    <ClInclude Include="Views\InspectRailFields.h" />
```

Then add one element to the `<ClCompile>` block inside the `<ItemDefinitionGroup>`, immediately after the `<AdditionalIncludeDirectories>` line (line 125):

```xml
      <!-- Views\ is this project's first source SUBDIRECTORY. App.vcxproj sets
           no ObjectFileName, so every .obj otherwise lands flat in $(IntDir):
           the first Views\X.cpp that shares a basename with a root-level X.cpp
           gets MSB8027, or worse, a silently stale object. Mirror the source
           tree instead, once, for every surface task that follows. -->
      <ObjectFileName>$(IntDir)%(RelativeDir)</ObjectFileName>
```

**STOP rule:** if an `<ObjectFileName>` element is already present because another surface task landed first, leave it alone — do not add a second one.

`AdditionalIncludeDirectories` already carries `$(MSBuildProjectDirectory)` (line 125), which is `app/src/App`, so `#include "pch.h"` and `#include "Demo/DemoWorld.h"` both resolve from `Views/`. No other project change is needed.

- [ ] **Step 4: Write the two probes at the foot of `InspectRailFields.cpp`,** immediately above the closing `}  // namespace urmsg::views`.

ASCII only, deliberately: `--diagnose` writes UTF-8 bytes through a redirected handle (`Startup.cpp:155-157`) and Windows PowerShell 5.1's `Get-Content` decodes ANSI by default, so a non-ASCII character comes back as mojibake in the one place this task is read.

```cpp
std::wstring InspectRailFieldsProbe() {
  auto const& world = demo::GetWorld();
  if (world.conversations.empty())
    return L"  inspect rail     : FAIL - the demo world has no conversations";
  auto const& conv = world.conversations.front();
  demo::MessageRow const* row = PickInspectMessage(conv);
  if (row == nullptr)
    return L"  inspect rail     : FAIL - conversation 0 has no message row";

  const auto normal = BuildMessageFields(conv, *row, false);
  const auto advanced = BuildMessageFields(conv, *row, true);

  // PROPERTIES, not spot checks. "normal[0].value is non-empty" cannot fail by
  // construction and would print PASS over a column of blanks; these two can
  // fail, and both name the key that tripped them so the report is actionable.
  // A blank value is a 34 DIP row of nothing; an over-long one overflows the
  // column (see kInspectValueMaxChars).
  size_t blanks = 0;
  size_t widest = 0;
  std::wstring widestKey = L"(none)";
  for (auto const& f : advanced) {
    if (f.value.empty()) ++blanks;
    if (widest < f.value.size()) {
      widest = f.value.size();
      widestKey = f.key;
    }
  }

  // Conversation mode is a RULE over all the conversations, not one magic
  // number: 2 rows at normal density, +1 for the conversation id, +1 more for a
  // group's hex group id.
  size_t convOk = 0;
  for (auto const& c : world.conversations) {
    const size_t expected = (c.kind == demo::ConversationKind::Group) ? 4u : 3u;
    if (BuildConversationFields(c, false).size() == 2 &&
        BuildConversationFields(c, true).size() == expected)
      ++convOk;
  }

  const bool ok = normal.size() == 8 && advanced.size() == 12 &&
                  normal[0].key == L"Sender" && advanced[8].key == L"Group id" &&
                  blanks == 0 && widest <= kInspectValueMaxChars &&
                  convOk == world.conversations.size();

  return std::format(
      L"  inspect rail     : {} - message fields {}/8 normal {}/12 advanced, "
      L"conversation fields correct in {}/{}, blank values {}, widest value {} "
      L"(\"{}\") of {} allowed, sender \"{}\"",
      ok ? L"PASS" : L"FAIL", normal.size(), advanced.size(), convOk,
      world.conversations.size(), blanks, widest, widestKey, kInspectValueMaxChars,
      normal.empty() ? std::wstring{L"(none)"} : normal[0].value);
}

std::wstring InspectRailDeviceProbe() {
  auto const& world = demo::GetWorld();
  if (world.conversations.empty())
    return L"  inspect devices  : FAIL - the demo world has no conversations";

  size_t messages = 0;
  size_t withReaders = 0;
  size_t consistent = 0;
  for (auto const& conv : world.conversations) {
    for (auto const& row : conv.rows) {
      if (row.kind != demo::RowKind::Message) continue;
      ++messages;
      // A row with no readers satisfies the subset property VACUOUSLY, so it is
      // not evidence and is not counted. withReaders is the denominator that
      // makes the ratio mean something: without it a world with zero read
      // receipts anywhere reports a perfect score.
      if (row.inspect.readBy.empty()) continue;
      ++withReaders;
      if (ReadByIsSubsetOfDeliveredTo(row.inspect)) ++consistent;
    }
  }

  auto const& conv = world.conversations.front();
  demo::MessageRow const* pick = PickInspectMessage(conv);
  const size_t delivered = pick ? pick->inspect.deliveredTo.size() : 0;
  const size_t read = pick ? pick->inspect.readBy.size() : 0;

  // The pick's own receipt counts are printed as FACTS, not gated on: the
  // fallback pick is legitimately allowed to be an incoming row with none.
  const bool ok = pick != nullptr && 0 < withReaders && consistent == withReaders;

  return std::format(
      L"  inspect devices  : {} - pick \"{}\" state {} delivered-by {} read-by {}; "
      L"read-by is a subset of delivered-by in {}/{} rows that have readers, of "
      L"{} message rows",
      ok ? L"PASS" : L"FAIL", pick ? pick->id : std::wstring{L"(none)"},
      pick ? DeliveryLabel(pick->state) : std::wstring{L"-"}, delivered, read, consistent,
      withReaders, messages);
}
```

- [ ] **Step 5: Wire the probes into `CollectDiagnostics()`, BEHIND A GUARD.** In `app/src/App/Startup.cpp`, add the include immediately after `#include "Strings.h"` (line 19):

```cpp
#include "Views/InspectRailFields.h"
```

and insert this immediately before `CollectDiagnostics()`'s `return lines;` (i.e. after the `fonts` push_back):

```cpp
  // GUARDED, and the guard is not a nicety. CollectDiagnostics() is NOT the
  // --diagnose path: it runs on EVERY launch, before winrt::init_apartment and
  // before WantsDiagnose(), inside the try/catch in wWinMain whose failure path
  // shows "URmessage could not start" and returns 1 with NO WINDOW AT ALL
  // (main.cpp:161-177). These two lines build the whole demo world. Behind the
  // guard, a defect in DemoWorld costs the --diagnose run a line; in front of
  // it, the same defect costs every launch its window.
  if (WantsDiagnose()) {
    lines.push_back(urmsg::views::InspectRailFieldsProbe());
    lines.push_back(urmsg::views::InspectRailDeviceProbe());
  }
```

(`WantsDiagnose()` is declared in `Startup.h`, which this file includes at line 6, and defined lower in the same translation unit — no forward declaration is needed.)

- [ ] **Step 6: Build, and SEE the FAIL lines.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -Command "New-Item -ItemType Directory -Force .verify | Out-Null; Start-Process -FilePath app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -RedirectStandardOutput .verify\diagnose.txt -NoNewWindow -Wait; Get-Content .verify\diagnose.txt -Encoding UTF8"
```

The build prints `OK in <n>s -> ...\app\build\x64\Release\` (build-local.ps1:102). The diagnostics then end with exactly two lines of this SHAPE — the numbers come from the DemoWorld task's world, not from this task, so read the shape and not the digits:

```
  inspect rail     : FAIL - conversation 0 has no message row
  inspect devices  : FAIL - pick "(none)" state - delivered-by 0 read-by 0; read-by is a subset of delivered-by in 0/W rows that have readers, of N message rows
```

`N` is the number of message rows in the whole world and `W` the number of them carrying a read receipt. Any `N > 0` and any `W > 0` is correct here — everything is stubbed, so `0/W` is the expected failure.

**STOP rules.** If the line ends `of 0 message rows`, the DemoWorld task's world has no message rows and this task is blocked on it. If it reads `0/0 rows that have readers`, no message in the world carries a read receipt — contract invariant 5 requires at least one row in state `Read`, and a `Read` row with an empty `readBy` is a `READ BY` list the rail would render as an empty state. Report either against the DemoWorld task; do not implement around it and do not weaken the probe.

- [ ] **Step 7: Implement the five label helpers.** Replace those five stubs:

```cpp
std::wstring RetentionClassLabel(demo::RetentionClass retention) {
  return retention == demo::RetentionClass::Eph ? L"Disappearing" : L"Permanent";
}

std::wstring AttestationLabel(bool verified) { return verified ? L"Verified" : L"Not verified"; }

std::wstring DeliveryLabel(demo::DeliveryState state) {
  switch (state) {
    case demo::DeliveryState::Pending:   return L"Pending";
    case demo::DeliveryState::Sent:      return L"Sent";
    case demo::DeliveryState::Delivered: return L"Delivered";
    case demo::DeliveryState::Read:      return L"Read";
    case demo::DeliveryState::Failed:    return L"Failed";
    case demo::DeliveryState::Expired:   return L"Expired";
  }
  return L"Unknown";
}

std::wstring SenderLabel(demo::MessageRow const& row) {
  // No fallback chain: MessageInspect::senderDisplayName is documented ALWAYS
  // populated, run continuations included, so an empty one is a DemoWorld defect
  // and InspectRailFieldsProbe reports it as a blank "Sender" value.
  if (row.outgoing) return L"You";
  return row.inspect.senderDisplayName;
}

std::wstring ShortHex(std::wstring const& hex, size_t keep) {
  if (hex.size() <= keep + 8) return hex;
  return hex.substr(0, keep) + L"…" + hex.substr(hex.size() - 4);  // U+2026 ellipsis
}

size_t OnlineDeviceCount(demo::MemberRef const& member) {
  size_t online = 0;
  for (auto const& device : member.devices)
    if (device.online) ++online;
  return online;
}
```

- [ ] **Step 8: Implement `BuildMessageFields`.** The first eight entries are design §6.3's list in its order and must not be reordered — the screenshot checks in R3 and R4 read them by position.

```cpp
std::vector<InspectField> BuildMessageFields(demo::Conversation const& conv,
                                             demo::MessageRow const& row, bool advanced) {
  std::vector<InspectField> out;
  out.push_back({L"Sender", SenderLabel(row)});
  out.push_back({L"Sent", row.inspect.sentAtLabel});
  out.push_back({L"Received", row.inspect.receivedAtLabel});
  out.push_back({L"Epoch", std::format(L"{}", row.inspect.epoch)});
  out.push_back({L"Sender leaf index", std::format(L"{}", row.inspect.senderLeafIndex)});
  out.push_back({L"Retention class", RetentionClassLabel(row.inspect.retention)});
  out.push_back({L"Size", row.inspect.sizeBucket});
  out.push_back({L"Attestation", AttestationLabel(row.inspect.attestationVerified)});
  if (!advanced) return out;
  // Design 6.6's four Advanced additions for this surface - "raw ids, hex group
  // id, leaf index, wire size" - minus the leaf index, which 6.3 already put in
  // the normal list above. Emitted UNCONDITIONALLY: every message belongs to a
  // group (a direct message is a two-party group), so an empty groupIdHex here
  // is a DemoWorld defect the probe names rather than something the rail hides.
  out.push_back({L"Group id", ShortHex(row.inspect.groupIdHex)});
  out.push_back({L"Wire size", std::format(L"{} bytes", row.inspect.wireSizeBytes)});
  out.push_back({L"Message id", row.id});
  out.push_back({L"Conversation id", conv.id});
  return out;
}
```

- [ ] **Step 9: Implement `BuildConversationFields`.**

```cpp
std::vector<InspectField> BuildConversationFields(demo::Conversation const& conv,
                                                  bool advanced) {
  std::vector<InspectField> out;
  out.push_back({L"Retention", conv.retentionLabel});
  out.push_back({L"Media", conv.mediaRetentionLabel});
  if (!advanced) return out;
  out.push_back({L"Conversation id", conv.id});
  // GROUPS ONLY, and this is the one place the two modes differ on purpose:
  // Conversation::groupIdHex is documented non-empty for a group, which means
  // empty for a direct message, so a DM would get a 34 DIP row of nothing.
  // MessageInspect::groupIdHex carries no such exemption and is emitted always.
  if (conv.kind == demo::ConversationKind::Group)
    out.push_back({L"Group id", ShortHex(conv.groupIdHex)});
  return out;
}
```

- [ ] **Step 10: Implement the four lookup / property functions.**

```cpp
demo::MessageRow const* PickInspectMessage(demo::Conversation const& conv) {
  demo::MessageRow const* lastMessage = nullptr;
  demo::MessageRow const* lastRead = nullptr;
  for (auto const& row : conv.rows) {
    if (row.kind != demo::RowKind::Message) continue;
    lastMessage = &row;
    if (row.outgoing && row.state == demo::DeliveryState::Read) lastRead = &row;
  }
  return lastRead ? lastRead : lastMessage;
}

demo::Conversation const* FindConversation(demo::World const& world, std::wstring const& id) {
  for (auto const& conv : world.conversations)
    if (conv.id == id) return &conv;
  return nullptr;
}

demo::MessageRow const* FindMessageRow(demo::Conversation const& conv, std::wstring const& id) {
  for (auto const& row : conv.rows)
    if (row.id == id) return &row;
  return nullptr;
}

bool ReadByIsSubsetOfDeliveredTo(demo::MessageInspect const& inspect) {
  for (auto const& reader : inspect.readBy) {
    bool found = false;
    for (auto const& target : inspect.deliveredTo) {
      // By id, not by (name, owner): DeviceRef::id is documented stable, and two
      // devices that happen to share a name would otherwise pass a check whose
      // whole point is device identity.
      if (target.id == reader.id) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}
```

- [ ] **Step 11: Build and run `--diagnose` again; see PASS.** Same two commands as Step 6.

Both lines must begin `PASS`. Four figures are THIS task's own contract and must read exactly: `8/8 normal`, `12/12 advanced`, `conversation fields correct in N/N` where N is the number of conversations (the contract requires 8), and `blank values 0`. The rest is shape:

```
  inspect rail     : PASS - message fields 8/8 normal 12/12 advanced, conversation fields correct in 8/8, blank values 0, widest value <w> ("<key>") of 28 allowed, sender "<a name, or You>"
  inspect devices  : PASS - pick "<id>" state <one of Pending|Sent|Delivered|Read|Failed> delivered-by <D> read-by <R>; read-by is a subset of delivered-by in W/W rows that have readers, of N message rows
```

**Write down `<id>`, the state word, `<D>` and `<R>` now — R3 and R4 check the captured rail against them.**

If `blank values` is non-zero the line names the offending key: that is a DemoWorld field that came back empty, not a rail defect. If `widest value` exceeds 28 the line names that key too: the value overflows the 360 DIP column and either DemoWorld must shorten it or it needs a `ShortHex` at its push_back. If `inspect devices` reports `n/W` for `n < W`, some message's `readBy` names a device id that is not in its `deliveredTo` — report it against the DemoWorld task rather than weakening the check.

- [ ] **Step 12: Commit.**

```
powershell -ExecutionPolicy Bypass -Command "git ls-files | Measure-Object -Line"
git add app/src/App/Views/InspectRailFields.h app/src/App/Views/InspectRailFields.cpp app/src/App/App.vcxproj app/src/App/Startup.cpp
git commit -m "demo: the inspector rail's field model, asserted in --diagnose"
powershell -ExecutionPolicy Bypass -Command "git ls-files | Measure-Object -Line"
```

The tracked-file count must rise by exactly 2 (63 -> 65 from today's tree). A count that fell means the index was truncated — re-add and re-commit before going on.

**Deliverable, independently checkable:** `URmessage.exe --diagnose` prints two `PASS` lines carrying the field counts (8/8, 12/12, N/N conversations), zero blank values, the widest value and its key against a stated 28-character budget, the id and delivery STATE of the message `--demo=inspect` will open on, and the read-by ⊆ delivered-by property with its own denominator beside it. No UI has changed.

---

## Task R2: Inspector rail — the 360 DIP column and conversation mode

**Files:**

Create app/src/App/Views/InspectRailView.h; Create app/src/App/Views/InspectRailView.cpp; Modify app/src/App/App.vcxproj; Modify app/src/App/MainWindow.xaml; Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: R1 (`Views/InspectRailFields.h`: `BuildConversationFields`, `OnlineDeviceCount`). The foundation task that owns `app/src/App/Demo/DemoWorld.h` (contract §1: `GetWorld()`, `Conversation`, `MemberRef`, `DeviceRef`). The foundation task that owns `app/src/App/Demo/DemoSwitches.h` (contract §2: `DemoOptions`, `DemoScreen`, `ParseDemoOptions()`). The foundation task that owns `app/src/App/Identicon.h` (contract §3: `urmsg::MakeIdenticon`). **Task F1**, which adds the `-AppArgs` parameter to `app/tools/verify-render.ps1` — every capture step here is a terminating error without it. The task that owns the demo launch size (D7, 1560×900): the rail is invisible below 1500 DIP by design, so a 480×760 launch renders no rail at all.
- Produces: namespace urmsg::views, in `app/src/App/Views/InspectRailView.h`: `inline constexpr double kInspectRailWidthDip = 360.0;` `inline constexpr double kInspectRailBreakpointDip = 1500.0;` `struct InspectRailView { winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr}; winrt::Microsoft::UI::Xaml::Controls::ScrollViewer conversationScroll{nullptr}; winrt::Microsoft::UI::Xaml::Controls::ScrollViewer messageScroll{nullptr}; };` `InspectRailView MakeInspectRail();` `void SetInspectRailConversation(InspectRailView&, demo::Conversation const&);` `void SetInspectRailMessage(InspectRailView&, demo::Conversation const&, demo::MessageRow const&);` (declared here, defined in R3) `void SetInspectRailAdvanced(InspectRailView&, bool);` (declared here, defined in R4) `urnw::kit::PaneListRow MakeMemberRow(demo::MemberRef const&);` `urnw::kit::PaneListRow MakeDeviceRow(demo::DeviceRef const&);` — In `MainWindow`: named XAML elements `RailRuleColumn`, `RailColumn`, `RailRule`, `InspectRailHost`; members `urmsg::demo::DemoOptions demo_{}`, `urmsg::views::InspectRailView rail_{}`; private methods `void BuildInspectRail();` `void ApplyRailBreakpoint(double width);`

# Task R2: Inspector rail — the 360 DIP column and conversation mode

This builds the rail and its default mode. Three things here are load-bearing.

- **The width is fixed at 360 and the column is the app's third pane.** That is why the mode swap in R3 is a crossfade and not a slide: a slide inside a fixed-width column implies a navigation that has not happened (design §6.3).
- **Below `kInspectRailBreakpointDip` (1500) the rail is gone**, and message inspect is simply unavailable there (design §6.5a). No sheet, no fallback, no error. The demo opens at 1560, so this is a resize behaviour rather than the normal state.
- **The rail holds ids, not pointers, and both bodies start collapsed.** Design §9.2's ambient activity appends to `Conversation::rows`; a cached `MessageRow*` would dangle the moment it lands. And a first population with nothing to swap from must start no storyboard at all — `RunCrossfade` (UrMotion.cpp:86-95) sets `incoming.Opacity(0.0)` before it begins, so a crossfade with an already-collapsed outgoing blanks the visible body and fades it back for no reason.

The rail is populated only when the demo world is on. A non-demo launch at 1600 DIP must not grow an empty third pane.

- [ ] **Step 1: Create `app/src/App/Views/InspectRailView.h`, complete.** All four contract functions are declared here in their finished form; R3 and R4 add only bodies to the `.cpp`, so no cross-task "next task" wording is ever committed into a shipped header.

```cpp
// The inspector rail (design 6.3, D3): one 360 DIP right column with two modes.
//
// Conversation mode is the default; message mode replaces the BODY when a bubble
// is selected, by crossfade at kBaseMs. The pane header does not change with the
// mode - a pane header names the COLUMN ("Conversations", "Thread"), not its
// content - so a mode swap is one animation and not two.
//
// THE RAIL HOLDS IDS, NOT POINTERS. Design 9.2's ambient activity appends a
// MessageRow to the open conversation's `rows`, and a std::vector append
// invalidates every MessageRow* into it - including the one --demo=inspect is
// sitting on. The current subject and density are parked on the elements' own
// Tag and re-resolved through views::FindConversation / views::FindMessageRow
// at render time.
//
// Demo copy in this module is LITERAL, not localized, for the reason recorded
// at the top of InspectRailFields.h.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Demo/DemoWorld.h"
#include "UrComponents.h"

namespace urmsg::views {

// The third pane: 360 wide, and only at or above 1500 DIP. 1500 is introduced by
// this work (design 6.5a). It is a DIFFERENT threshold from the app's existing
// urnw::kit::kWideBreakpointDip (1000, UrComponents.h:75) and must not be folded
// into it: the two early-outs would mask each other.
inline constexpr double kInspectRailWidthDip = 360.0;
inline constexpr double kInspectRailBreakpointDip = 1500.0;

struct InspectRailView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer conversationScroll{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer messageScroll{nullptr};
};

// Both bodies start COLLAPSED, so whichever Set* runs first presents its mode
// with no outgoing element and therefore no storyboard. The window's own
// WindowReveal already covers the entrance; a rail that also faded itself in
// would be a second animation over the same tree.
InspectRailView MakeInspectRail();

// Conversation mode: the subject row, the member list, the retention block.
// Called on a conversation-list selection and on a click into empty thread space
// (design 9.1). The density is the rail's own current density - see
// SetInspectRailAdvanced, which is the one function that changes it.
void SetInspectRailConversation(InspectRailView& v, demo::Conversation const& c);

// Message mode: the lock header, the failure block when the row failed, design
// 6.3's fields, and the delivered-by / read-by device lists. Called on a bubble
// click (design 9.1) and by --demo=inspect.
void SetInspectRailMessage(InspectRailView& v, demo::Conversation const& c,
                           demo::MessageRow const& m);

// DENSITY ONLY. Re-populates whichever mode is showing, IN PLACE, with NO
// crossfade: the rail is already on screen, and blanking it to fade it back in
// reads as a mode swap that did not happen. Design 7 assigns kBaseMs to the rail
// MODE swap, not to a density change.
void SetInspectRailAdvanced(InspectRailView& v, bool advanced);

// One person row and one device row. Public because the Network page's "Your
// devices" list (design 6.4) is the same device row and must not become a second
// species of it. Both are urnw::kit::MakePaneListRow(36), so the rail's three
// lists share one height, one left edge and one rhythm.
//
// Neither carries an identicon: MakePaneListRow has a dot, a title and a meta
// and no icon slot, and adding one would be a second row species in a pane
// layout whose whole claim is that it has exactly one. MemberRef::identityKey
// and DeviceRef::ownerKey do exist, so if member identicons are wanted the right
// move is a MakePaneListRow variant in UrComponents.h, not a bespoke row here.
// The rail's one identicon is the conversation's, on the subject row.
urnw::kit::PaneListRow MakeMemberRow(demo::MemberRef const& member);
urnw::kit::PaneListRow MakeDeviceRow(demo::DeviceRef const& device);

}  // namespace urmsg::views
```

- [ ] **Step 2: Create `app/src/App/Views/InspectRailView.cpp` with the file header, the includes and the two small adapters.**

```cpp
// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Views/InspectRailView.h"

#include <format>
#include <string>
#include <vector>

// BOTH automation headers, in this order, exactly as UrComponents.cpp:9-10 does
// it. AccessibilityView lives in the Peers projection; whether it reaches this
// file transitively through the other one is a cppwinrt layout detail and not
// something to bet a build on.
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>

#include "Identicon.h"
#include "Log.h"
#include "UrColors.h"
#include "UrMotion.h"
#include "Views/InspectRailFields.h"

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace kit = urnw::kit;
namespace automation = winrt::Microsoft::UI::Xaml::Automation;

namespace urmsg::views {
namespace {

// std::wstring -> hstring is EXPLICIT, and the kit builders take
// `winrt::hstring const&`, so every std::wstring crossing into the kit goes
// through one named adapter rather than through forty hstring{...} at call
// sites. This is the same adapter MainWindow.xaml.cpp names Loc().
winrt::hstring H(std::wstring const& value) { return winrt::hstring{value}; }

// A style out of the app dictionary by key, or null if it is missing: applying
// styles by key is what keeps a pane built in code in step with App.xaml, and a
// missing key must not throw a layout away. UrComponents.cpp:78-86 has the same
// six lines in its own anonymous namespace and does not export them. A THIRD
// copy is the signal to promote it into UrComponents.h; this is the second.
Style StyleByKey(wchar_t const* key) {
  auto app = Application::Current();
  if (!app) return nullptr;
  auto boxed = winrt::box_value(winrt::hstring{key});
  if (!app.Resources().HasKey(boxed)) return nullptr;
  return app.Resources().Lookup(boxed).try_as<Style>();
}

}  // namespace
}  // namespace urmsg::views
```

- [ ] **Step 3: Add the rail's own state helpers** to that anonymous namespace, below `StyleByKey`.

```cpp
// ---- the rail's state, parked on its own elements --------------------------
//
// FrameworkElement::Tag is an IInspectable slot the platform never touches, so
// it carries the rail's subject and density without a file-static that a second
// rail would silently share. IDS, never pointers - see the header.

void SetRailAdvanced(InspectRailView const& v, bool advanced) {
  if (v.root) v.root.Tag(winrt::box_value(advanced));
}

bool RailAdvanced(InspectRailView const& v) {
  return v.root && winrt::unbox_value_or<bool>(v.root.Tag(), false);
}

void SetRailSubject(InspectRailView const& v, std::wstring const& conversationId,
                    std::wstring const& messageId) {
  if (v.conversationScroll)
    v.conversationScroll.Tag(winrt::box_value(winrt::hstring{conversationId}));
  if (v.messageScroll) v.messageScroll.Tag(winrt::box_value(winrt::hstring{messageId}));
}

std::wstring TagId(FrameworkElement const& element) {
  if (!element) return {};
  auto tag = element.Tag().try_as<winrt::hstring>();
  return tag ? std::wstring{*tag} : std::wstring{};
}

std::wstring RailConversationId(InspectRailView const& v) { return TagId(v.conversationScroll); }
std::wstring RailMessageId(InspectRailView const& v) { return TagId(v.messageScroll); }

// The StackPanel inside a body scroller. The fixed contract gives InspectRailView
// three fields and no body fields, so the panel is fetched rather than cached -
// which is also one fewer thing that can go stale.
StackPanel BodyOf(ScrollViewer const& scroller) {
  return scroller ? scroller.Content().try_as<StackPanel>() : nullptr;
}

// A crossfade ONLY when the mode actually changes.
//
// CrossfadePageSwap(outgoing, incoming) sets incoming.Opacity(0) and fades it
// back over kBaseMs (UrMotion.cpp:86-95). That is right for a real swap and
// wrong for anything else: called with an already-collapsed outgoing it blanks
// the VISIBLE body and re-fades it, and called twice in one synchronous block it
// drives two storyboards over the same two elements in opposite directions and
// the end state is a race. So the outgoing is passed only when it is actually on
// screen; otherwise `incoming` is passed as its own outgoing, which
// UrMotion.cpp:123-127 documents as the instant, no-storyboard path.
//
// A crossfade, never a slide: the column's width is fixed, and a slide inside a
// fixed-width column implies a navigation that has not happened (design 6.3).
// CrossfadePageSwap is already gated on motion::ShouldAnimate(), so "animations
// off" makes this an instant, correct swap rather than a slow one.
void PresentMode(InspectRailView const& v, bool messageMode) {
  auto incoming = messageMode ? v.messageScroll : v.conversationScroll;
  auto outgoing = messageMode ? v.conversationScroll : v.messageScroll;
  if (!incoming) return;
  const bool swapping = outgoing && outgoing.Visibility() == Visibility::Visible;
  urnw::motion::CrossfadePageSwap(swapping ? outgoing : incoming, incoming);
}
```

- [ ] **Step 4: Add `MakeInspectRail()`** and its scroller helper. Put the helper in the anonymous namespace and `MakeInspectRail` in `namespace urmsg::views` above the closing brace.

```cpp
// (anonymous namespace)
ScrollViewer MakeBodyScroller() {
  ScrollViewer scroller;
  scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
  scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
  scroller.Visibility(Visibility::Collapsed);
  StackPanel body;
  scroller.Content(body);
  return scroller;
}
```

```cpp
// (namespace urmsg::views)
InspectRailView MakeInspectRail() {
  InspectRailView v;

  Grid root;
  // The view paints its own surface rather than depending on the host's style:
  // a view module that is transparent unless its container happens to carry
  // UrPaneStyle is a module with an invisible dependency.
  root.Background(urnw::colors::BackgroundBrush());
  RowDefinition headerRow, bodyRow;
  headerRow.Height(GridLengthHelper::Auto());
  bodyRow.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  root.RowDefinitions().Append(headerRow);
  root.RowDefinitions().Append(bodyRow);

  // The 40 DIP strip every pane in this window opens with, by the same two keys
  // MainWindow.xaml uses in markup (lines 143-151), so a pane built in code and
  // a pane declared in XAML are the same pane.
  Border header;
  if (auto style = StyleByKey(L"UrPaneHeaderStyle")) header.Style(style);
  TextBlock title;
  if (auto style = StyleByKey(L"UrPaneTitleStyle")) title.Style(style);
  title.Text(L"Details");
  header.Child(title);
  root.Children().Append(header);

  // Both bodies live in the same Grid cell, both collapsed. See the header.
  v.conversationScroll = MakeBodyScroller();
  Grid::SetRow(v.conversationScroll, 1);
  root.Children().Append(v.conversationScroll);

  v.messageScroll = MakeBodyScroller();
  Grid::SetRow(v.messageScroll, 1);
  root.Children().Append(v.messageScroll);

  v.root = root;
  SetRailAdvanced(v, false);
  SetRailSubject(v, L"", L"");
  return v;
}
```

- [ ] **Step 5: Add `MakeMemberRow()` and `MakeDeviceRow()`** in `namespace urmsg::views`.

```cpp
kit::PaneListRow MakeMemberRow(demo::MemberRef const& member) {
  auto row = kit::MakePaneListRow(36);
  const size_t online = OnlineDeviceCount(member);
  row.dot.Fill(0 < online ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                          : urnw::colors::FaintBrush());

  std::wstring title = member.displayName;
  if (member.admin) title += L" · Admin";  // U+00B7 middle dot
  row.title.Text(H(title));

  // The meta states the presence in WORDS. That is what entitles the dot beside
  // it to remain a restatement - BuildPaneListRowParts marks that dot Raw on
  // exactly that assumption (UrComponents.cpp:246-248) - and it is how this row
  // obeys "colour is never the only carrier of state".
  const std::wstring presence = std::format(L"{}/{} online", online, member.devices.size());
  row.meta.Text(H(presence));

  // MakePaneListRow does NOT mark title and meta Raw; only MakePaneListRowButton
  // does (UrComponents.cpp:295-298). Without these two lines the row's own name
  // is announced and then both children are announced again after it - the exact
  // triple announcement the row name exists to prevent. A THIRD caller needing
  // this is the signal to move the two calls down into BuildPaneListRowParts so
  // both row forms carry it; MakeDeviceRow below is the second.
  automation::AutomationProperties::SetAccessibilityView(
      row.title, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetAccessibilityView(
      row.meta, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetName(row.root, H(title + L", " + presence));
  return row;
}

kit::PaneListRow MakeDeviceRow(demo::DeviceRef const& device) {
  auto row = kit::MakePaneListRow(36);
  row.dot.Fill(device.online ? urnw::colors::MakeBrush(urnw::colors::kUrGreen)
                             : urnw::colors::FaintBrush());

  std::wstring title = device.name;
  const std::wstring owner =
      device.isThisComputer ? std::wstring{L"This computer"} : device.ownerName;
  if (!owner.empty()) title += L" · " + owner;  // U+00B7 middle dot
  row.title.Text(H(title));

  // Same rule as MakeMemberRow: the WORD carries the state and the dot restates
  // it. An online device reads "online"; an offline one reads when it was last
  // seen. Nothing here is legible only by hue.
  const std::wstring meta = device.online ? std::wstring{L"online"} : device.lastSeenLabel;
  row.meta.Text(H(meta));

  automation::AutomationProperties::SetAccessibilityView(
      row.title, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetAccessibilityView(
      row.meta, automation::Peers::AccessibilityView::Raw);
  automation::AutomationProperties::SetName(
      row.root, H(title + L", " + (device.online ? L"online" : L"offline") + L", " +
                  device.lastSeenLabel));
  return row;
}
```

- [ ] **Step 6: Add the subject-row helper** to the anonymous namespace. This is the app's first use of the deterministic identicon (Spec C §W9, design §5).

```cpp
// Conversation mode's subject: the identicon, the name, and one muted line under
// it. 56 tall so a 40x40 identicon sits in it on the pane's own rhythm.
// MakeIdenticon applies its OWN CornerRadius(8) - nothing here sets one.
FrameworkElement MakeSubjectRow(demo::Conversation const& conv) {
  auto root = kit::MakePaneRow(56);

  Grid grid;
  grid.ColumnSpacing(10);
  ColumnDefinition iconColumn, textColumn;
  iconColumn.Width(GridLengthHelper::Auto());
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  grid.ColumnDefinitions().Append(iconColumn);
  grid.ColumnDefinitions().Append(textColumn);

  auto identicon = urmsg::MakeIdenticon(conv.identityKey, 40);
  identicon.VerticalAlignment(VerticalAlignment::Center);
  automation::AutomationProperties::SetAccessibilityView(
      identicon, automation::Peers::AccessibilityView::Raw);
  grid.Children().Append(identicon);

  StackPanel text;
  text.VerticalAlignment(VerticalAlignment::Center);
  TextBlock name;
  if (auto style = StyleByKey(L"UrRowTitleStyle")) name.Style(style);
  name.Text(H(conv.name));
  text.Children().Append(name);

  TextBlock note;
  if (auto style = StyleByKey(L"UrRowNoteStyle")) note.Style(style);
  // members.size(), NOT memberCount. DemoWorld's --diagnose invariant 2 requires
  // them equal, and members.size() is what decides how many rows the MEMBERS
  // list below actually draws - so this line and that list cannot disagree in a
  // screenshot, whatever memberCount happens to say.
  note.Text(H(conv.kind == demo::ConversationKind::Group
                  ? std::format(L"Group, {} members", conv.members.size())
                  : std::wstring{L"Direct message"}));
  text.Children().Append(note);
  Grid::SetColumn(text, 1);
  grid.Children().Append(text);

  root.Child(grid);
  return root;
}
```

- [ ] **Step 7: Add `PopulateConversation()` to the anonymous namespace, and `SetInspectRailConversation()` to `namespace urmsg::views`.** The split is the point: populate rebuilds the child list and starts nothing, present runs the one animation.

```cpp
// (anonymous namespace) POPULATE ONLY. No storyboard, so the density switch can
// call this on a rail that is already on screen without the rail flashing.
void PopulateConversation(InspectRailView const& v, demo::Conversation const& conv,
                          bool advanced) {
  auto panel = BodyOf(v.conversationScroll);
  if (!panel) return;
  auto body = panel.Children();
  body.Clear();
  body.Append(MakeSubjectRow(conv));

  body.Append(kit::MakePaneGroupHeader(L"MEMBERS", H(std::format(L"{}", conv.members.size())))
                  .root);
  if (conv.members.empty()) {
    body.Append(kit::MakePaneEmptyLine(L"No members"));
  } else {
    for (auto const& member : conv.members) body.Append(MakeMemberRow(member).root);
  }

  body.Append(kit::MakePaneGroupHeader(L"RETENTION").root);
  for (auto const& field : BuildConversationFields(conv, advanced))
    body.Append(kit::MakePaneKeyValueRow(H(field.key), H(field.value), 34).root);
}
```

```cpp
// (namespace urmsg::views)
void SetInspectRailConversation(InspectRailView& v, demo::Conversation const& c) {
  if (!v.root) return;
  PopulateConversation(v, c, RailAdvanced(v));
  SetRailSubject(v, c.id, L"");
  PresentMode(v, /*messageMode=*/false);
  urnw::LogInfo("rail: conversation mode -> {} ({} members)", winrt::to_string(c.id),
                c.members.size());
}
```

- [ ] **Step 8: Add both files to `app/src/App/App.vcxproj`,** beside the entries R1 added:

```xml
    <ClCompile Include="Views\InspectRailView.cpp" />
```
```xml
    <ClInclude Include="Views\InspectRailView.h" />
```

- [ ] **Step 9: Mount the column in `app/src/App/MainWindow.xaml`.** In `ChatsPage`'s `Grid.ColumnDefinitions`, immediately after `<ColumnDefinition x:Name="ThreadColumn" Width="0" />` (line 132), add:

```xml
                        <ColumnDefinition x:Name="RailRuleColumn" Width="Auto" />
                        <ColumnDefinition x:Name="RailColumn" Width="0" />
```

**STOP rule — this grid is shared.** `Grid.Column` is positional, and `ListColumn`/`RuleColumn`/`ThreadColumn` are already 0/1/2. If another surface task has landed columns here first, do NOT add these blindly: confirm that `RailRuleColumn` and `RailColumn` end up at indices **3 and 4** and that the two elements in Step 10 name those indices. A silent shift puts the rail behind the thread pane and nothing errors.

- [ ] **Step 10: Add the rail's two elements to `MainWindow.xaml`.** Immediately after the `</Grid>` that closes `ThreadPane` (line 180, the line after `<Grid x:Name="ThreadBody" Grid.Row="1" />`) and before the `</Grid>` that closes `ChatsPage`:

```xml
                    <!-- The inspector rail (design 6.3, D3). A THIRD pane, on
                         the same rule-and-fill grammar as the other two, live
                         only at or above 1500 DIP (design 6.5a) and only in the
                         demo world. Its content is built in code:
                         Views/InspectRailView.h. -->
                    <Border x:Name="RailRule" Grid.Column="3"
                            Style="{StaticResource UrPaneVRuleStyle}"
                            Visibility="Collapsed" />
                    <Grid x:Name="InspectRailHost" Grid.Column="4" Visibility="Collapsed"
                          Style="{StaticResource UrPaneStyle}" />
```

- [ ] **Step 11: Declare the rail in `app/src/App/MainWindow.xaml.h`.** Add the two includes immediately after `#include "UrComponents.h"` (line 21):

```cpp
#include "Demo/DemoSwitches.h"
#include "Views/InspectRailView.h"
```

Add the two methods to the private section, immediately after `void ApplyBreakpoint();`:

```cpp
  // The inspector rail (design 6.3). Built once, in the demo world only: a
  // non-demo launch at 1600 DIP must not grow an empty third pane.
  void BuildInspectRail();

  // The rail's own breakpoint. SEPARATE from ApplyBreakpoint's 1000 because it
  // is a different threshold (the third pane at 1500, design 6.5a), and the two
  // early-outs would otherwise mask each other.
  void ApplyRailBreakpoint(double width);
```

and the four members immediately after `bool breakpointApplied_ = false;`:

```cpp
  // Parsed ONCE, in the constructor, and read by every surface that needs it.
  // Five surfaces each calling ParseDemoOptions() would be five copies of the
  // switch state that can drift, and five CommandLineToArgvW calls.
  urmsg::demo::DemoOptions demo_{};
  urmsg::views::InspectRailView rail_{};
  bool railWide_ = false;
  bool railApplied_ = false;
```

- [ ] **Step 12: Build and populate the rail in `app/src/App/MainWindow.xaml.cpp`.** Add one include beside the existing ones (after `#include "UrColors.h"`, line 17):

```cpp
#include "Demo/DemoWorld.h"
```

Add the parse as the first statement after `InitializeComponent();`:

```cpp
  // Parsed once, here, for every surface. See MainWindow.xaml.h.
  demo_ = urmsg::demo::ParseDemoOptions();
```

Add the build call immediately after `BuildConversationList();`:

```cpp
  BuildInspectRail();
```

and add the method immediately after `BuildConversationList()`:

```cpp
void MainWindow::BuildInspectRail() {
  if (!demo_.enabled) return;

  rail_ = urmsg::views::MakeInspectRail();
  InspectRailHost().Children().Clear();
  InspectRailHost().Children().Append(rail_.root);

  auto const& world = urmsg::demo::GetWorld();
  if (world.conversations.empty()) {
    // DemoWorld's --diagnose invariant 1 requires 8 conversations, so this is a
    // DemoWorld failure and --diagnose already names it. The rail stays empty
    // rather than inventing a subject.
    urnw::LogWarn("rail: not populated - the demo world has no conversations");
    return;
  }

  // Conversation 0 is what --demo opens on (fixed contract 2). The
  // conversation-list surface drives this afterwards, from the onSelect(int)
  // callback its MakeConversationList takes.
  urmsg::views::SetInspectRailConversation(rail_, world.conversations.front());
}
```

- [ ] **Step 13: Add `ApplyRailBreakpoint` and call it from `ApplyBreakpoint`.** In `ApplyBreakpoint()`, immediately after `if (width <= 0) return;` (line 168):

```cpp
  // BEFORE this function's own `wide == wide_` early-out. 1500 and 1000 are
  // different thresholds: a resize that crosses 1500 without crossing 1000 must
  // still move the rail, and it would not if this sat below the early-out.
  ApplyRailBreakpoint(width);
```

and add the method immediately after `ApplyBreakpoint`:

```cpp
void MainWindow::ApplyRailBreakpoint(double width) {
  // Demo-only AND wide-only. Below 1500 the rail collapses and message inspect
  // is simply unavailable (design 6.5a): no sheet, no fallback, no error. A
  // narrowed demo window is a smaller demo, not a broken one.
  const bool railWide = demo_.enabled && urmsg::views::kInspectRailBreakpointDip <= width;
  // railApplied_, and not just `railWide == railWide_`, for the reason
  // ApplyBreakpoint's own comment gives: a narrow first pass would otherwise
  // early-out having written nothing, leaving the window correct only because
  // the markup defaults happen to spell the collapsed state.
  if (railApplied_ && railWide == railWide_) return;
  railWide_ = railWide;
  railApplied_ = true;

  // This is the ONLY place the rail's width is written, and it writes it from
  // the one constant. Neither SetInspectRail* touches the column, which is why
  // a mode swap cannot move it.
  RailColumn().Width(railWide
                         ? GridLengthHelper::FromPixels(urmsg::views::kInspectRailWidthDip)
                         : GridLengthHelper::FromPixels(0));
  const auto visibility = railWide ? Visibility::Visible : Visibility::Collapsed;
  RailRule().Visibility(visibility);
  InspectRailHost().Visibility(visibility);
  urnw::LogInfo("rail: {} at {:.0f} dip window",
                railWide ? "shown (360 dip column)" : "hidden", width);
}
```

- [ ] **Step 14: Build and capture, then copy the capture aside.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
powershell -ExecutionPolicy Bypass -Command "Copy-Item .verify\urmessage-window-screen.png .verify\rail-chats.png -Force; Copy-Item .verify\urmessage-window-wide.png .verify\rail-chats-1200.png -Force"
```

The copy is not housekeeping: `verify-render.ps1` writes to fixed paths and overwrites them on every run, so R3's and R4's comparisons have nothing to compare against unless this capture is named and kept.

**STOP rule.** If PowerShell answers `A parameter cannot be found that matches parameter name 'AppArgs'`, **Task F1 has not landed**. The script is `[CmdletBinding()]`, so an undeclared parameter is a terminating error and the app never launched. Stop there — do not add the parameter here, and do not launch the exe by hand instead: the capture path and the launch path have to be the same one.

- [ ] **Step 15: Check the log FIRST, then the image.** The log is the only place the window's real width and the rail's applied state are stated as numbers rather than inferred from pixels.

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path .localstate-verify\logs\urmessage-app.log -Pattern 'rail:' | ForEach-Object { $_.Line }"
```

(`verify-render.ps1:153` points `%URMESSAGE_APP_ROOT%` at `<repo>\.localstate-verify`, and `Paths.cpp:52` + `Ids.h:72` put the log at `<root>\logs\urmessage-app.log`.)

Expected, in this order, three lines:

```
... rail: conversation mode -> <conversation id> (<M> members)
... rail: shown (360 dip column) at 1560 dip window
... rail: hidden at 1200 dip window
```

The third comes from the script's own closing resize to 1200 DIP — that is `ApplyRailBreakpoint`'s other half, and it is the half no first capture ever exercises.

**STOP rule.** If the second line reads `rail: hidden at 480 dip window`, the demo launch size (D7, 1560×900) has not landed. The rail is correctly hidden and this is a Consumes failure, not a rail defect. If there is no `rail:` line at all, `--demo=chats` did not reach `ParseDemoOptions()`.

- [ ] **Step 16: Read `.verify/rail-chats.png` and check it against this list.** The capture is in **PHYSICAL pixels** and the script printed `window dpi : <dpi> (scale <n>)`: every DIP figure below is that many pixels times the printed scale — at 125% a 36 DIP row is 45 px and the 360 DIP column is 450 px. Take `M` from the log line in Step 15.

  1. The rightmost **360 DIP** of the window is a third column, with a 1 px `#1FFFFFFF`-over-`#101010` vertical rule down its left edge — a second rule, matching the one between the list and the thread.
  2. Its top **40 DIP** is a `#151515` strip whose left-aligned text reads `Details`, in the same small letterspaced voice as `Conversations` on the far left, inset **12 DIP** from the column's left edge.
  3. Under it, a **56 DIP** row: a 40×40 rounded-corner identicon at the left, the conversation's name beside it in `#F8F8F8` at 13, and a muted 11 px line under the name reading either `Direct message` or `Group, M members`.
  4. The identicon's colours contain **none** of `#F8523B`, `#87FB67`, `#638BFC`, `#E6EA23` or `#FFC400` — the identicon palette is constrained so an avatar can never be mistaken for a state (fixed contract §3).
  5. Then a **28 DIP** `#151515` strip reading `MEMBERS` with `M` right-aligned.
  6. Then **exactly M** rows of **36 DIP**, each with a 7 px dot at the left, a name, and a right-aligned meta of the form `<n>/<d> online`. Count the rows: the header figure and the row count both come from `conv.members.size()`, and DemoWorld's `--diagnose` invariant 2 requires `memberCount` to agree with it — so a disagreement anywhere here is a DemoWorld defect, not a layout one.
  7. Then a `RETENTION` strip and **exactly two** 34 DIP rows: `Retention` and `Media`, keys left in `#989898`, values right in `#F8F8F8`. Neither value is clipped at the column's right edge.
  8. No gold anywhere in the column: `#FFC400` is the Pro entitlement and nothing else.

- [ ] **Step 17: Read `.verify/rail-chats-1200.png` — the script's 1200 DIP resize — and confirm the rail is GONE.** No third column, no second vertical rule, and the thread pane runs to the window's right edge. This is design §6.5a's behaviour and it matches the `rail: hidden at 1200 dip window` log line from Step 15.

- [ ] **Step 18: Commit.**

```
powershell -ExecutionPolicy Bypass -Command "git ls-files | Measure-Object -Line"
git add app/src/App/Views/InspectRailView.h app/src/App/Views/InspectRailView.cpp app/src/App/App.vcxproj app/src/App/MainWindow.xaml app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m "demo: the inspector rail column and its conversation mode"
powershell -ExecutionPolicy Bypass -Command "git ls-files | Measure-Object -Line"
```

The count must rise by exactly 2.

**Deliverable, independently checkable:** `.verify/rail-chats.png` at 1560 DIP shows a 360 DIP third pane with a `Details` header, the identicon subject row, exactly `M` member rows each stating its presence in words, and the two retention rows; `.verify/rail-chats-1200.png` shows no rail at all; and `.localstate-verify/logs/urmessage-app.log` states both halves of the breakpoint as numbers. `SetInspectRailMessage` and `SetInspectRailAdvanced` are declared and not yet defined — nothing calls them, so nothing links against them.

---

## Task R3: Inspector rail — message mode, the failed-message affordance and the `kBaseMs` crossfade

**Files:**

Modify app/src/App/Views/InspectRailView.cpp; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: R1 (`BuildMessageFields`, `PickInspectMessage`, `DeliveryLabel`) and R2 (`InspectRailView`, `MakeInspectRail`, `PopulateConversation`/`PresentMode`/`BodyOf`/`StyleByKey`/`H`, the mounted column, `MainWindow::BuildInspectRail`, `demo_`). The foundation task that owns `Demo/DemoSwitches.h` (`DemoScreen::Inspect`). **Task F1** (`verify-render.ps1 -AppArgs`). The demo launch size (D7).
- Produces: `void urmsg::views::SetInspectRailMessage(InspectRailView& v, demo::Conversation const& c, demo::MessageRow const& m);` — defined here (declared in R2's header). This is the seam `ThreadView`'s `onSelectMessage` calls on a bubble click, paired with `SetInspectRailConversation`, which its `onDeselect` calls on a click into empty thread space.

# Task R3: Inspector rail — message mode, the failed-message affordance and the `kBaseMs` crossfade

Message mode is the reason the rail exists (D3): the thread and the evidence in one frame. This task builds the mode, the failed-message affordance design §9.1 requires, and the swap between the two modes. The device lists — the load-bearing part — are R4, so this task ends with a rail that shows design §6.3's eight fields and nothing it cannot yet justify.

The swap goes through `urnw::motion::CrossfadePageSwap`, already the app's one page-transition path and already fading the incoming element over `kBaseMs` on the standard curve. That is exactly what design §7's `Rail mode swap` row asks for, so this task adds **no** new duration, curve or storyboard, and it adds no second call site: `PresentMode` from R2 is the only place `CrossfadePageSwap` appears in this module, and Step 8 checks that.

- [ ] **Step 1: Add the lock-header helper** to `InspectRailView.cpp`'s anonymous namespace, beside `MakeSubjectRow`.

```cpp
// Message mode's subject: a padlock, what the product WILL say about a message,
// and the cipher under it.
//
// The judgement is recorded here because design 2 makes it a hard constraint
// ("no copy in the demo may state that a message WAS encrypted as a fact about a
// real operation"): the cipher name and every field below it are FABRICATED
// values from DemoWorld, this header renders only inside the demo world, and the
// DEMO chip in the title bar (D2) is the mitigation the design itself names. If
// the owner reads "End-to-end encrypted" as too strong, the single change is
// this one string - nothing else in the rail depends on it.
FrameworkElement MakeLockHeader(demo::MessageInspect const& inspect) {
  auto root = kit::MakePaneRow(56);

  Grid grid;
  grid.ColumnSpacing(10);
  ColumnDefinition iconColumn, textColumn;
  iconColumn.Width(GridLengthHelper::Auto());
  textColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
  grid.ColumnDefinitions().Append(iconColumn);
  grid.ColumnDefinitions().Append(textColumn);

  FontIcon lock;
  // UrRowIconStyle carries the family (Segoe Fluent Icons, named explicitly so
  // FontIcon does not fall back to the older Segoe MDL2), the size (16) and
  // AccessibilityView Raw. The size is NOT overridden: styles-by-key is what
  // stops a screen acquiring a second icon weight, and the rail's one icon has
  // no claim to be the exception. Only the colour is set, and the words beside
  // it say the same thing, so the colour is a restatement.
  if (auto style = StyleByKey(L"UrRowIconStyle")) lock.Style(style);
  lock.Glyph(L"\uE72E");  // Segoe Fluent E72E, Lock
  lock.Foreground(urnw::colors::MakeBrush(urnw::colors::kUrGreen));
  grid.Children().Append(lock);

  StackPanel text;
  text.VerticalAlignment(VerticalAlignment::Center);
  TextBlock title;
  if (auto style = StyleByKey(L"UrBodyStrongTextStyle")) title.Style(style);
  title.Text(L"End-to-end encrypted");
  text.Children().Append(title);
  TextBlock note;
  if (auto style = StyleByKey(L"UrRowNoteStyle")) note.Style(style);
  note.Text(H(inspect.cipher));
  text.Children().Append(note);
  Grid::SetColumn(text, 1);
  grid.Children().Append(text);

  root.Child(grid);
  return root;
}
```

- [ ] **Step 2: Add the failure block** below it, in the same anonymous namespace. This is design §9.1's `Click a failed message | Shows its reason and a [ Try again ] affordance`, in full.

```cpp
// The reason AND the affordance, because design 9.1 asks for both - and the
// button is EXPLICITLY DISABLED, because the same section forbids anything that
// looks live and does nothing and there is no send path in the demo to retry
// into (design 2). UrButtonBaseStyle's Disabled visual state draws the button at
// 38% opacity (App.xaml:371-375), so "present but not available" is VISIBLE
// rather than something a user discovers by clicking.
//
// UrPaneActionSecondaryStyle, not UrSecondaryButtonStyle: the latter is the
// 48-tall, 24pt NeueBit hero button, which in a 360 DIP rail would be a slab.
// This one is 40 tall, radius 4, outlined, with its own 12 DIP inset.
void AppendFailureBlock(UIElementCollection const& body, std::wstring const& reason) {
  auto row = kit::MakePaneRow(40);
  TextBlock line;
  if (auto style = StyleByKey(L"UrRowTitleStyle")) line.Style(style);
  line.Foreground(urnw::colors::DangerBrush());
  line.Text(H(reason));
  row.Child(line);
  body.Append(row);

  Button retry;
  if (auto style = StyleByKey(L"UrPaneActionSecondaryStyle")) retry.Style(style);
  retry.Content(winrt::box_value(winrt::hstring{L"Try again"}));
  retry.IsEnabled(false);
  // A Button whose Content is text still gets a name from that text, but the
  // reason it cannot be pressed is not in it. This project has paid twice for
  // controls that reach a screen reader as "button" and nothing else.
  automation::AutomationProperties::SetName(retry, L"Try again, not available in the demo");
  body.Append(retry);
}
```

- [ ] **Step 3: Add `PopulateMessage()`** to the anonymous namespace, beside `PopulateConversation`. Populate only — it starts no storyboard.

```cpp
void PopulateMessage(InspectRailView const& v, demo::Conversation const& conv,
                     demo::MessageRow const& row, bool advanced) {
  auto panel = BodyOf(v.messageScroll);
  if (!panel) return;
  auto body = panel.Children();
  body.Clear();
  body.Append(MakeLockHeader(row.inspect));

  // failureReason is documented non-empty ONLY when state == Failed, and an
  // empty reason must produce NO block rather than an empty one: a StackPanel
  // gives every child its space whether or not the child drew anything, which is
  // the measured defect SetTextOrCollapse exists for.
  if (!row.failureReason.empty()) AppendFailureBlock(body, row.failureReason);

  body.Append(kit::MakePaneGroupHeader(L"MESSAGE").root);
  for (auto const& field : BuildMessageFields(conv, row, advanced))
    body.Append(kit::MakePaneKeyValueRow(H(field.key), H(field.value), 34).root);
}
```

- [ ] **Step 4: Add `SetInspectRailMessage()`** to `namespace urmsg::views`, immediately after `SetInspectRailConversation`.

```cpp
void SetInspectRailMessage(InspectRailView& v, demo::Conversation const& c,
                           demo::MessageRow const& m) {
  if (!v.root) return;
  PopulateMessage(v, c, m, RailAdvanced(v));
  // IDS, not the references we were just handed: `m` is a reference into
  // Conversation::rows, and design 9.2's ambient activity appends to that vector.
  SetRailSubject(v, c.id, m.id);
  PresentMode(v, /*messageMode=*/true);
  urnw::LogInfo("rail: message mode -> {} in {} (state {}, failure \"{}\")",
                winrt::to_string(m.id), winrt::to_string(c.id),
                winrt::to_string(DeliveryLabel(m.state)), winrt::to_string(m.failureReason));
}
```

- [ ] **Step 5: Deep-link `--demo=inspect` in `MainWindow::BuildInspectRail()`.** Add the include beside the others in `MainWindow.xaml.cpp`:

```cpp
#include "Views/InspectRailFields.h"
```

Then replace the final line of `BuildInspectRail()` — `urmsg::views::SetInspectRailConversation(rail_, world.conversations.front());` — with:

```cpp
  auto const& conv = world.conversations.front();

  // ONE initial mode, never both.
  //
  // Calling SetInspectRailConversation and then SetInspectRailMessage back to
  // back would run two crossfades over the SAME two elements in OPPOSITE
  // directions inside one synchronous block: RunCrossfade sets
  // incoming.Opacity(0) and begins a storyboard, so both scrollers end up driven
  // by two clocks at once and each Completed handler collapses its outgoing by
  // reading that contested Opacity. The end state would be a race - and it is
  // the exact frame every --demo=inspect capture depends on.
  //
  // --demo=inspect is a STATE, not a screen (design 8): the thread with a
  // message pre-selected and the rail already in message mode, which is the
  // state a screenshot needs and the state no click can reach for an agent.
  // PickInspectMessage is shared with the thread's selection outline, so both
  // land on the same bubble by construction.
  if (demo_.screen == urmsg::demo::DemoScreen::Inspect) {
    if (auto const* picked = urmsg::views::PickInspectMessage(conv)) {
      urmsg::views::SetInspectRailMessage(rail_, conv, *picked);
      return;
    }
    urnw::LogWarn("rail: --demo=inspect but conversation 0 has no message row");
  }
  urmsg::views::SetInspectRailConversation(rail_, conv);
```

- [ ] **Step 6: Build and capture message mode, then copy the capture aside.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=inspect"
powershell -ExecutionPolicy Bypass -Command "Copy-Item .verify\urmessage-window-screen.png .verify\rail-inspect.png -Force"
```

Same **STOP rule** as R2 Step 14 on `A parameter cannot be found that matches parameter name 'AppArgs'` — that means Task F1 has not landed and nothing launched.

- [ ] **Step 7: Read the log to learn WHICH message was picked and what state it is in.** This is what decides whether check 4 in the next step applies; nothing in the image says it.

```
powershell -ExecutionPolicy Bypass -Command "Select-String -Path .localstate-verify\logs\urmessage-app.log -Pattern 'rail:' | ForEach-Object { $_.Line }"
```

Expected two lines (plus the `hidden at 1200 dip window` line from the script's closing resize):

```
... rail: message mode -> <message id> in <conversation id> (state <Pending|Sent|Delivered|Read|Failed>, failure "<reason or empty>")
... rail: shown (360 dip column) at 1560 dip window
```

The id and the state must match the `pick "<id>" state <word>` figures the `inspect devices` line printed in R1 Step 11 — both come from `PickInspectMessage` on conversation 0. If they differ, the world changed between the two runs and R1's numbers are stale; re-run `--diagnose` before trusting them:

```
powershell -ExecutionPolicy Bypass -Command "Start-Process -FilePath app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -RedirectStandardOutput .verify\diagnose.txt -NoNewWindow -Wait; Get-Content .verify\diagnose.txt -Encoding UTF8 | Select-String 'inspect '"
```

If the log says `rail: --demo=inspect but conversation 0 has no message row`, that is a DemoWorld failure — R1 Step 6's STOP rule covers it.

- [ ] **Step 8: Read `.verify/rail-inspect.png` and check it against this list.** DIP figures are physical pixels times the `window dpi` scale the script printed.

  1. The rail's header strip still reads `Details` and the column is still 360 DIP wide with its rule in the same place as `.verify/rail-chats.png` — the mode changed the BODY, not the column.
  2. The body's first row is **56 DIP**: a green (`#87FB67`) padlock at the left inset 12 DIP, `End-to-end encrypted` in `#F8F8F8` semibold, and the cipher under it in muted 11.
  3. **If Step 7's state word was `Failed`:** immediately under the lock header, one **40 DIP** row whose text is `#F8523B` and reads exactly the `failure "..."` string from the log, then a **40 DIP** outlined button reading `Try again`, inset 12 DIP from both edges, drawn dim (38% opacity) because it is disabled. **If the state was anything else:** neither element is present and there is no blank gap where they would be — the `MESSAGE` strip starts immediately under the lock header.
  4. Then a **28 DIP** `#151515` `MESSAGE` strip.
  5. Then **exactly eight** 34 DIP key/value rows, top to bottom, in this order: `Sender`, `Sent`, `Received`, `Epoch`, `Sender leaf index`, `Retention class`, `Size`, `Attestation`. Keys left in `#989898`, values right in `#F8F8F8`.
  6. `Sender` reads a person's name or `You` — never blank. `Epoch` and `Sender leaf index` read as plain integers. `Retention class` reads `Permanent` or `Disappearing` (message vocabulary), which is a different word set from conversation mode's `Retention` row — that is why the two rows have different keys.
  7. No value is clipped at the column's right edge and none wraps to a second line. (R1's probe budgets 28 characters for exactly this.)
  8. Neither the identicon subject row nor the `MEMBERS` list is anywhere in the column: conversation mode is collapsed, not stacked underneath.

- [ ] **Step 9: Prove the swap is a crossfade and not a slide, and that neither setter can move the column.** A 250 ms fade is invisible at the script's 1200 ms settle, so this is checked as a code property, which is stronger than a still frame:

```
powershell -ExecutionPolicy Bypass -Command "Write-Host '-- CrossfadePageSwap in InspectRailView.cpp'; Select-String -Path app\src\App\Views\InspectRailView.cpp -Pattern 'CrossfadePageSwap' | ForEach-Object { $_.LineNumber.ToString() + ': ' + $_.Line.Trim() }; Write-Host '-- kInspectRailWidthDip in MainWindow.xaml.cpp'; Select-String -Path app\src\App\MainWindow.xaml.cpp -Pattern 'kInspectRailWidthDip' | ForEach-Object { $_.LineNumber.ToString() + ': ' + $_.Line.Trim() }"
```

Expected: **exactly one** `CrossfadePageSwap` line, inside `PresentMode`, and **exactly one** `kInspectRailWidthDip` line, inside `ApplyRailBreakpoint`'s `RailColumn().Width(...)`. Two `CrossfadePageSwap` hits means a second call site was added and the mode swap can race. Any width assignment outside `ApplyRailBreakpoint` means a setter can move the column.

Then confirm by eye against the two saved captures: `.verify/rail-chats.png` and `.verify/rail-inspect.png` have the same pixel dimensions (the script prints `window rect` for each run), and the rail's left rule and the `Details` strip are at the same x in both. A slide would have moved one of them. There is no click to make here — the two deep links produce the two end states, which is the whole reason `--demo=<screen>` exists.

- [ ] **Step 10: Commit.**

```
powershell -ExecutionPolicy Bypass -Command "git ls-files | Measure-Object -Line"
git add app/src/App/Views/InspectRailView.cpp app/src/App/MainWindow.xaml.cpp
git commit -m "demo: the inspector rail's message mode, crossfaded at kBaseMs"
powershell -ExecutionPolicy Bypass -Command "git ls-files | Measure-Object -Line"
```

The count must be unchanged — this task creates no files.

**Deliverable, independently checkable:** `--demo=inspect` opens with the rail already in message mode, showing the lock header, the failure block when the picked row failed, and design §6.3's eight fields in order, in a column pixel-identical in position to conversation mode's; the log names the picked message and its delivery state, and matches `--diagnose`'s `pick`/`state`; and the module contains exactly one `CrossfadePageSwap` call site. **No click reaches the rail yet:** the thread surface must wire `MakeThread`'s `onSelectMessage` to `SetInspectRailMessage` and its `onDeselect` to `SetInspectRailConversation`, and the list surface must wire `MakeConversationList`'s `onSelect` to `SetInspectRailConversation`. Until those land, `--demo=inspect` is the only route to message mode and design §9.1's defining D3 interaction is unverified.

---

## Task R4: Inspector rail — the delivered-by / read-by device lists and the density switch

**Files:**

Modify app/src/App/Views/InspectRailView.cpp; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: R1 (`FindConversation`, `FindMessageRow`, `ReadByIsSubsetOfDeliveredTo`, `BuildMessageFields`'s advanced list, `InspectRailDeviceProbe`'s output), R2 (`MakeDeviceRow`, `PopulateConversation`, `RailAdvanced`/`SetRailAdvanced`/`RailConversationId`/`RailMessageId`, `MainWindow::BuildInspectRail`, `demo_`), R3 (`PopulateMessage`). The foundation task that owns `Demo/DemoSwitches.h` (`DemoOptions::advanced`, set by `--demo-advanced`). **Task F1** (`verify-render.ps1 -AppArgs`). The demo launch size (D7).
- Produces: `void urmsg::views::SetInspectRailAdvanced(InspectRailView& v, bool advanced);` — defined here (declared in R2's header). This is what the Advanced Mode task's single `OnAdvancedModeChanged` subscriber in `MainWindow` calls so the rail re-renders live with no restart (design §9.1).

# Task R4: Inspector rail — the delivered-by / read-by device lists and the density switch

This is the part of the rail the design calls load-bearing. Spec C §5.3, quoted in design §6.3: *"Delivered is a statement by a device, never by the server."* Naming the devices that received and read a message is a claim most messengers cannot make, and it is why the rail is worth building at all — so it gets its own task rather than a paragraph at the end of the last one.

The density switch lands here too, because `BuildMessageFields(..., true)` already produces the extra rows and the only thing missing is a re-render that keeps the rail's subject. **It re-renders by POPULATING ONLY.** A density change is not a mode swap: running `CrossfadePageSwap` over a scroller that is already visible blanks it to opacity 0 and fades it back over `kBaseMs`, which reads as a mode swap that did not happen. Design §7 assigns `kBaseMs` to the rail *mode* swap. Step 10 checks that this function starts no storyboard.

- [ ] **Step 1: Add the device-list helper** to `InspectRailView.cpp`'s anonymous namespace, beside `AppendFailureBlock`.

```cpp
// One titled list of devices. Design 6.3 and Spec C 5.3's "delivered is a
// statement by a device, never by the server" are the whole reason this is a
// list of DEVICES WITH NAMES and not a state word: the rail names who
// acknowledged and who read, which is the claim the surface exists to make.
//
// The row is MakeDeviceRow - the same species as conversation mode's member
// rows and the Network page's "Your devices" - so the rail's three lists share
// one height, one left edge and one rhythm.
void AppendDeviceList(UIElementCollection const& body, wchar_t const* title,
                      std::vector<demo::DeviceRef> const& devices,
                      wchar_t const* emptyText) {
  body.Append(kit::MakePaneGroupHeader(title, H(std::format(L"{}", devices.size()))).root);
  if (devices.empty()) {
    // One honest centred line, not a gap: "nothing here" and "this failed to
    // load" must not look the same, and an empty StackPanel says neither.
    body.Append(kit::MakePaneEmptyLine(emptyText));
    return;
  }
  for (auto const& device : devices) body.Append(MakeDeviceRow(device).root);
}
```

- [ ] **Step 2: Append the two lists in `PopulateMessage`.** Add these two calls at the very end of the function, immediately after the `for (auto const& field : BuildMessageFields(...))` loop:

```cpp
  AppendDeviceList(body, L"DELIVERED TO", row.inspect.deliveredTo,
                   L"No device has acknowledged this message");
  AppendDeviceList(body, L"READ BY", row.inspect.readBy,
                   L"No device has read this message");
```

- [ ] **Step 3: Add `SetInspectRailAdvanced()` as the LAST function in `namespace urmsg::views`,** immediately above the closing `}  // namespace urmsg::views`. Its position matters: Step 10's gate reads the tail of the file.

```cpp
void SetInspectRailAdvanced(InspectRailView& v, bool advanced) {
  if (!v.root) return;
  SetRailAdvanced(v, advanced);

  // DENSITY ONLY, and therefore POPULATE ONLY. There is deliberately NO
  // PresentMode call in this function: the rail is already on screen in
  // whichever mode it is in, and CrossfadePageSwap would blank the visible
  // scroller to opacity 0 and fade it back over kBaseMs - a mode swap the user
  // did not ask for and cannot have meant. Design 7 assigns kBaseMs to the rail
  // MODE swap. Populating alone is also correct with animations off.
  auto const& world = demo::GetWorld();
  auto const* conv = FindConversation(world, RailConversationId(v));
  // Nothing populated yet (the rail was just built): the flag is stored and the
  // next Set* picks it up. That is the path --demo-advanced takes.
  if (!conv) return;

  const std::wstring messageId = RailMessageId(v);
  if (!messageId.empty()) {
    // RE-RESOLVED, not remembered. Ambient activity (design 9.2) may have
    // appended rows to this conversation since the rail was populated, and a
    // MessageRow* cached at populate time would now point into freed storage.
    if (auto const* row = FindMessageRow(*conv, messageId)) {
      PopulateMessage(v, *conv, *row, advanced);
      return;
    }
  }
  PopulateConversation(v, *conv, advanced);
}
```

- [ ] **Step 4: Drive it from `MainWindow::BuildInspectRail()`.** Insert this immediately after the `InspectRailHost().Children().Append(rail_.root);` line and before `auto const& world = ...`:

```cpp
  // The density is applied through the ONE function that owns density, before
  // anything is populated, so --demo-advanced exercises the same path the
  // Settings toggle will rather than a second one only the deep link can reach.
  //
  // When the Advanced Mode task lands (Demo/AdvancedMode.h, fixed contract 5)
  // this argument becomes urmsg::AdvancedModeEnabled(), and that task adds
  // `urmsg::views::SetInspectRailAdvanced(rail_, on);` to the SINGLE
  // OnAdvancedModeChanged subscriber this window registers. It is demo_.advanced
  // today because AdvancedMode.h does not exist yet and --demo-advanced is the
  // only source of the flag inside this surface's own tasks.
  urmsg::views::SetInspectRailAdvanced(rail_, demo_.advanced);
```

- [ ] **Step 5: Build and capture message mode at NORMAL density,** so the device lists are checked before Advanced Mode is added on top of them.

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=inspect"
powershell -ExecutionPolicy Bypass -Command "Copy-Item .verify\urmessage-window-screen.png .verify\rail-inspect-normal.png -Force"
```

Same **STOP rule** as R2 Step 14 on `A parameter cannot be found that matches parameter name 'AppArgs'`.

- [ ] **Step 6: Get the numbers the capture is checked against, in full.** Do not rely on notes from R1:

```
powershell -ExecutionPolicy Bypass -Command "New-Item -ItemType Directory -Force .verify | Out-Null; Start-Process -FilePath app\build\x64\Release\URmessage.exe -ArgumentList '--diagnose' -RedirectStandardOutput .verify\diagnose.txt -NoNewWindow -Wait; Get-Content .verify\diagnose.txt -Encoding UTF8 | Select-String 'inspect '"
```

Take `D` from `delivered-by <D>` and `R` from `read-by <R>` on the `inspect devices` line. That line's pick and the rail's subject are the same message: both are `PickInspectMessage(world.conversations[0])`.

- [ ] **Step 7: Read `.verify/rail-inspect-normal.png` and check the two lists.** DIP figures are physical pixels times the `window dpi` scale the script printed.

  1. Under the eighth key/value row (`Attestation`) there is a **28 DIP** `DELIVERED TO` strip with `D` right-aligned, then **exactly `D` rows of 36 DIP**.
  2. Each of those rows has a 7 px dot at the left, a device name and its owner joined by a middle dot (`·`) — or `This computer` in place of an owner — and a right-aligned meta reading either the word `online` or a last-seen label such as `2 min ago`. Same left edge, same 36 DIP height and same rhythm as the `MEMBERS` list in `.verify/rail-chats.png`.
  3. Count the rows whose meta reads exactly `online`: that count equals the number of bright dots in the list. The check is on the WORD; the hue only has to agree with it.
  4. Then a `READ BY` strip with `R` right-aligned and **exactly `R` rows**. `R` is less than or equal to `D`, and every device name under `READ BY` also appears under `DELIVERED TO` — the property `--diagnose` asserts over the whole world, now visible for this one message.
  5. If `R` is 0, the strip is followed by one centred faint line reading `No device has read this message`, not by a gap. If `D` is 0, likewise `No device has acknowledged this message`.

- [ ] **Step 8: Capture Advanced Mode and copy it aside.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=inspect --demo-advanced"
powershell -ExecutionPolicy Bypass -Command "Copy-Item .verify\urmessage-window-screen.png .verify\rail-inspect-advanced.png -Force"
```

- [ ] **Step 9: Read `.verify/rail-inspect-advanced.png` and check the extra rows against `.verify/rail-inspect-normal.png`.**

  1. The `MESSAGE` group now holds **exactly twelve** 34 DIP key/value rows: the same eight, in the same order, then `Group id`, `Wire size`, `Message id`, `Conversation id`.
  2. `Group id` renders as eight hex characters, a horizontal ellipsis and four more — 13 characters — right-aligned on one line. Nothing wraps and nothing is clipped at the column's right edge. (If the id was already short enough it renders whole; either way it is inside R1's 28-character budget, which `--diagnose` asserts.)
  3. `Wire size` reads `<n> bytes`.
  4. The `DELIVERED TO` and `READ BY` lists are unchanged — same headers, same `D` and `R`, same rows — and still BELOW the fields. Advanced Mode adds rows; it does not reorder the surface.
  5. The lock header, the column width and the rule position are identical to `.verify/rail-inspect-normal.png`, and the two files have the same pixel dimensions.

- [ ] **Step 10: Prove the density switch starts no animation.** A `kBaseMs` fade is over long before the script's 1200 ms settle, so a still frame cannot catch this. Check the code property instead:

```
powershell -ExecutionPolicy Bypass -Command "$t = Get-Content app\src\App\Views\InspectRailView.cpp -Raw; $i = $t.IndexOf('void SetInspectRailAdvanced'); if ($i -lt 0) { Write-Host 'SetInspectRailAdvanced NOT FOUND' } else { Write-Host ('CrossfadePageSwap call sites in file : ' + ([regex]::Matches($t,'CrossfadePageSwap\(')).Count); Write-Host ('PresentMode calls inside/after SetInspectRailAdvanced : ' + ([regex]::Matches($t.Substring($i),'PresentMode\(')).Count) }"
```

Expected exactly:

```
CrossfadePageSwap call sites in file : 1
PresentMode calls inside/after SetInspectRailAdvanced : 0
```

The first number proves `PresentMode` is still the module's only transition path. The second proves `SetInspectRailAdvanced` — placed last in the file by Step 3 — does not reach it. Either number rising means the rail now blanks and re-fades on every Advanced Mode toggle.

- [ ] **Step 11: Confirm conversation mode's Advanced density too, and copy it aside.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats --demo-advanced"
powershell -ExecutionPolicy Bypass -Command "Copy-Item .verify\urmessage-window-screen.png .verify\rail-chats-advanced.png -Force"
```

In `.verify/rail-chats-advanced.png`, read the subject row's second line first, because it decides the expected count:

  - It reads `Direct message` → the `RETENTION` block has **exactly three** 34 DIP rows: `Retention`, `Media`, `Conversation id`.
  - It reads `Group, M members` → **exactly four**: `Retention`, `Media`, `Conversation id`, `Group id`.

That is the rule `--diagnose` asserts over every conversation (`conversation fields correct in N/N`), so a fourth row on a direct message, or a missing one on a group, is a `BuildConversationFields` defect and the probe would already have said `FAIL`. The `MEMBERS` list above is untouched: the same `M` rows in the same order as `.verify/rail-chats.png`.

- [ ] **Step 12: Commit.**

```
powershell -ExecutionPolicy Bypass -Command "git ls-files | Measure-Object -Line"
git add app/src/App/Views/InspectRailView.cpp app/src/App/MainWindow.xaml.cpp
git commit -m "demo: delivered-by and read-by device lists, and the rail's advanced density"
powershell -ExecutionPolicy Bypass -Command "git ls-files | Measure-Object -Line"
```

The count must be unchanged.

**Deliverable, independently checkable:** four saved captures — `.verify/rail-inspect-normal.png`, `.verify/rail-inspect-advanced.png`, `.verify/rail-chats.png`, `.verify/rail-chats-advanced.png` — showing 8 vs 12 message fields, 2 vs 3-or-4 conversation fields, and the delivered-by / read-by device lists whose counts `D` and `R` and whose subset relationship match the `inspect devices` line in `--diagnose`; plus a code gate showing one `CrossfadePageSwap` call site in the module and none reachable from `SetInspectRailAdvanced`.

**What is NOT delivered here, stated plainly so the plan does not read as if it were.** `SetInspectRailAdvanced` is exercised by `--demo-advanced` through `BuildInspectRail`, which is the same path the Settings toggle will take — but the *toggle* reaches the rail only when the Advanced Mode task adds `SetInspectRailAdvanced(rail_, on)` to the single `OnAdvancedModeChanged` subscriber `MainWindow` registers (fixed contract §5). And no CLICK reaches the rail at all until the thread surface wires `MakeThread`'s `onSelectMessage` / `onDeselect` and the list surface wires `MakeConversationList`'s `onSelect`. Until then, `--demo=inspect` is the only route to message mode.
