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
#include "RunMode.h"  // urmsg::RunMode — FormatKeyState answers a different question per mode

namespace urmsg::views {

// THE RELAY-PATH FRAMING HEADER MOVED to urmsg::RelayPathGroupTitle (RunMode.h)
// when it became a PAIR, and the constant that used to live here is gone rather
// than kept beside it — one owner, or the two drift. It is still the page's
// single always-on honesty frame (d5 §3.4), still prefix-first in the
// letterspaced chrome voice, still rendered by the page and asserted by
// `net framing` through the SAME function so it cannot be dropped silently
// (d5 §6). What changed is that "DEMO MODEL: RELAY PATH" is the FABRICATED arm
// only: in live mode those three nodes are observations — the platform url the
// library dialled and the message server's own client_id — so framing them as a
// demo model denies the run a property it has, which the honesty rule forbids in
// the same breath as it forbids the opposite.

// 18 -> "18 ms". Also formats a RelayNode::hopMs for the Advanced wire labels.
std::wstring FormatLatency(int latencyMs);

// Fabricated: true -> "Demo model: verified", false -> "Demo model: not verified".
// Live:       the ONE placeholder (demo::kUnavailable), for BOTH.
//
// PREFIX-FIRST in the fabricated arm, matching AttestationLabel
// (InspectRailFields.h) character for character in shape, and for the reason
// recorded there: the bare words state that a check RAN and returned a result,
// and this binary runs no check. Leading with the claim loses the framing to
// exactly the crop a screenshot performs, and --demo-watermark=off removes the
// chip a suffix would lean on.
//
// THE LIVE ARM IS NOT A PREFIX SWAP, and that is the half worth reading twice.
// "Live session: not verified" would still assert a negative RESULT — a check
// that ran and failed — and no check runs: this build pins no server key, which
// is why the live world hard-codes keyVerified false (Live/LiveWorld.cpp:350-352).
// An absence is not a negative, so the live value is the same placeholder the
// rail renders for every field no source can fill.
//
// This function is the ONE owner of the string: N4's Server key row renders it
// and A3's settings row calls it (the d7 audit's N2/N4/A3 overrides); the bare
// words ship in no label, no automation name, no gate.
std::wstring FormatKeyState(bool keyVerified, urmsg::RunMode mode);

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
