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

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Demo/DemoWorld.h"
#include "UrComponents.h"

namespace urmsg::views {

struct InspectRailView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer conversationScroll{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::ScrollViewer messageScroll{nullptr};
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
// crossfade: the rail is already on screen, and blanking it to fade it back in
// reads as a mode swap that did not happen. Design 7 assigns kBaseMs to the rail
// MODE swap, not to a density change.
void SetInspectRailAdvanced(InspectRailView& v, bool advanced);

// One person row and one device row. Public because the Network page's "Your
// devices" list (design 6.4) is the same device row and must not become a second
// species of it. Both are urnw::kit::MakePaneListRow(36), so the rail's three
// lists share one height, one left edge and one rhythm.
//
// Both carry a 20px identicon of the identity the row is about (design d1 §7):
// the presence dot is re-parented onto the chip as its corner badge, INSIDE the
// same kit grid, so the one-row-species claim above survives — same builder,
// same height, same columns. What this deliberately is NOT is a new
// MakePaneListRow variant in UrComponents.h: the variant would be the second
// species.
urnw::kit::PaneListRow MakeMemberRow(demo::MemberRef const& member);
urnw::kit::PaneListRow MakeDeviceRow(demo::DeviceRef const& device);

}  // namespace urmsg::views
