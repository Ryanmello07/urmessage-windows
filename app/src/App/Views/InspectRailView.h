// The inspector rail (design 6.3, D3): one 360 DIP right column with two modes.
//
// Conversation mode is the default; message mode replaces the BODY when a bubble
// is selected, by crossfade at kBaseMs. The pane header does not change with the
// mode - a pane header names the COLUMN ("Conversations", "Thread"), not its
// content - so a mode swap is one animation and not two.
//
// THE COLUMN IS NOT DECLARED HERE, AND NEITHER ARE ITS NUMBERS. The window shell
// already owns all four: MainWindow.xaml declares RailRule (Grid.Column=3) and
// RailHost (Grid.Column=4), Demo/DemoShellState.h holds kRailWidthDip (360) and
// kRailBreakpointDip (1500), and MainWindow::ApplyBreakpoint is the ONE writer of
// RailColumn().Width(), RailRule().Visibility() and RailHost().Visibility(). This
// file is the rail's CONTENT and nothing else. A second host in Grid.Column=4
// would be a grid the shell never shows while this module filled it - the way
// this project has already shipped four features that painted zero pixels - and a
// second copy of 360 or 1500 is two constants with one value, which drift.
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

#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Demo/DemoWorld.h"
#include "UrComponents.h"

namespace urmsg::views {

struct InspectRailView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer conversationScroll{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer messageScroll{nullptr};
  // The pane header's right-aligned density word ("ADVANCED"), parked so
  // SetInspectRailAdvanced can flip it instantly - chrome appearing in step
  // with a populate-only re-render, no storyboard (design d4 §13).
  winrt::Microsoft::UI::Xaml::Controls::TextBlock headerMeta{nullptr};
  // The members whose device sub-rows are open (design d4 §7.2). IDS, not
  // pointers - the same rule the subject tags follow. Cleared by
  // SetInspectRailConversation (a new subject gets fresh state), preserved
  // across a SetInspectRailAdvanced re-population (density must not silently
  // collapse what a person opened).
  std::vector<std::wstring> expandedMemberIds{};
};

// Both bodies start COLLAPSED, so whichever Set* runs first presents its mode
// with no outgoing element and therefore no storyboard. The window's own
// WindowReveal already covers the entrance; a rail that also faded itself in
// would be a second animation over the same tree.
//
// The returned root is mounted into MainWindow's RailHost. It does NOT size
// itself: the 360 comes from RailColumn, which ApplyBreakpoint sets from
// urmsg::demo::kRailWidthDip.
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
// crossfade, no settle and no cascade: the rail is already on screen, and
// blanking it to fade it back in reads as a mode swap that did not happen
// (design d4 §12.3 - R4's populate-only contract is deliberately kept). The
// only visible moves are the appended ADVANCED caption+card and the header
// meta's instant visibility flip. Called once at startup by BuildInspectRail
// to seed the density; the LIVE subscription that re-calls it on every toggle
// belongs to the wiring task (wiring.md:578), not here.
void SetInspectRailAdvanced(InspectRailView& v, bool advanced);

// The device row. `showOwner` keeps the ` · owner` suffix (message mode's
// delivered-by / read-by lists, where the devices belong to DIFFERENT people
// and the suffix is the load-bearing content); a member's own expanded
// sub-rows pass false, where the suffix would repeat the row above.
//
// The row carries a 20px mini-identicon of the identity it is about (design
// d1 §7) with the presence dot badged on its corner. `identiconSeed` is the
// seed to draw, RESOLVED BY THE CALLER: the owning member's identityKey when
// the device belongs to a member (DeviceIdenticonSeed - polish B2, so the
// device row and the member's avatar are one face), else DeviceRef::ownerKey.
// The MEMBER row this used to sit beside is gone: members are now
// kit::MakePanePresenceRow buttons with expandable device sub-rows (design
// d4 §7) - the kit variant the old comment said would be the right move if
// member identicons were wanted.
urnw::kit::PaneListRow MakeDeviceRow(demo::DeviceRef const& device, bool showOwner,
                                     demo::Seed const& identiconSeed);

}  // namespace urmsg::views
