# Click graph and ambient activity

> Part of [the URmessage demo UI plan](../2026-09-06-urmessage-demo-ui.md). Read that file's **Global Constraints** first — they apply to every task here.

---

## Task W5: The click graph, part 1: list selection drives the thread, bubble selection drives the rail

**Files:**

Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: `urmsg::demo::GetWorld`, `Conversation`, `MessageRow`, `RowKind` (Demo/DemoWorld.h, world surface, contract v2 §1); `urmsg::views::ConversationListView`, `MakeConversationList`, `SetConversationSelected` (Views/ConversationListView.h, contract v2 §4); `urmsg::views::ThreadView`, `MakeThread`, `SetThreadConversation`, `SetThreadSelectedMessage` (Views/ThreadView.h); `urmsg::views::InspectRailView`, `MakeInspectRail`, `SetInspectRailConversation`, `SetInspectRailMessage` (Views/InspectRailView.h); `MainWindow::DrainDeepLink`, `layout_` (W3, W4)
- Produces: `void MainWindow::BuildDemoViews();` `void MainWindow::RebuildConversationList();` `void MainWindow::SelectConversation(int index);` `void MainWindow::SelectMessage(std::wstring id);` `void MainWindow::ClearMessageSelection();` `int MainWindow::OpenConversationIndex() const;` members `list_`, `thread_`, `rail_`, `openConversationId_`, `selectedMessageId_`

## Task W5: three rows of design doc §9.1's table

Clicking a conversation row opens that thread with the rail in conversation mode; clicking a
bubble swaps the rail to message inspect and outlines the bubble; clicking empty thread
space deselects.

The views own their pixels and their own click wiring — contract v2 §4 hands the callbacks
in at construction (`MakeConversationList(world, onSelect)`,
`MakeThread(onSelectMessage, onDeselect)`), so `MainWindow` never reaches inside a view to
find something to click. It owns the graph only.

**The world is the single source of truth for rows.** `ThreadBubble` carries only `{id,
root}`, so `SelectMessage` looks the `MessageRow` up in
`GetWorld().conversations[i].rows`. That is also what makes ambient activity (W8/W9)
survive a re-selection: the appended row lives in the world, not in a view.

**`ThreadView` is built ONCE.** `SetThreadConversation` re-points it. Rebuilding it per
selection would drop the two callbacks it was constructed with.

- [ ] **Step 1: Declare the members and methods in `app/src/App/MainWindow.xaml.h`.**

Add to the include block:

```cpp
#include <vector>

#include "Demo/DemoWorld.h"
#include "Views/ConversationListView.h"
#include "Views/InspectRailView.h"
#include "Views/ThreadView.h"
```

Add to `private:`, under `void DrainDeepLink();`:

```cpp
  // The composer's click graph (design doc 9.1). Every one of these is reached
  // through a callback a view was CONSTRUCTED with, so MainWindow never walks a
  // view's element tree looking for something to attach to.
  void BuildDemoViews();
  void RebuildConversationList();
  void SelectConversation(int index);
  // By value, not string_view: this is called with selectedMessageId_ and it
  // writes selectedMessageId_. A view aliasing the member it is about to change
  // is a use-after-free one edit away, with no compiler warning.
  void SelectMessage(std::wstring id);
  void ClearMessageSelection();
  // -1 when nothing is open. Derived from the world every time rather than
  // cached: a Conversation const* into World::conversations is invalidated by
  // anything that appends, and ambient activity appends.
  int OpenConversationIndex() const;

  urmsg::views::ConversationListView list_{};
  urmsg::views::ThreadView thread_{};
  urmsg::views::InspectRailView rail_{};
  std::wstring openConversationId_;
  std::wstring selectedMessageId_;
```

- [ ] **Step 2: Add `OpenConversationIndex()` and `RebuildConversationList()` to `MainWindow.xaml.cpp`.**

```cpp
int MainWindow::OpenConversationIndex() const {
  if (openConversationId_.empty()) return -1;
  auto const& conversations = urmsg::demo::GetWorld().conversations;
  for (size_t i = 0; i < conversations.size(); ++i)
    if (conversations[i].id == openConversationId_) return static_cast<int>(i);
  return -1;
}

void MainWindow::RebuildConversationList() {
  // The view wires its own rows and calls back with an INDEX into
  // World::conversations (contract v2 section 4), so there is one place that
  // knows how a row maps to a conversation and it is not here.
  list_ = urmsg::views::MakeConversationList(
      urmsg::demo::GetWorld(), [weak = get_weak()](int index) {
        if (auto self = weak.get()) self->SelectConversation(index);
      });
  ListHost().Children().Clear();
  if (!list_.root) {
    urnw::LogError("window: MakeConversationList returned no root - the demo list pane is empty");
    return;
  }
  ListHost().Children().Append(list_.root);
  const int open = OpenConversationIndex();
  if (0 <= open) urmsg::views::SetConversationSelected(list_, open);
}
```

- [ ] **Step 3: Add `BuildDemoViews()` to `MainWindow.xaml.cpp`.**

```cpp
void MainWindow::BuildDemoViews() {
  RebuildConversationList();

  // Built once. SetThreadConversation re-points it; rebuilding it per selection
  // would drop the two callbacks it was constructed with, and would re-run the
  // bubble entrance animation for a conversation the viewer is already in.
  thread_ = urmsg::views::MakeThread(
      [weak = get_weak()](std::wstring id) {
        if (auto self = weak.get()) self->SelectMessage(std::move(id));
      },
      [weak = get_weak()]() {
        if (auto self = weak.get()) self->ClearMessageSelection();
      });
  ThreadHost().Children().Clear();
  if (thread_.root)
    ThreadHost().Children().Append(thread_.root);
  else
    urnw::LogError("window: MakeThread returned no root - the demo thread pane is empty");

  rail_ = urmsg::views::MakeInspectRail();
  RailHost().Children().Clear();
  if (rail_.root)
    RailHost().Children().Append(rail_.root);
  else
    urnw::LogError("window: MakeInspectRail returned no root - the inspector rail is empty");
}
```

- [ ] **Step 4: Add `SelectConversation()`, `SelectMessage()` and `ClearMessageSelection()` to `MainWindow.xaml.cpp`.**

```cpp
void MainWindow::SelectConversation(int index) {
  auto const& conversations = urmsg::demo::GetWorld().conversations;
  if (index < 0 || conversations.size() <= static_cast<size_t>(index)) return;
  auto const& conversation = conversations[static_cast<size_t>(index)];
  // Re-clicking the conversation that is already open is a NO-OP, not a rebuild.
  // A rebuild would re-run the thread's entrance animation and drop the message
  // selection for something the viewer did not ask to change.
  if (conversation.id == openConversationId_) return;

  openConversationId_ = conversation.id;
  selectedMessageId_.clear();
  urmsg::views::SetConversationSelected(list_, index);
  urmsg::views::SetThreadConversation(thread_, conversation);
  urmsg::views::SetInspectRailConversation(rail_, conversation);
  urnw::LogInfo("window: conversation -> {} (index {})",
                urnw::Narrow(openConversationId_), index);
}

void MainWindow::SelectMessage(std::wstring id) {
  // Below kRailBreakpointDip there IS no rail, so selecting a message does
  // nothing visible and message inspect is unavailable until the window is
  // widened again (design doc 6.5a). No sheet, no fallback, no error - a
  // narrowed demo window is a smaller demo, not a broken one.
  if (!layout_.rail) {
    urnw::LogInfo("window: message inspect unavailable below {:.0f} dip of content width",
                  urmsg::demo::kRailBreakpointDip);
    return;
  }
  const int index = OpenConversationIndex();
  if (index < 0) return;
  auto const& conversation =
      urmsg::demo::GetWorld().conversations[static_cast<size_t>(index)];
  for (auto const& row : conversation.rows) {
    if (row.id != id) continue;
    selectedMessageId_ = id;
    urmsg::views::SetThreadSelectedMessage(thread_, selectedMessageId_);
    urmsg::views::SetInspectRailMessage(rail_, conversation, row);
    urnw::LogInfo("window: message inspect -> {}", urnw::Narrow(selectedMessageId_));
    return;
  }
  urnw::LogWarn("window: no row {} in conversation {}", urnw::Narrow(id),
                urnw::Narrow(conversation.id));
}

void MainWindow::ClearMessageSelection() {
  if (selectedMessageId_.empty()) return;
  selectedMessageId_.clear();
  urmsg::views::SetThreadSelectedMessage(thread_, std::wstring{});
  const int index = OpenConversationIndex();
  if (0 <= index)
    urmsg::views::SetInspectRailConversation(
        rail_, urmsg::demo::GetWorld().conversations[static_cast<size_t>(index)]);
  urnw::LogInfo("window: message inspect cleared");
}
```

- [ ] **Step 5: Clear a stranded selection when the rail collapses.**

In `MainWindow::ApplyBreakpoint()`, immediately after the
`RailHost().Visibility(railVisibility);` line, add:

```cpp
  // An outlined bubble with no rail beside it is an affordance pointing at
  // nothing. Dropping below the breakpoint drops the selection with the rail.
  if (!rail && !selectedMessageId_.empty()) ClearMessageSelection();
```

- [ ] **Step 6: Build the views and run the rest of the deep link.**

In `MainWindow::EnterDemoMode()`, immediately BEFORE the two lines

```cpp
  pendingLink_ = link;
  pendingLinkArmed_ = true;
```

add:

```cpp
  BuildDemoViews();
```

Then extend `MainWindow::DrainDeepLink()`, after its `SelectNavTag(pendingLink_.navTag);`
line and before its `urnw::LogInfo`:

```cpp
  // Named explicitly rather than taken from the view's first row: World is
  // seeded and deterministic (design doc 5), so conversation 0 is a stable
  // target, and it stays the right target even if something later reorders the
  // list's children.
  if (pendingLink_.selectConversation) SelectConversation(0);
  if (pendingLink_.selectMessage) {
    // `inspect` is `thread` with the rail already in message mode - the state a
    // screenshot needs, since input may not be synthesised. The NEWEST Message
    // row, chosen by kind so a day separator or a system row can never become
    // the inspect target.
    auto const& rows = urmsg::demo::GetWorld().conversations.front().rows;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
      if (it->kind != urmsg::demo::RowKind::Message) continue;
      SelectMessage(it->id);
      break;
    }
  }
```

- [ ] **Step 7: Build and look at `--demo=thread`.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread"
```

Expected console: `dips        : 1560x900`, `foreground  : YES`.

Read `.verify\urmessage-window.png`. Expected: the left 320-dip column shows **8**
conversation rows (`GetWorld().conversations.size() == 8`, asserted by the world surface's
`--diagnose`); whether the list opens with a group header above them is
`MakeConversationList`'s business — count the rows carrying an identicon, not the children.
The first row carries the selected paint. The middle column shows that conversation's
bubbles. The right 360-dip column is **not** empty and shows conversation details, not
message inspect, because nothing is selected.

Save this frame as the baseline for the A/B:

```powershell
Copy-Item .verify\urmessage-window.png .verify\baseline-demo-thread.png -Force
```

- [ ] **Step 8: Look at `--demo=inspect` and prove it is a DIFFERENT frame.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=inspect"
```

```powershell
(Get-FileHash .verify\urmessage-window.png).Hash -eq (Get-FileHash .verify\baseline-demo-thread.png).Hash
```

Expected: `False`. Read `.verify\urmessage-window.png`. Expected: the same 8 rows and the
same bubbles as step 7, except the LAST bubble now carries the accent selection outline and
the right column has swapped to message mode — a lock header, then labelled fields, then
the delivered-by and read-by device lists with device names.

- [ ] **Step 9: Prove in the log that the rail was live when the message was selected.**

```powershell
$log = '.localstate-verify\logs\urmessage-app.log'
Select-String -Path $log -SimpleMatch 'window: demo deep link ->' | Select-Object -Last 1
(Select-String -Path $log -SimpleMatch 'window: message inspect ->' | Select-Object -Last 1)
(Select-String -Path $log -SimpleMatch 'window: message inspect unavailable').Count
```

Expected: the deep-link line ends `message=true (rail=true)`; there is a
`window: message inspect -> <row id>` line after it; and the `unavailable` count is **0**
for this run. A non-zero count means `SelectMessage` ran before `ApplyBreakpoint` wrote
`layout_`, which is the exact ordering W4 step 8 exists to prevent.

- [ ] **Step 10: Confirm the narrow case is silent, not broken.**

Read `.verify\urmessage-window-wide.png` from the same run (1200×800 window). Expected: list
and thread only, no rail column, and the previously-outlined bubble has NO outline.

```powershell
(Select-String -Path $log -SimpleMatch 'window: message inspect cleared' | Select-Object -Last 1)
```

Expected: one such line, written when the harness resized below the breakpoint.

- [ ] **Step 11: Commit.**

```
git add app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m "demo: click graph - list selects thread, bubble selects rail, empty space deselects"
```

**Deliverable:** `--demo=thread` and `--demo=inspect` produce two frames from the same build
that a file hash proves differ, with no synthesised input, and the log shows zero
`message inspect unavailable` lines at 1560 dip.

---

## Task W6: The click graph, part 2: the status-strip drawer, and every nav destination leading somewhere real

**Files:**

Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: `urmsg::views::StatusStripView`, `MakeStatusStrip`, `SetStatusStripDrawerOpen` (Views/StatusStripView.h, contract v2 §4); `urmsg::views::NetworkPageView`, `MakeNetworkPage` (Views/NetworkPageView.h); `urmsg::views::SettingsView`, `MakeSettings` (Views/SettingsView.h); `urmsg::views::DeveloperView`, `MakeDeveloper` (Views/DeveloperView.h); `urmsg::SetAdvancedModeEnabled`, `urmsg::AdvancedModeEnabled` (W2); `urmsg::demo::GetWorld` (world surface)
- Produces: `void MainWindow::ToggleStatusDrawer();` members `strip_`, `network_`, `settings_`, `developer_`, `drawerOpen_`

## Task W6: the strip's drawer, and no destination that leads nowhere

The remaining reachable rows of design doc §9.1 that this surface can wire: activating the
status strip raises and dismisses the node preview drawer, and the three demo destinations
are built and mounted so every nav item leads to a populated pane. A nav item landing on an
empty pane is exactly the dead-looking affordance §9.1 forbids.

Settings is constructed with the callback contract v2 §4 gives it —
`MakeSettings(onAdvancedChanged, advanced)` — and that callback goes straight to
`urmsg::SetAdvancedModeEnabled` (W2). There is no write-back into a toggle from the
composer, so there is no re-entrancy to guard: the state changes in one place and the views
are told about it in one place (W7).

**Every `Make*` root and every control this task attaches to is null-checked**, and a
missing one is a named WARN line in the log. `BuildDemoViews()` runs inside
`EnterDemoMode()` inside the `MainWindow` constructor inside `App::OnLaunched`'s try/catch
(App.xaml.cpp:89): an `hresult_error` out of a null projection there kills the whole window
with `URmessage could not create its main window` — a catastrophic-looking failure for one
absent button, while the sibling view tasks are still landing.

- [ ] **Step 1: Declare the members and method in `app/src/App/MainWindow.xaml.h`.**

Add to the include block:

```cpp
#include "Views/DeveloperView.h"
#include "Views/NetworkPageView.h"
#include "Views/SettingsView.h"
#include "Views/StatusStripView.h"
```

Add to `private:`:

```cpp
  void ToggleStatusDrawer();

  urmsg::views::StatusStripView strip_{};
  urmsg::views::NetworkPageView network_{};
  urmsg::views::SettingsView settings_{};
  urmsg::views::DeveloperView developer_{};
  bool drawerOpen_ = false;
```

- [ ] **Step 2: Mount the strip and wire its activation in `BuildDemoViews()` (`MainWindow.xaml.cpp`).**

Append to `MainWindow::BuildDemoViews()`:

```cpp
  strip_ = urmsg::views::MakeStatusStrip(urmsg::demo::GetWorld());
  StatusStripHost().Children().Clear();
  if (strip_.root) StatusStripHost().Children().Append(strip_.root);
  if (strip_.strip) {
    strip_.strip.Click([weak = get_weak()](auto const&, auto const&) {
      if (auto self = weak.get()) self->ToggleStatusDrawer();
    });
  } else {
    // Named, not silent. An unattached Click is a strip that looks live and is
    // not - the one thing design doc 9.1 calls a demo bug - and a throw here
    // would kill the window instead.
    urnw::LogWarn("window: demo status strip has no activation button; the drawer cannot open");
  }
```

- [ ] **Step 3: Mount the three destination views in `BuildDemoViews()` (same file).**

Append:

```cpp
  network_ = urmsg::views::MakeNetworkPage(urmsg::demo::GetWorld());
  NetworkHost().Children().Clear();
  if (network_.root)
    NetworkHost().Children().Append(network_.root);
  else
    urnw::LogWarn("window: MakeNetworkPage returned no root; the Network destination is empty");

  // The toggle's only job is to tell the ONE owner (AdvancedMode.h). The owner
  // persists and notifies; the subscriber registered in W7 re-renders. Nothing
  // here writes back into the control, so there is no re-entrant Toggled to
  // guard against and a launch switch can never become a preference write.
  settings_ = urmsg::views::MakeSettings(
      [](bool on) { urmsg::SetAdvancedModeEnabled(on); }, urmsg::AdvancedModeEnabled());
  SettingsHost().Children().Clear();
  if (settings_.root)
    SettingsHost().Children().Append(settings_.root);
  else
    urnw::LogWarn("window: MakeSettings returned no root; the Settings destination is empty");

  developer_ = urmsg::views::MakeDeveloper(urmsg::demo::GetWorld());
  DeveloperHost().Children().Clear();
  if (developer_.root)
    DeveloperHost().Children().Append(developer_.root);
  else
    urnw::LogWarn("window: MakeDeveloper returned no root; the Developer destination is empty");
```

- [ ] **Step 4: Add `ToggleStatusDrawer()` to `MainWindow.xaml.cpp`.**

```cpp
void MainWindow::ToggleStatusDrawer() {
  drawerOpen_ = !drawerOpen_;
  urmsg::views::SetStatusStripDrawerOpen(strip_, drawerOpen_);
  urnw::LogInfo("window: status drawer {}", drawerOpen_ ? "open" : "closed");
}
```

- [ ] **Step 5: Build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

Expected: `Build succeeded`, 0 errors.

- [ ] **Step 6: Look at the strip.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread"
```

Read `.verify\urmessage-window.png`. Expected: a strip runs the full width of the window
along its bottom edge, below the nav pane as well as the content, separated from the content
above it by a single hairline. In Normal mode it carries **four** items and no more — a
state dot, a state word, the server host `urmsg-01.ur.io`, and a lock glyph — because
§6.5 makes it a connect indicator, not a network readout. The drawer is not open, so
nothing is drawn above the strip.

- [ ] **Step 7: Confirm nothing was left unattached.**

```powershell
$log = '.localstate-verify\logs\urmessage-app.log'
Select-String -Path $log -SimpleMatch 'window: demo status strip has no activation button'
Select-String -Path $log -SimpleMatch 'returned no root'
```

Expected: **no output from either command.** Any line here names a view whose contract-v2
signature was implemented but whose element was left at its `{nullptr}` default, and that
view's task is the one to fix — not this one.

- [ ] **Step 8: Look at Network, Settings and Developer.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=network"
```

Read `.verify\urmessage-window.png`. Expected: the header reads `Network`; the pane shows
**three** relay nodes joined by two wires (`World::relayPath.size() == 3`, asserted by the
world surface), then the message-server block with host, jurisdiction, latency and
key-verification state, then **three** linked devices with last-seen labels.

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=settings"
```

Expected: the header reads `Settings`; the pane shows at least the three group headers
**Appearance**, **Privacy** and **Security** populated (§6.6), and an `Advanced Mode` row
with a switch in the **off** position. The pane is not empty.

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=developer"
```

Expected: the header reads `Developer`, the nav pane's footer lists **Developer above
Settings** with Developer selected, and the pane is populated — even though
`--demo-advanced` was not passed, because `DeepLinkFor(Developer).forceAdvanced` is true and
`EnterDemoMode` fed that to `InitAdvancedMode`.

- [ ] **Step 9: Confirm the Contacts destination is honest rather than broken.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=chats"
```

Expected in `.verify\urmessage-window.png`: the nav pane lists Chats, Contacts and Network.
Contacts is left visible on purpose — design doc §6.6's nav table names it in both the
Normal and the Advanced column — and no surface in this plan builds a Contacts view, so it
routes to `StubPage`: header `Contacts`, one muted centred line, which is this app's
existing honest empty state and not a blank pane. That is the expectation; a Contacts pane
that renders *nothing* is a regression in `ShowDestination`.

- [ ] **Step 10: Commit.**

```
git add app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m "demo: status strip drawer wired, three destination views mounted, every Make* null-checked"
```

**Deliverable:** every nav destination renders a populated pane, the strip renders four
fields at the window's foot, and the log contains no `returned no root` or
`no activation button` line.

---

## Task W7: Advanced Mode: one subscriber, five surfaces, density only

**Files:**

Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: `urmsg::OnAdvancedModeChanged`, `urmsg::AdvancedModeEnabled` (W2); `urmsg::views::SetConversationListAdvanced` (Views/ConversationListView.h); `urmsg::views::SetStatusStripAdvanced` (Views/StatusStripView.h); `urmsg::views::SetNetworkPageAdvanced` (Views/NetworkPageView.h); `urmsg::views::SetInspectRailAdvanced` (Views/InspectRailView.h); `MainWindow::SelectNavTag`, `currentTag_`, `list_`, `strip_`, `network_`, `rail_` (W3, W5, W6)
- Produces: `void MainWindow::ApplyAdvanced(bool on);` — the one subscriber contract v2 §5 requires

## Task W7: one toggle, five surfaces, no crossfade

Advanced Mode is not a destination. It is one persisted flag (W2) that changes what four
existing surfaces show and adds a fifth nav item (§6.6). Every one re-renders live, without
a restart — that is the row of §9.1's table this task closes.

Contract v2 §4, verbatim: **"Every `Set*Advanced` changes DENSITY only. It must never run a
mode crossfade."** So the rail is told with `SetInspectRailAdvanced` and never re-shown with
`SetInspectRailMessage`/`SetInspectRailConversation` — re-showing runs the rail's
conversation↔message crossfade, which fades the visible pane to opacity 0 and back over
itself for a change that is not a mode change at all.

`kProGold` / `UrProGoldBrush` (`#FFC400`) is reserved for the Pro entitlement across the
whole product and appears nowhere in this task.

- [ ] **Step 1: Declare the method in `app/src/App/MainWindow.xaml.h`.**

Add to `private:`:

```cpp
  // The ONE subscriber of urmsg::OnAdvancedModeChanged (contract v2 section 5).
  // No view reads the preference; they are all told, from here, in one order.
  void ApplyAdvanced(bool on);
```

- [ ] **Step 2: Add `ApplyAdvanced()` to `app/src/App/MainWindow.xaml.cpp`.**

```cpp
void MainWindow::ApplyAdvanced(bool on) {
  advanced_ = on;

  // Developer is a destination that exists only under Advanced Mode. Leaving it
  // selected while it disappears would strand the window on a hidden
  // destination with no way back.
  DeveloperNavItem().Visibility(on ? Visibility::Visible : Visibility::Collapsed);
  if (!on && currentTag_ == L"developer") SelectNavTag(L"chats");

  urmsg::views::SetConversationListAdvanced(list_, on);
  urmsg::views::SetStatusStripAdvanced(strip_, on);
  urmsg::views::SetNetworkPageAdvanced(network_, on);
  // DENSITY only (contract v2 section 4). NOT SetInspectRailMessage: that runs
  // the rail's conversation<->message crossfade, which would fade the pane the
  // viewer is looking at out over itself for a change that is not a mode change.
  urmsg::views::SetInspectRailAdvanced(rail_, on);

  urnw::LogInfo("window: advanced mode {}", on);
}
```

- [ ] **Step 3: Register the subscriber and apply once, in `EnterDemoMode()` (same file).**

In `MainWindow::EnterDemoMode()`, DELETE this line, which W3 wrote:

```cpp
  DeveloperNavItem().Visibility(advanced_ ? Visibility::Visible
                                          : Visibility::Collapsed);
```

and, immediately AFTER the `BuildDemoViews();` call, add:

```cpp
  // Registered after the views exist, so the first notification cannot reach a
  // half-built window. InitAdvancedMode deliberately notifies nobody, which is
  // why the initial apply is an explicit call here: ONE path into Advanced Mode
  // whatever turned it on - the launch switch, a --demo=developer deep link, the
  // persisted preference, or a click in Settings.
  urmsg::OnAdvancedModeChanged([weak = get_weak()](bool on) {
    if (auto self = weak.get()) self->ApplyAdvanced(on);
  });
  ApplyAdvanced(urmsg::AdvancedModeEnabled());
```

- [ ] **Step 4: Build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

Expected: `Build succeeded`, 0 errors.

- [ ] **Step 5: Capture Normal mode as the baseline.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=inspect"
```

```powershell
Copy-Item .verify\urmessage-window.png .verify\baseline-normal-inspect.png -Force
```

Read `.verify\urmessage-window.png`. Expected: the bottom strip shows exactly **four** items
(dot, state word, host, lock); conversation rows show name, preview and time with **no**
group-id chip; the rail's message mode shows labelled fields with **no** hex group id; the
nav pane's footer has Settings only.

- [ ] **Step 6: The same deep link, one switch different.**

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=inspect --demo-advanced"
```

```powershell
(Get-FileHash .verify\urmessage-window.png).Hash -eq (Get-FileHash .verify\baseline-normal-inspect.png).Hash
```

Expected: `False`. Read `.verify\urmessage-window.png`. Expected, all four differences
visible in ONE frame against step 5: the bottom strip now shows **seven** items (dot, state,
host, lock, epoch, session mode, records/s); every conversation row carries a group-id chip;
the rail's message mode adds the hex group id, the leaf index and the wire size; and the nav
pane's footer lists **Developer above Settings**. Nothing anywhere is `#FFC400`.

- [ ] **Step 7: Confirm the rail did NOT crossfade — it changed density in place.**

```powershell
$log = '.localstate-verify\logs\urmessage-app.log'
(Select-String -Path $log -SimpleMatch 'window: advanced mode').Count
(Select-String -Path $log -SimpleMatch 'window: message inspect ->').Count
```

Expected: `1` and `1`. Exactly one `advanced mode` line (the single apply at launch) and
exactly one `message inspect ->` line (the deep link's own selection). A second
`message inspect ->` would mean `ApplyAdvanced` re-showed the rail through
`SetInspectRailMessage` — the crossfade contract v2 §4 forbids — and the fix is to delete
that call, not to suppress the log line.

- [ ] **Step 8: Confirm the launch switch wrote no preference — on disk this time.**

```powershell
$prefs = '.localstate-verify\app_prefs.json'
$before = if (Test-Path $prefs) { Get-Content $prefs -Raw } else { '(absent)' }
$before
```

Run the `--demo-advanced` capture from step 6 again, then:

```powershell
$after = if (Test-Path $prefs) { Get-Content $prefs -Raw } else { '(absent)' }
"before: $before"
"after : $after"
"unchanged: $($before -eq $after)"
```

Expected: `unchanged: True`, with both reading `(absent)` on a clean worktree — the file does
not exist yet because nothing in this plan writes it except a click in Settings.
`.localstate-verify` is the right place to look: `verify-render.ps1:153` redirects
`URMESSAGE_APP_ROOT` there per worktree, so a stale `%LOCALAPPDATA%` file from a direct
`--diagnose` run is not evidence about this launch.

- [ ] **Step 9: Commit.**

```
git add app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m "demo: one Advanced Mode subscriber re-renders list, strip, rail, network and nav"
```

**Deliverable:** two frames from the same binary, differing only by `--demo-advanced`, that
a file hash proves differ; four surfaces visibly changed; exactly one `message inspect ->`
line proving the rail changed density without a mode crossfade; and the prefs file
unchanged.

---

## Task W8: DemoAutoplay: the ambient-activity module, its ladder and cadence asserted in --diagnose

**Files:**

Create app/src/App/Demo/DemoAutoplay.h; Create app/src/App/Demo/DemoAutoplay.cpp; Modify app/src/App/App.vcxproj; Modify app/src/App/Startup.cpp

**Interfaces:**

- Consumes: `urmsg::demo::GetWorld`, `urmsg::demo::MutableWorld`, `World`, `Conversation`, `MessageRow`, `MessageInspect`, `DeliveryState`, `RetentionClass`, `ConversationKind`, `RowKind`, `MemberRef::displayName`, `DeviceRef` (Demo/DemoWorld.h, world surface, contract v2 §1); `urnw::LogInfo` / `LogError` (Common/Log.h:36,44); `urnw::Narrow` (Common/Strings.h); `winrt::Microsoft::UI::Dispatching::DispatcherQueue` / `DispatcherQueueTimer` (pch.h:36)
- Produces: `urmsg::demo::DeliveryState urmsg::demo::AdvanceDelivery(DeliveryState state);` `int64_t urmsg::demo::IdleMsForRound(int round);` `int64_t urmsg::demo::TypingMsForRound(int round);` `urmsg::demo::MessageRow urmsg::demo::IncomingForRound(Conversation const& conversation, int round);` `enum class urmsg::demo::AutoplayPhase { Idle, Typing };` `struct urmsg::demo::AutoplayCallbacks { std::function<int()> openConversationIndex; std::function<void(bool)> setTyping; std::function<void()> onDeliveryAdvanced; std::function<void(MessageRow const&)> onIncoming; };` `struct urmsg::demo::Autoplay;` `std::unique_ptr<urmsg::demo::Autoplay> urmsg::demo::MakeAutoplay(winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue, AutoplayCallbacks callbacks);` `void urmsg::demo::StartAutoplay(Autoplay& loop);`

## Task W8: ambient activity, as a module with an asserted ladder

Design doc §9.2: while the demo is open, a typing indicator occasionally appears in the OPEN
thread and an incoming message arrives; the most recent outgoing message advances ONE
delivery state, up to `read`, then stops. Not a tour. It never changes destination, never
moves the selection, and never opens or closes the rail or the drawer — and the cheapest way
to keep that true is to give the loop no way to do any of them, which is why
`AutoplayCallbacks` is four narrow functions and not a pointer to the window.

**The loop owns the world mutation, and it is the only thing that does.** Contract v2 §1:
`MutableWorld()`'s only caller is ambient activity, which "appends a `MessageRow` and
advances one `DeliveryState`." This module does exactly those two writes and no others — no
preview rewrite, no reordering of `World::conversations`. That is what makes an ambient
message survive a re-selection, a rebuild, or an Advanced Mode toggle: the row is in the
world, not in a view's vector.

The cadence is a TABLE, not a random draw. The demo world is seeded (§5) and two runs of the
same demo must behave the same way.

This task builds and asserts the module. W9 wires it to the window.

- [ ] **Step 1: Create `app/src/App/Demo/DemoAutoplay.h`.**

```cpp
// Ambient activity (design doc 9.2): the slow background loop that makes the
// demo feel alive while the owner is talking over it.
//
// It is NOT a scripted tour. It only ever ADDS to the conversation that is
// already open. It never changes destination, never moves the selection and
// never opens or closes the rail or the drawer - which is why the loop is given
// four narrow callbacks rather than the window: it has no way to do any of those
// things even by mistake.
//
// It is also the ONLY caller of urmsg::demo::MutableWorld (contract v2 section
// 1), and it makes exactly the two writes that comment names: one row appended,
// one DeliveryState advanced.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <winrt/Microsoft.UI.Dispatching.h>

#include "Demo/DemoWorld.h"

namespace urmsg::demo {

// ---- the pure half (asserted in --diagnose) --------------------------------

// One step up Spec C 5.3's delivery ladder, and no further than Read. Failed and
// Expired are TERMINAL: ambient activity must never quietly "fix" a failed send,
// and the demo's whole claim to honesty is that nothing changes state without a
// cause.
DeliveryState AdvanceDelivery(DeliveryState state);

// The loop's cadence, deterministic by round index. ~40s between rounds, 3-6s of
// typing, both from a fixed table so a second run of the demo behaves like the
// first.
int64_t IdleMsForRound(int round);
int64_t TypingMsForRound(int round);

// The next ambient incoming message for `conversation`. Fabricated HERE, in the
// Demo directory, so the diff that deletes the demo is still one directory.
MessageRow IncomingForRound(Conversation const& conversation, int round);

// ---- the timer half --------------------------------------------------------

enum class AutoplayPhase { Idle, Typing };

struct AutoplayCallbacks {
  // Index into World::conversations, or -1 when no thread is open: the round is
  // then skipped WHOLE rather than half-played into nothing.
  std::function<int()> openConversationIndex;
  std::function<void(bool)> setTyping;
  // The world's newest outgoing row moved one step; the thread has to be re-set
  // to draw the new glyph.
  std::function<void()> onDeliveryAdvanced;
  // The row is ALREADY in World::conversations[i].rows. The window appends it to
  // the visible thread; it does not have to store it.
  std::function<void(MessageRow const&)> onIncoming;
};

struct Autoplay {
  Autoplay() = default;
  ~Autoplay();
  Autoplay(Autoplay const&) = delete;
  Autoplay& operator=(Autoplay const&) = delete;

  winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer timer{nullptr};
  AutoplayCallbacks callbacks;
  AutoplayPhase phase = AutoplayPhase::Idle;
  int round = 0;
  // Snackbar's dangling-tick guard (UrComponents.cpp:848-878): the destructor
  // nulls this, so a tick that outlives the loop finds a null rather than a
  // dangling pointer. "The window owns both, so the timer cannot outlive it" is
  // an invariant nothing enforces.
  std::shared_ptr<Autoplay*> self;
};

// Built but NOT started. Off unless --demo-autoplay (design doc 8).
std::unique_ptr<Autoplay> MakeAutoplay(
    winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue,
    AutoplayCallbacks callbacks);
// Stopping is the destructor's job: the loop dies with the unique_ptr member on
// the window, and there is no in-app control that stops it (see the plan's
// remaining concerns). No StopAutoplay is declared, because nothing would call
// one.
void StartAutoplay(Autoplay& loop);

}  // namespace urmsg::demo
```

- [ ] **Step 2: Create `app/src/App/Demo/DemoAutoplay.cpp` — the tables and the pure half.**

```cpp
// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "Demo/DemoAutoplay.h"

#include <array>
#include <chrono>
#include <format>

#include "Log.h"
#include "Strings.h"

namespace urmsg::demo {
namespace {

// Plausible and neutral (design doc 5): no real people, no recognisable handles,
// and nothing that states a message WAS encrypted as a fact about an operation
// that did not happen.
constexpr std::array<const wchar_t*, 4> kIncomingBodies{
    L"Pushed the retention change - take a look when you get a minute.",
    L"That works for me. Shall we say Thursday?",
    L"The Reykjavik node came back up about ten minutes ago.",
    L"Much better. That reads far cleaner than the old copy.",
};

// 3-6s of typing and ~40s between rounds, from fixed tables. A table rather than
// a random draw because the demo world is seeded and this has to be
// reproducible; four entries rather than one so two consecutive rounds do not
// look metronomic.
constexpr std::array<int64_t, 4> kTypingMs{3200, 5400, 4100, 5900};
constexpr std::array<int64_t, 4> kIdleMs{40000, 38500, 41500, 39500};

size_t Slot(int round, size_t size) {
  return static_cast<size_t>(round < 0 ? 0 : round) % size;
}

std::wstring ClockLabel() {
  SYSTEMTIME now{};
  ::GetLocalTime(&now);
  return std::format(L"{:02}:{:02}", now.wHour, now.wMinute);
}

}  // namespace

DeliveryState AdvanceDelivery(DeliveryState state) {
  switch (state) {
    case DeliveryState::Pending:
      return DeliveryState::Sent;
    case DeliveryState::Sent:
      return DeliveryState::Delivered;
    case DeliveryState::Delivered:
      return DeliveryState::Read;
    case DeliveryState::Read:
    case DeliveryState::Failed:
    case DeliveryState::Expired:
      break;
  }
  return state;
}

int64_t IdleMsForRound(int round) { return kIdleMs[Slot(round, kIdleMs.size())]; }
int64_t TypingMsForRound(int round) { return kTypingMs[Slot(round, kTypingMs.size())]; }

MessageRow IncomingForRound(Conversation const& conversation, int round) {
  // Inherit the identity of the last INCOMING row of this conversation, so the
  // ambient message comes from someone the thread has already seen rather than
  // from a stranger with a brand-new identicon.
  MessageRow const* last = nullptr;
  for (auto const& row : conversation.rows)
    if (row.kind == RowKind::Message && !row.outgoing) last = &row;

  MessageRow row{};
  row.kind = RowKind::Message;
  row.id = std::format(L"{}-ambient-{}", conversation.id, round);
  row.body = kIncomingBodies[Slot(round, kIncomingBodies.size())];
  row.timeLabel = ClockLabel();
  row.outgoing = false;
  // An incoming message: `state` is OUR read state of it, and we are reading it
  // right now.
  row.state = DeliveryState::Read;
  row.permanentRecord = false;
  row.senderKey = last ? last->senderKey : conversation.identityKey;
  // senderName is empty in DMs by contract; groups name the sender.
  if (conversation.kind == ConversationKind::Group) {
    row.senderName = (last && !last->senderName.empty())
                         ? last->senderName
                         : (conversation.members.empty()
                                ? std::wstring{}
                                : conversation.members.front().displayName);
  }

  auto const& world = GetWorld();
  auto& inspect = row.inspect;
  inspect.epoch = world.currentEpoch;
  inspect.senderLeafIndex = last ? last->inspect.senderLeafIndex : 1u;
  inspect.retention =
      conversation.disappearing ? RetentionClass::Eph : RetentionClass::Permanent;
  inspect.sizeBucket = L"<= 1 KiB";
  inspect.wireSizeBytes = static_cast<uint32_t>(row.body.size() * 2 + 96);
  inspect.attestationVerified = true;
  inspect.cipher = L"XChaCha20-Poly1305";
  inspect.groupIdHex = conversation.groupIdHex;
  // ALWAYS populated, even where senderName is deliberately empty (contract v2
  // section 1): the rail's "from" field must never be blank.
  inspect.senderDisplayName =
      row.senderName.empty() ? conversation.name : row.senderName;
  inspect.sentAtLabel = row.timeLabel;
  inspect.receivedAtLabel = row.timeLabel;
  // Delivered to every device of ours; read by the ones that are online, since
  // this one is on screen.
  inspect.deliveredTo = world.myDevices;
  inspect.readBy.clear();
  for (auto const& device : world.myDevices)
    if (device.online) inspect.readBy.push_back(device);
  return row;
}

}  // namespace urmsg::demo
```

- [ ] **Step 3: Append the timer half to `Demo/DemoAutoplay.cpp`.**

Insert immediately before the closing `}  // namespace urmsg::demo`:

```cpp
namespace {

void Schedule(Autoplay& loop, int64_t ms);

// One of the two writes contract v2 licenses ambient activity to make. Returns
// whether anything actually moved.
bool AdvanceNewestOutgoing(int index) {
  auto& rows = MutableWorld().conversations[static_cast<size_t>(index)].rows;
  for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
    if (it->kind != RowKind::Message || !it->outgoing) continue;
    const DeliveryState next = AdvanceDelivery(it->state);
    // Already Read, or Failed, or Expired: the ladder stops and the loop leaves
    // it alone from here on.
    if (next == it->state) return false;
    it->state = next;
    urnw::LogInfo("demo: ambient delivery {} -> state {}", urnw::Narrow(it->id),
                  static_cast<int>(next));
    return true;
  }
  return false;
}

void Tick(Autoplay& loop) {
  const int index =
      loop.callbacks.openConversationIndex ? loop.callbacks.openConversationIndex() : -1;

  if (loop.phase == AutoplayPhase::Idle) {
    // No open thread means nowhere for a typing indicator to appear, so the round
    // is skipped WHOLE rather than half-played into nothing.
    if (index < 0) {
      Schedule(loop, IdleMsForRound(loop.round));
      return;
    }
    // The delivery step happens at the TOP of the round, before the typing
    // indicator, because the composer's only way to redraw a changed delivery
    // glyph is to re-set the whole thread (contract v2's ThreadView has no
    // per-row delivery setter). Doing it here means that re-set lands while
    // nothing else on screen is moving, rather than on top of the incoming
    // message's entrance.
    if (AdvanceNewestOutgoing(index) && loop.callbacks.onDeliveryAdvanced)
      loop.callbacks.onDeliveryAdvanced();
    if (loop.callbacks.setTyping) loop.callbacks.setTyping(true);
    loop.phase = AutoplayPhase::Typing;
    Schedule(loop, TypingMsForRound(loop.round));
    return;
  }

  // Typing -> the message lands.
  if (loop.callbacks.setTyping) loop.callbacks.setTyping(false);
  if (0 <= index) {
    Conversation& conversation =
        MutableWorld().conversations[static_cast<size_t>(index)];
    const MessageRow row = IncomingForRound(conversation, loop.round);
    // Into the WORLD first. The window then appends it to the visible thread; it
    // never has to remember it, and nothing that rebuilds a view from the world
    // can lose it.
    conversation.rows.push_back(row);
    urnw::LogInfo("demo: ambient message {} in {}", urnw::Narrow(row.id),
                  urnw::Narrow(conversation.id));
    if (loop.callbacks.onIncoming) loop.callbacks.onIncoming(row);
  }

  loop.phase = AutoplayPhase::Idle;
  ++loop.round;
  Schedule(loop, IdleMsForRound(loop.round));
}

void Schedule(Autoplay& loop, int64_t ms) {
  if (!loop.timer) return;
  try {
    loop.timer.Stop();
    loop.timer.Interval(std::chrono::milliseconds(ms));
    loop.timer.Start();
  } catch (winrt::hresult_error const&) {
    // DispatcherQueueTimer.Start() throws ERROR_INVALID_OPERATION (0x800710dd)
    // once the queue has begun shutting down - the same teardown race
    // Snackbar::Show documents at UrComponents.cpp:885-890. An ambient message
    // nobody can see any more is safe to drop.
  }
}

}  // namespace

Autoplay::~Autoplay() {
  // A throw out of a destructor is std::terminate, and nothing here needs to
  // succeed - the timer dies with the queue either way.
  try {
    if (timer) timer.Stop();
  } catch (winrt::hresult_error const&) {
  }
  if (self) *self = nullptr;
}

std::unique_ptr<Autoplay> MakeAutoplay(
    winrt::Microsoft::UI::Dispatching::DispatcherQueue const& queue,
    AutoplayCallbacks callbacks) {
  auto loop = std::make_unique<Autoplay>();
  loop->callbacks = std::move(callbacks);
  loop->self = std::make_shared<Autoplay*>(loop.get());
  if (!queue) {
    urnw::LogError(
        "demo: autoplay built without a dispatcher queue - ambient activity is off");
    return loop;
  }
  loop->timer = queue.CreateTimer();
  loop->timer.IsRepeating(false);
  loop->timer.Tick([self = loop->self](auto const&, auto const&) {
    if (auto* live = *self) Tick(*live);
  });
  return loop;
}

void StartAutoplay(Autoplay& loop) {
  loop.phase = AutoplayPhase::Idle;
  loop.round = 0;
  urnw::LogInfo("demo: ambient activity on - first round in {} ms",
                IdleMsForRound(loop.round));
  Schedule(loop, IdleMsForRound(loop.round));
}
```

- [ ] **Step 4: Register both files in `app/src/App/App.vcxproj`.**

After `<ClCompile Include="Demo\AdvancedMode.cpp" />` add:

```xml
    <ClCompile Include="Demo\DemoAutoplay.cpp" />
```

After `<ClInclude Include="Demo\AdvancedMode.h" />` add:

```xml
    <ClInclude Include="Demo\DemoAutoplay.h" />
```

- [ ] **Step 5: Add the `--diagnose` assertion to `app/src/App/Startup.cpp`.**

Add to the include block:

```cpp
#include "Demo/DemoAutoplay.h"
```

Add to the anonymous namespace, beside the other demo checks:

```cpp
std::wstring DemoAutoplayCheck() {
  using namespace urmsg::demo;
  const bool ladder = AdvanceDelivery(DeliveryState::Pending) == DeliveryState::Sent &&
                      AdvanceDelivery(DeliveryState::Sent) == DeliveryState::Delivered &&
                      AdvanceDelivery(DeliveryState::Delivered) == DeliveryState::Read &&
                      AdvanceDelivery(DeliveryState::Read) == DeliveryState::Read &&
                      AdvanceDelivery(DeliveryState::Failed) == DeliveryState::Failed &&
                      AdvanceDelivery(DeliveryState::Expired) == DeliveryState::Expired;
  int rounds = 0;
  bool cadence = true;
  for (int round = 0; round < 8; ++round) {
    ++rounds;
    const int64_t typing = TypingMsForRound(round);
    const int64_t idle = IdleMsForRound(round);
    if (typing < 3000 || 6000 < typing) cadence = false;
    if (idle < 38000 || 42000 < idle) cadence = false;
  }
  // Deterministic, not random: the demo world is seeded and a second run of the
  // same demo has to behave like the first.
  const bool stable = IdleMsForRound(0) == IdleMsForRound(4) &&
                      TypingMsForRound(1) == TypingMsForRound(5) &&
                      IdleMsForRound(0) != IdleMsForRound(1);
  const bool ok = ladder && cadence && stable;
  return std::format(
      L"  demo autoplay    : {}  (ladder Pending->Sent->Delivered->Read->Read, "
      L"Failed/Expired terminal {} | {} rounds each with typing in 3000-6000ms and "
      L"idle in 38000-42000ms {} | period 4 and not constant {})",
      ok ? L"PASS" : L"FAIL", ladder, rounds, cadence, stable);
}
```

and push it in `CollectDiagnostics()` beside the other three:

```cpp
  lines.push_back(DemoAutoplayCheck());
```

- [ ] **Step 6: Build and run the assertion.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

```powershell
$repo = (Resolve-Path .).Path
$env:URMESSAGE_APP_ROOT = "$repo\.localstate-verify"
Start-Process -FilePath "$repo\app\build\x64\Release\URmessage.exe" -ArgumentList '--diagnose' -Wait -RedirectStandardOutput "$repo\.verify\diagnose.txt" -RedirectStandardError "$repo\.verify\diagnose.err.txt"
Get-Content "$repo\.verify\diagnose.txt" | Select-String 'demo '
```

Expected, verbatim, alongside the three lines from W1 and W2:

```
  demo autoplay    : PASS  (ladder Pending->Sent->Delivered->Read->Read, Failed/Expired terminal true | 8 rounds each with typing in 3000-6000ms and idle in 38000-42000ms true | period 4 and not constant true)
```

- [ ] **Step 7: Prove the assertion can fail.**

Temporarily change `kTypingMs`'s first entry from `3200` to `9000`, rebuild, and re-run the
block from step 6. Expected: the line now reads `FAIL` with `... true | 8 rounds ... false |
... true`, naming the cadence clause specifically. Change it back, rebuild, and confirm
`PASS` again. A gate that has never been seen red is not a gate.

- [ ] **Step 8: Commit.**

```
git add app/src/App/Demo/DemoAutoplay.h app/src/App/Demo/DemoAutoplay.cpp app/src/App/App.vcxproj app/src/App/Startup.cpp
git commit -m "demo: ambient-activity module; delivery ladder and cadence asserted in --diagnose"
```

**Deliverable:** `--diagnose` prints a fourth PASS line whose text names three separate
clauses, and that line has been seen to go red on one clause and back.

---

## Task W9: Wire ambient activity to the window, behind --demo-autoplay

**Files:**

Modify app/src/App/MainWindow.xaml.h; Modify app/src/App/MainWindow.xaml.cpp

**Interfaces:**

- Consumes: `urmsg::demo::MakeAutoplay`, `StartAutoplay`, `AutoplayCallbacks`, `Autoplay` (W8); `urmsg::views::SetThreadTyping`, `AppendThreadRow`, `SetThreadConversation`, `SetThreadSelectedMessage` (Views/ThreadView.h, contract v2 §4); `MainWindow::OpenConversationIndex`, `thread_`, `selectedMessageId_` (W5); `winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread()` (verified present in the generated projection, Microsoft.UI.Dispatching.h:554)
- Produces: `void MainWindow::StartAmbientActivity();` `void MainWindow::RefreshOpenThread();` member `std::unique_ptr<urmsg::demo::Autoplay> MainWindow::autoplay_`

## Task W9: give the loop its four callbacks, and prove it moves nothing else

The module exists and its ladder is asserted. This task hands it four narrow callbacks and
starts it only under `--demo-autoplay`.

Design doc §9.2's three constraints are enforced by what the callbacks CAN reach, not by a
comment: nothing on this path calls `SelectNavTag`, `SelectConversation`, `SelectMessage`,
`ClearMessageSelection` or `ToggleStatusDrawer`. Step 10 turns that into log evidence scoped
to a single run.

The dispatcher queue comes from
`winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread()`, not from
`Window::DispatcherQueue()`. Both are real, but the static is the one this repo's projection
definitely carries (`Microsoft.UI.Dispatching.h:554`) and `EnterDemoMode` runs on the UI
thread, which is the thread that owns the queue.

- [ ] **Step 1: Declare the members and methods in `app/src/App/MainWindow.xaml.h`.**

Add to the include block:

```cpp
#include <memory>

#include "Demo/DemoAutoplay.h"
```

Add to `private:`:

```cpp
  // Ambient activity (design doc 9.2). Off unless --demo-autoplay. None of these
  // may change destination, move the selection, or open or close the rail or the
  // drawer - and none of them can, because none of them calls anything that
  // does.
  void StartAmbientActivity();
  // Re-sets the open thread from the world. Called ONLY when the loop has moved
  // a delivery state, because contract v2's ThreadView has no per-row delivery
  // setter and this is the one API that redraws the glyph.
  void RefreshOpenThread();

  std::unique_ptr<urmsg::demo::Autoplay> autoplay_;
```

- [ ] **Step 2: Add `RefreshOpenThread()` to `app/src/App/MainWindow.xaml.cpp`.**

```cpp
void MainWindow::RefreshOpenThread() {
  const int index = OpenConversationIndex();
  if (index < 0) return;
  auto const& conversation =
      urmsg::demo::GetWorld().conversations[static_cast<size_t>(index)];
  urmsg::views::SetThreadConversation(thread_, conversation);
  // SetThreadConversation rebuilds the bubbles, so the selection outline has to
  // be put back. Restoring a selection is the opposite of moving one: after this
  // returns, the same message id is selected that was selected before.
  if (!selectedMessageId_.empty())
    urmsg::views::SetThreadSelectedMessage(thread_, selectedMessageId_);
}
```

- [ ] **Step 3: Add `StartAmbientActivity()` to `MainWindow.xaml.cpp`.**

```cpp
void MainWindow::StartAmbientActivity() {
  if (!options_.enabled || !options_.autoplay) return;

  urmsg::demo::AutoplayCallbacks callbacks;
  callbacks.openConversationIndex = [weak = get_weak()]() -> int {
    auto self = weak.get();
    return self ? self->OpenConversationIndex() : -1;
  };
  callbacks.setTyping = [weak = get_weak()](bool on) {
    if (auto self = weak.get()) urmsg::views::SetThreadTyping(self->thread_, on);
  };
  callbacks.onDeliveryAdvanced = [weak = get_weak()]() {
    if (auto self = weak.get()) self->RefreshOpenThread();
  };
  callbacks.onIncoming = [weak = get_weak()](urmsg::demo::MessageRow const& row) {
    // AppendThreadRow, not SetThreadConversation: the row springs in (design doc
    // 7's bubble entrance) instead of the whole thread being redrawn under it.
    // The row is already in the world, so any later rebuild still has it.
    if (auto self = weak.get()) urmsg::views::AppendThreadRow(self->thread_, row);
  };

  autoplay_ = urmsg::demo::MakeAutoplay(
      winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread(),
      std::move(callbacks));
  urmsg::demo::StartAutoplay(*autoplay_);
}
```

- [ ] **Step 4: Start the loop last, from `DrainDeepLink()` (same file).**

As the LAST statement of `MainWindow::DrainDeepLink()`, after its `urnw::LogInfo("window:
demo deep link -> ...")` call, add:

```cpp
  // Started here rather than in EnterDemoMode: the loop's first act is to ask
  // which conversation is open, and the deep link is what opens one. Starting it
  // in the constructor would have every round before the first click skip whole.
  StartAmbientActivity();
```

- [ ] **Step 5: Build.**

```
powershell -ExecutionPolicy Bypass -File app/tools/build-local.ps1
```

Expected: `Build succeeded`, 0 errors.

- [ ] **Step 6: Confirm the loop is OFF by default.**

```powershell
Remove-Item .localstate-verify\logs\urmessage-app.log -ErrorAction SilentlyContinue
```

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread"
```

```powershell
$log = '.localstate-verify\logs\urmessage-app.log'
(Select-String -Path $log -SimpleMatch 'demo: ambient').Count
```

Expected: `0`. `--demo-autoplay` was not passed, so nothing was even constructed. Read
`.verify\urmessage-window.png` and confirm there is no typing indicator in the thread.

- [ ] **Step 7: Establish whether the PrintWindow capture is byte-stable on this machine.**

This is the control for step 9's comparison. Run the same command twice with no code change
in between:

```powershell
Copy-Item .verify\urmessage-window.png .verify\baseline-autoplay-off.png -Force
```

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread"
```

```powershell
(Get-FileHash .verify\urmessage-window.png).Hash -eq (Get-FileHash .verify\baseline-autoplay-off.png).Hash
```

If this prints `True`, the hash is a valid gate and step 9 uses it. If it prints `False`,
`PrintWindow` is not byte-stable here (a caret phase or a hover state differs between runs)
and step 9 uses the two counted claims instead. **Record which one you got**; do not use the
hash without having seen this control pass.

- [ ] **Step 8: Turn it on and watch one full round.**

```powershell
Remove-Item .localstate-verify\logs\urmessage-app.log -ErrorAction SilentlyContinue
```

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread --demo-autoplay"
```

The script leaves the app running. The first round is `IdleMsForRound(0)` = 40000 ms plus
`TypingMsForRound(0)` = 3200 ms, so:

```powershell
Start-Sleep -Seconds 55
$log = '.localstate-verify\logs\urmessage-app.log'
Select-String -Path $log -SimpleMatch 'demo: ambient'
```

Expected, in this order and no more:

```
demo: ambient activity on - first round in 40000 ms
demo: ambient delivery <row id> -> state <n>
demo: ambient message <conversation id>-ambient-0 in <conversation id>
```

Exactly ONE `ambient message` line — the loop adds one message per round. A second one at
55 s means the idle table is not being honoured.

- [ ] **Step 9: Confirm autoplay changes nothing at launch.**

The capture in step 8 was taken ~1.2 s after launch (`$SettleMs`), well inside the first idle
window, so it must look exactly like step 6's.

If step 7's control printed `True`:

```powershell
(Get-FileHash .verify\urmessage-window.png).Hash -eq (Get-FileHash .verify\baseline-autoplay-off.png).Hash
```

Expected: `True`.

If step 7's control printed `False`, use the two counted claims instead — read
`.verify\urmessage-window.png` and confirm (a) there is **no** typing indicator anywhere in
the thread, and (b) the bubble count in the thread is the same as in
`.verify\baseline-autoplay-off.png`. Do not write "identical" in either case; write down
which check you ran.

- [ ] **Step 10: Confirm the loop never moved anything it must not — scoped to this one run.**

The log was deleted before step 8, so `demo: ambient activity on` occurs exactly once and
can anchor the window.

```powershell
$log = '.localstate-verify\logs\urmessage-app.log'
$start = (Select-String -Path $log -SimpleMatch 'demo: ambient activity on').LineNumber
$after = Get-Content $log | Select-Object -Skip $start
"destination : $(($after | Select-String -SimpleMatch 'window: destination ->').Count)"
"conversation: $(($after | Select-String -SimpleMatch 'window: conversation ->').Count)"
"inspect     : $(($after | Select-String -SimpleMatch 'window: message inspect').Count)"
"drawer      : $(($after | Select-String -SimpleMatch 'window: status drawer').Count)"
"messages    : $(($after | Select-String -SimpleMatch 'demo: ambient message').Count)"
```

Expected, exactly:

```
destination : 0
conversation: 0
inspect     : 0
drawer      : 0
messages    : 1
```

The first four zeros ARE design doc §9.2's three hard constraints — never changes
destination, never moves the selection, never opens or closes the rail or the drawer — as
evidence rather than as a claim. `-SimpleMatch` is required: `->` is a regex otherwise, and
`findstr` would OR the words apart.

- [ ] **Step 11: Confirm the ambient row survives a rebuild.**

The world owns the appended row, so re-entering the conversation must not lose it. With the
app from step 8 still running and one ambient message delivered, resize it below the rail
breakpoint and back — `verify-render.ps1` already does the down-resize at the end of its run
— then re-run the harness once more and check the thread was rebuilt WITH the ambient row:

```
powershell -ExecutionPolicy Bypass -File app/tools/verify-render.ps1 -AppArgs "--demo=thread"
```

Expected: this is a fresh process, so the world is re-seeded and the ambient row is gone —
which is correct and is the point of a seeded world. The property to check is the in-process
one, from step 8's log: after `demo: ambient message ...`, a `window: layout` line appears
(the harness resize) and **no** error or warning line follows it. The ambient row lives in
`World::conversations[i].rows`, so any later `SetThreadConversation` redraws it rather than
dropping it.

- [ ] **Step 12: Commit.**

```
git add app/src/App/MainWindow.xaml.h app/src/App/MainWindow.xaml.cpp
git commit -m "demo: ambient activity wired to the open thread, off unless --demo-autoplay"
```

**Deliverable:** with `--demo-autoplay` the log shows exactly one ambient message and one
delivery step per round and **zero** destination, selection or drawer lines after the loop
started; without it the loop is never constructed and `demo: ambient` appears zero times.
