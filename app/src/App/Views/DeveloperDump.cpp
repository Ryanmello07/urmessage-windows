// SPDX-License-Identifier: MPL-2.0
// No "pch.h" here on purpose: App.vcxproj marks this unit
// <PrecompiledHeader>NotUsing</PrecompiledHeader>, the same as
// Demo/DemoWorld.cpp and Views/StatusStripRules.cpp. Startup.cpp's
// CollectDiagnostics() calls WorldDumpDiagnostics() from wWinMain BEFORE
// winrt::init_apartment(), so "pure C++, no winrt" has to be a property the
// compiler enforces rather than a comment at the top of a file.
#include "Views/DeveloperDump.h"

#include <cstdint>
#include <format>
#include <string_view>

#include "Demo/DemoWorld.h"

namespace urmsg::views {
namespace {

std::wstring_view KindName(demo::ConversationKind kind) {
  return kind == demo::ConversationKind::Group ? L"group" : L"direct";
}

// The row-line PREFIXES. Three literals, each three letters, so a count of
// them cannot be confused with a continuation line — see
// WorldDumpDiagnostics.
std::wstring_view RowTag(demo::RowKind kind) {
  switch (kind) {
    case demo::RowKind::Message: return L"msg";
    case demo::RowKind::DaySeparator: return L"day";
    case demo::RowKind::System: return L"sys";
  }
  return L"???";
}

std::wstring_view StateName(demo::DeliveryState state) {
  switch (state) {
    case demo::DeliveryState::Pending: return L"pending";
    case demo::DeliveryState::Sent: return L"sent";
    case demo::DeliveryState::Delivered: return L"delivered";
    case demo::DeliveryState::Read: return L"read";
    case demo::DeliveryState::Failed: return L"failed";
    case demo::DeliveryState::Expired: return L"expired";
  }
  return L"?";
}

std::wstring_view RetentionName(demo::RetentionClass retention) {
  return retention == demo::RetentionClass::Eph ? L"eph" : L"permanent";
}

std::wstring_view ConnectName(demo::ConnectState state) {
  switch (state) {
    case demo::ConnectState::Connected: return L"connected";
    case demo::ConnectState::Connecting: return L"connecting";
    case demo::ConnectState::Offline: return L"offline";
  }
  return L"?";
}

// 8 of the 32 bytes. Enough to tell two identicon seeds apart by eye, short
// enough that a conv line still fits a terminal.
std::wstring HexPrefix(demo::Seed const& seed) {
  std::wstring out;
  for (size_t i = 0; i < 8 && i < seed.size(); ++i)
    out += std::format(L"{:02x}", static_cast<unsigned>(seed[i]));
  return out;
}

// The relay glyphs are Segoe Fluent PRIVATE-USE codepoints. Printed as U+XXXX
// rather than as the character: a PUA glyph renders as a box everywhere this
// dump is pasted, and the codepoint is the fact worth checking. EMPTY is
// printed loudly because contract §1 invariant 7 forbids it.
std::wstring GlyphCode(std::wstring const& glyph) {
  if (glyph.empty()) return L"EMPTY";
  return std::format(L"U+{:04X}", static_cast<uint32_t>(glyph[0]));
}

// The two claim-carrying tokens, framed in token form (no spaces, so the
// key=value shape survives). The bare words would state that a check RAN and
// returned a result; this binary runs no check (G4), and the framing must
// live IN the token because the dump rides the Copy button onto the
// clipboard, where no page header can follow it. This is the same decision
// FormatKeyState makes for the product rows in its prefix-first shape
// (NetworkPageView.cpp's "Demo model: verified").
std::wstring_view KeyToken(bool verified) {
  return verified ? L"demo-model-verified" : L"demo-model-not-verified";
}

}  // namespace

std::wstring DumpDemoWorld() {
  auto const& world = demo::GetWorld();
  std::wstring out;

  // The framing is the FIRST LINE OF THE TEXT ITSELF, not a group header on
  // the page: the text is what the Copy button moves, and the framing has to
  // travel with it (the d7 audit's A6 framing override). The exact string the
  // rail's lock header already ships (InspectRailView.cpp:516), reused rather
  // than paraphrased so the demo carries one wording of this sentence.
  // WorldDumpDiagnostics asserts this line, so it cannot be dropped silently.
  out += L"Demo model: fabricated data, no crypto in this build\n";

  out += std::format(L"world  epoch={} conversations={} devices={} relay={} state={} "
                     L"session={} records/s={}\n",
                     world.currentEpoch, world.conversations.size(),
                     world.myDevices.size(), world.relayPath.size(),
                     ConnectName(world.connectState), world.sessionMode,
                     world.recordsPerSecond);
  out += std::format(L"server {}  {}  {} ms  key={}\n", world.server.host,
                     world.server.jurisdiction, world.server.latencyMs,
                     KeyToken(world.server.keyVerified));
  for (auto const& d : world.myDevices) {
    out += std::format(L"device {} \"{}\" owner=\"{}\" key={} {} lastSeen=\"{}\" thisPc={}\n",
                       d.id, d.name, d.ownerName, HexPrefix(d.ownerKey),
                       d.online ? L"online" : L"offline", d.lastSeenLabel,
                       int(d.isThisComputer));
  }
  for (auto const& n : world.relayPath) {
    out += std::format(L"relay  \"{}\" / \"{}\" glyph={} {} ms healthy={}\n", n.label,
                       n.subLabel, GlyphCode(n.glyph), n.hopMs, int(n.healthy));
  }

  for (auto const& c : world.conversations) {
    // Line 1 starts "\nconv "; the continuation starts with FIVE spaces, so
    // neither can be mistaken for a "\n  msg "/"\n  day "/"\n  sys " row line.
    out += std::format(
        L"\nconv {} {} \"{}\" key={} groupId={} unread={} muted={} disappearing={} "
        L"members={}/{} rows={}\n"
        L"     retention=\"{}\" media=\"{}\" preview=\"{}\" time=\"{}\"\n",
        c.id, KindName(c.kind), c.name, HexPrefix(c.identityKey),
        c.groupIdHex.empty() ? std::wstring{L"-"} : c.groupIdHex, c.unread,
        int(c.muted), int(c.disappearing), c.memberCount, c.members.size(),
        c.rows.size(), c.retentionLabel, c.mediaRetentionLabel, c.preview,
        c.timeLabel);
    for (auto const& m : c.members) {
      out += std::format(L"  memb {} \"{}\" key={} admin={} devices={}\n", m.id,
                         m.displayName, HexPrefix(m.identityKey), int(m.admin),
                         m.devices.size());
      for (auto const& d : m.devices) {
        out += std::format(L"    dev {} \"{}\" {} \"{}\"\n", d.id, d.name,
                           d.online ? L"online" : L"offline", d.lastSeenLabel);
      }
    }
    for (auto const& r : c.rows) {
      // MessageInspect's cipher field is DELIBERATELY OMITTED from this line:
      // a specific algorithm name is a claim about what encrypted the
      // message, whatever label sits above it, and the field's standing
      // no-render ruling (InspectRailFields.h:26-27) does not except the dump
      // (the d7 audit's A6 override). Do not "complete" the line.
      out += std::format(
          L"  {} {} {} {} sender=\"{}\" key={} \"{}\" t=\"{}\" perm={} sys=\"{}\" "
          L"reason=\"{}\"\n"
          L"       epoch={} leaf={} ret={} size=\"{}\" wire={} att={} "
          L"gid={} from=\"{}\" sent=\"{}\" recv=\"{}\" delivered={} read={}\n",
          RowTag(r.kind), r.id, r.outgoing ? L"out" : L"in", StateName(r.state),
          r.senderName, HexPrefix(r.senderKey), r.body, r.timeLabel,
          int(r.permanentRecord), r.systemText, r.failureReason, r.inspect.epoch,
          r.inspect.senderLeafIndex, RetentionName(r.inspect.retention),
          r.inspect.sizeBucket, r.inspect.wireSizeBytes,
          KeyToken(r.inspect.attestationVerified),
          r.inspect.groupIdHex.empty() ? std::wstring{L"-"} : r.inspect.groupIdHex,
          r.inspect.senderDisplayName, r.inspect.sentAtLabel,
          r.inspect.receivedAtLabel, r.inspect.deliveredTo.size(),
          r.inspect.readBy.size());
    }
  }
  return out;
}

std::vector<std::wstring> WorldDumpDiagnostics() {
  std::vector<std::wstring> lines;
  auto const& world = demo::GetWorld();

  size_t expectedRows = 0;
  for (auto const& c : world.conversations) expectedRows += c.rows.size();

  const std::wstring dump = DumpDemoWorld();

  auto count = [&dump](std::wstring_view prefix) {
    size_t n = 0, pos = 0;
    while ((pos = dump.find(prefix, pos)) != std::wstring::npos) {
      ++n;
      pos += prefix.size();
    }
    return n;
  };

  // EXACTLY these three prefixes, and nothing else in the dump begins with
  // them: a conversation's continuation line starts with five spaces, a
  // member line with "  memb " and a device line with "    dev ". Counting
  // "\n  " alone would double-count the continuation lines.
  const size_t convLines = count(L"\nconv ");
  const size_t rowLines =
      count(L"\n  msg ") + count(L"\n  day ") + count(L"\n  sys ");

  // The dump is built from the SEEDED world (design §5, contract §1), so two
  // calls in one process must be byte-identical.
  const bool stable = (dump == DumpDemoWorld());

  // The framing is part of the dump's contract (G4): it is what the Copy
  // button puts on the clipboard, so dropping it must fail here rather than
  // ship silently. "Demo model:" with a capital D — the exact string the
  // rail's lock header ships; a lowercase paraphrase is not the reused
  // string and does not satisfy this check.
  const std::wstring firstLine = dump.substr(0, dump.find(L'\n'));
  const bool framed = firstLine.find(L"Demo model:") != std::wstring::npos;

  // EQUALITY, not >=. A dump that silently dropped rows must fail here; that
  // is the whole reason this assertion exists.
  const bool ok = (convLines == world.conversations.size()) &&
                  (rowLines == expectedRows) && stable && framed && !dump.empty();
  lines.push_back(std::format(
      L"  dump.world       : {} {} conv lines == {} conversations; {} row lines == "
      L"{} rows; stable across two calls: {}; framing first line: {}   [query: "
      L"occurrences of \"\\nconv \" and of \"\\n  msg \"+\"\\n  day \"+\"\\n  sys \"; "
      L"first line contains \"Demo model:\"]",
      ok ? L"PASS" : L"FAIL", convLines, world.conversations.size(), rowLines,
      expectedRows, stable ? L"yes" : L"NO", framed ? L"yes" : L"NO"));
  return lines;
}

}  // namespace urmsg::views
