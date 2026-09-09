// The Network destination (design §6.4): the relay path, the message server and
// this account's linked devices.
//
// The Format* functions below are PURE — no XAML, no WinRT, no localization
// lookup. Localized() caches its ResourceLoader on the FIRST call and
// --diagnose deliberately makes ResourceProbe() that first caller
// (Startup.h:54-61), so a formatter that reached the store from the diagnose
// path could make the entire UI render key ids. Keeping them pure means that
// ordering never has to be remembered again.
//
// They format values DemoWorld already carries as English literals ("2 min
// ago", "Iceland"), so they are consistent with the world they read.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>

#include "Demo/DemoWorld.h"

namespace urmsg::views {

// The framing header over the relay path (d5 §3.4): prefix-first honesty in
// the letterspaced chrome voice, always visible in both modes — the page's
// single always-on frame, the analogue of the rail's "Demo model: end-to-end
// encrypted" header (InspectRailView.cpp:410). It cannot be cropped away from
// the diagram it frames because it sits directly on top of it. The page
// renders THIS constant and `net framing` asserts the same constant, so the
// framing cannot be dropped silently (d5 §6).
inline constexpr wchar_t kRelayPathGroupTitle[] = L"DEMO MODEL: RELAY PATH";

// 18 -> "18 ms". Also formats a RelayNode::hopMs for the Advanced wire labels.
std::wstring FormatLatency(int latencyMs);

// true -> "Demo model: verified", false -> "Demo model: not verified".
//
// PREFIX-FIRST, matching AttestationLabel (InspectRailFields.cpp:113)
// character for character in shape, and for the reason recorded there: the
// bare words state that a check RAN and returned a result, and this binary
// runs no check. Leading with the claim loses the framing to exactly the crop
// a screenshot performs, and --demo-watermark=off removes the chip a suffix
// would lean on. This function is the ONE owner of the string: N4's Server
// key row renders it and A3's settings row calls it (the d7 audit's N2/N4/A3
// overrides); the bare words ship in no label, no automation name, no gate.
std::wstring FormatKeyState(bool keyVerified);

// isThisComputer -> "This computer · Online"
// online         -> "Online"
// otherwise      -> "Last seen 3 days ago"
//
// DeviceRef::ownerName is deliberately NOT used: these are the account's OWN
// devices, so repeating the account holder's name on all three says nothing.
std::wstring FormatDeviceMeta(urmsg::demo::DeviceRef const& device);

// 59 -> "59 ms round trip". Advanced Mode only (design §6.6). It is the
// END-TO-END figure: ServerInfo carries one latencyMs and no per-hop split, so
// this must never be presented as a per-hop timing. The per-hop figures come
// from RelayNode::hopMs and ride the wires (hopMs is the timing of the hop
// TERMINATING at that node; node 0's 0 ms is "no hop to yourself" and renders
// nothing — the d7 audit's N2 override).
std::wstring FormatRoundTrip(urmsg::demo::ServerInfo const& server);

// The --diagnose lines for this surface. Pure C++ for the reason above.
// main.cpp pushes these inside its `if (diagnose)` block — NOT into
// CollectDiagnostics(), which runs on every launch (main.cpp:176).
std::vector<std::wstring> CollectNetworkDiagnostics();

// ---- the page (fixed contract §4) ------------------------------------------
// `root` ONLY, verbatim from the contract. The page's elements are private to
// NetworkPageView.cpp, which keeps them in a registry keyed by this root — see
// the PageParts block there. That is why SetNetworkPageAdvanced looks its
// parts up rather than reading them off this struct.
struct NetworkPageView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
};
NetworkPageView MakeNetworkPage(urmsg::demo::World const& world);

// DENSITY ONLY (fixed contract §4). It shows and hides lines that are already
// built and starts or stops the packet travel. It never rebuilds the page and
// never runs a mode crossfade — a Set*Advanced that faded would blank a
// visible surface to opacity 0 and fade it back over itself.
void SetNetworkPageAdvanced(NetworkPageView& v, bool advanced);

// The first-show entrance (d5 §3.7): the three top-level sections — path
// panel, server group, devices group — rise kDist8 and fade in, staggered by
// kStaggerMs over 3 steps (within kMaxStaggerSteps), each kBaseMs on the
// standard bezier. It runs ONCE per page: re-navigation must not re-run it.
// It composes with the shell's CrossfadePageSwap rather than fighting it —
// the crossfade owns the page root's opacity, this owns the sections' rise
// and fade, and the two never animate the same property on the same element.
// With ShouldAnimate() false the page is simply there, fully formed.
// MainWindow::ShowDestination calls it when the network destination becomes
// visible — the same view-owns/window-calls split
// AnimateConversationListEntrance and StartReveal already use.
void AnimateNetworkPageEntrance(NetworkPageView& v);

}  // namespace urmsg::views
