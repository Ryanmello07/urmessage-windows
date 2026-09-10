// SPDX-License-Identifier: MPL-2.0
//
// NO pch.h include, and App.vcxproj compiles this unit with
// PrecompiledHeader=NotUsing: Startup.cpp's `demo stress` line calls
// BuildStressHistory from CollectDiagnostics before winrt::init_apartment,
// the same reason DemoWorld.cpp beside it is NotUsing.
#include "Demo/DemoStress.h"

#include <format>
#include <iterator>  // make_move_iterator
#include <string>

namespace urmsg::demo {
namespace {

// Messages per synthetic day, oldest day first, cycled. 30..39 keeps a day
// separator landing every ~30-40 rows of history, the density the task asked
// the captures to show.
constexpr int kDayMessages[] = {34, 31, 38, 36, 33, 39, 30, 35};

// Weekday labels for the separators, NEWEST synthetic day first: the newest
// synthetic day reads "Sunday", the one above it "Saturday", and the cycle
// wraps weekly. Any weekday name is plausible next to the fixture's
// "Yesterday"/"Today"; the bodies underneath say "Stress load", so a cropped
// capture can never read the set as real history (G4).
constexpr wchar_t const* kDayNamesNewestFirst[] = {L"Sunday",   L"Saturday",
                                                   L"Friday",   L"Thursday",
                                                   L"Wednesday", L"Tuesday",
                                                   L"Monday"};

// Same-sender streak lengths, cycled. 2..4 is the run structure of a real
// group thread and it is what makes the corner/run geometry (T7) exercise
// First/Middle/Last at scale instead of a column of singles.
constexpr int kStreakLens[] = {2, 3, 4, 3};

// Speaker rotation across streaks. Values index the group's members; OUT is
// "You". Adjacent entries always differ, so two streaks can never merge into
// one run by accident; outgoing turns up every third streak or so, the way
// the fixture's own thread interleaves.
constexpr int kSpeakerCycle[] = {0, 1, -1, 2, 3, -1, 4, 1, -1, 3};
constexpr int kOut = -1;

// Body fragments. Every body is "Stress load NNN — <fragment>": readable
// synthetic English that names what it is. No fragment states or implies
// anything about encryption, verification or delivery beyond the delivery
// field itself (G4).
constexpr wchar_t const* kShort[] = {
    L"checking scroll rhythm.",
    L"window probe baseline.",
    L"timing pass reads clean.",
    L"capture folder refreshed.",
    L"same seed, same bytes.",
    L"row height sample.",
};
constexpr wchar_t const* kMedium[] = {
    L"checking the scroll rhythm against the plain build before any "
    L"windowing pass lands.",
    L"a longer sample line so the bubble wraps the way a real thread wraps "
    L"on a narrow window.",
    L"nothing in this row is real traffic; it exists so the backlog has "
    L"something to scroll.",
};
// \n inside the body: a two-line bubble, the row-height variety a real
// backlog gets from pasted quotes.
constexpr wchar_t const* kTwoLine[] = {
    L"sample row, line one.\nLine two keeps the bubble tall, the way a "
    L"pasted quote reads.",
    L"first line of a two-line sample.\nThe second line is what changes the "
    L"row height.",
};

std::wstring BodyFor(int ordinal, int k) {
  // k % 8 picks the length class (one two-line and four mediums per eight
  // rows, the rest short); k / 8 rotates the fragment inside the class, so
  // neighbouring rows never repeat and the rotation period is long enough
  // that a screenful shows no obvious loop.
  wchar_t const* fragment = nullptr;
  switch (k % 8) {
    case 0: fragment = kTwoLine[(k / 8) % 2]; break;
    case 4:
    case 5:
    case 6: fragment = kMedium[(k / 8) % 3]; break;
    default: fragment = kShort[(k / 8) % 6]; break;
  }
  // U+2014 EM DASH, escaped per the non-ASCII rule: PUA/raw glyphs in source
  // render blank in terminals and a dropped one is invisible.
  return std::format(L"Stress load {:03} \u2014 {}", ordinal, fragment);
}

}  // namespace

std::vector<MessageRow> BuildStressHistory(World const& w, int n) {
  std::vector<MessageRow> out;
  if (n <= 0 || w.conversations.empty()) return out;
  Conversation const& c = w.conversations.front();

  // The outgoing senderKey is LIFTED from the fixture's own first outgoing
  // row rather than invented (the W9 ambient seed's rule): the run geometry
  // unifies outgoing rows by direction, but anything that ever hashes the key
  // sees the same "me" the fixture uses.
  Seed outgoingKey{};
  for (auto const& r : c.rows)
    if (r.kind == RowKind::Message && r.outgoing) {
      outgoingKey = r.senderKey;
      break;
    }

  // The synthetic epochs walk backward from the fixture's first row, so the
  // whole conversation stays strictly ordered old -> new across the seam.
  const uint64_t firstEpoch =
      c.rows.empty() ? w.currentEpoch : c.rows.front().inspect.epoch;

  // Partition n into [separator + messages] day blocks, oldest first. A
  // remainder too small to open a day with its separator folds into the
  // newest day built so far (or, for n smaller than one day, stands as
  // separator-less rows above every label — the T1 audit gates unlabelled,
  // adjacent and trailing separators, and this block trips none of them).
  // daySep is tracked explicitly: a fold grows a day WITHOUT growing its
  // separator count, so "msgs != pattern" cannot be used to infer one.
  std::vector<int> dayMsgs;
  std::vector<bool> daySep;
  {
    int left = n;
    int d = 0;
    while (left > 0) {
      const int m = kDayMessages[d % 8];
      if (left <= m) {
        if (dayMsgs.empty()) {
          dayMsgs.push_back(left);
          daySep.push_back(false);
        } else {
          dayMsgs.back() += left;
        }
        left = 0;
      } else {
        dayMsgs.push_back(m);
        daySep.push_back(true);
        left -= 1 + m;  // one row of budget is the separator
        ++d;
      }
    }
  }
  const int dayCount = static_cast<int>(dayMsgs.size());
  out.reserve(static_cast<size_t>(n));

  // Streak/speaker state carries across days; a day boundary always starts a
  // fresh streak (the separator breaks the run anyway), so the truncated
  // last streak of a day is the only run under 2.
  int streakStep = 0;    // position in kStreakLens
  int speakerStep = 0;   // position in kSpeakerCycle
  int outOrdinal = 0;    // outgoing rows so far, for the delivery spread
  int emitted = 0;       // synthetic rows so far, for ids and epochs
  std::wstring previousSender;  // the fixture's run-continuation rule, mirrored

  for (int di = 0; di < dayCount && emitted < n; ++di) {
    const std::wstring dayLabel =
        kDayNamesNewestFirst[(dayCount - 1 - di) % 7];
    const int msgs = dayMsgs[di];

    // The separator first, when this day paid for one — only the
    // separator-less remainder block (daySep false) skips it.
    if (daySep[di]) {
      MessageRow sep{};
      sep.kind = RowKind::DaySeparator;
      sep.id = std::format(L"c0-s{:04}", emitted);
      sep.body = dayLabel;  // DaySeparatorLabel() reads body (Demo/ThreadLayout.h)
      ++emitted;
      previousSender.clear();  // a separator breaks a run
      out.push_back(std::move(sep));
    }

    for (int j = 0; j < msgs && emitted < n; ) {
      const int streak = kStreakLens[streakStep % 4];
      ++streakStep;
      const int speaker = kSpeakerCycle[speakerStep % 10];
      ++speakerStep;
      const int take = (std::min)(streak, msgs - j);

      for (int t = 0; t < take && emitted < n; ++t, ++j, ++emitted) {
        MessageRow r{};
        r.kind = RowKind::Message;
        r.id = std::format(L"c0-s{:04}", emitted);

        const int minutes = 8 * 60 + 3 + j * 13;  // 08:03 on, 13 min apart
        r.timeLabel = std::format(L"{:02}:{:02}", minutes / 60, minutes % 60);

        const bool outgoing = (speaker == kOut);
        r.outgoing = outgoing;

        MemberRef const* member =
            outgoing ? nullptr
                     : &c.members[static_cast<size_t>(speaker) % c.members.size()];
        const std::wstring senderDisplay =
            outgoing ? std::wstring(L"You") : member->displayName;

        r.senderKey = outgoing ? outgoingKey : member->identityKey;
        // senderName on a run START only, empty on continuations — the same
        // rule DemoWorld's AppendRows applies, so the stressed world's
        // bubbles follow the fixture's pattern exactly.
        const bool continuation =
            !previousSender.empty() && !outgoing && previousSender == member->id;
        if (!outgoing && !continuation) r.senderName = senderDisplay;
        previousSender = outgoing ? std::wstring() : member->id;

        // Mostly Read, a few Delivered/Sent — and never Pending/Failed/
        // Expired: those are curated fixture states (the thread's 12:11
        // Pending foot, the one 12:09 Failed) and diluting them in synthetic
        // history would make every gate that counts them less sharp.
        if (outgoing) {
          r.state = (outOrdinal % 11 == 0) ? DeliveryState::Sent
                  : (outOrdinal % 7 == 0)  ? DeliveryState::Delivered
                                           : DeliveryState::Read;
          ++outOrdinal;
        } else {
          r.state = DeliveryState::Sent;  // meaningful only on outgoing rows
        }

        r.body = BodyFor(emitted + 1, emitted);

        MessageInspect& ins = r.inspect;
        // Epochs walk backward from the fixture's first row; clamped at 1 so
        // an N beyond the fixture's epoch budget wraps nothing.
        const int64_t epoch =
            static_cast<int64_t>(firstEpoch) - static_cast<int64_t>(n - emitted);
        ins.epoch = static_cast<uint64_t>(epoch > 1 ? epoch : 1);
        ins.senderLeafIndex =
            outgoing ? 0u
                     : static_cast<uint32_t>(
                           (static_cast<size_t>(speaker) % c.members.size()) + 1);
        ins.retention = c.disappearing ? RetentionClass::Eph : RetentionClass::Permanent;
        // The fixture's own formulas (DemoWorld.cpp's AppendRows), so the
        // rail's Advanced rows read in the same shape as every other row.
        ins.sizeBucket = (r.body.size() < 80) ? L"<= 1 KiB" : L"<= 4 KiB";
        ins.wireSizeBytes = static_cast<uint32_t>(288 + r.body.size() * 2);
        ins.attestationVerified = true;  // no synthetic row is Failed
        // Populated for consistency with every world row and rendered
        // NOWHERE — the standing G4 ruling (InspectRailFields.h) forbids a
        // cipher name on any surface, and the stress rows change nothing
        // about that.
        ins.cipher = L"XChaCha20-Poly1305";
        ins.groupIdHex = c.groupIdHex;
        ins.senderDisplayName = senderDisplay;
        ins.sentAtLabel = dayLabel + L" " + r.timeLabel;

        if (outgoing) {
          const bool reached = (r.state == DeliveryState::Delivered ||
                                r.state == DeliveryState::Read);
          if (reached) {
            ins.receivedAtLabel = ins.sentAtLabel;
            for (auto const& m : c.members)
              for (auto const& dev : m.devices) ins.deliveredTo.push_back(dev);
            if (r.state == DeliveryState::Read) ins.readBy = ins.deliveredTo;
          }
        } else {
          ins.receivedAtLabel = ins.sentAtLabel;
          ins.deliveredTo = w.myDevices;
          ins.readBy = w.myDevices;
        }

        out.push_back(std::move(r));
      }
    }
  }
  return out;
}

int PrependStressHistory(World& w, int n) {
  if (n <= 0 || w.conversations.empty()) return 0;
  Conversation& c = w.conversations.front();
  // Idempotence per world: every synthetic id carries the c0-s prefix, so a
  // world whose history already starts with one was extended before.
  if (!c.rows.empty() && c.rows.front().id.starts_with(L"c0-s")) return 0;
  std::vector<MessageRow> history = BuildStressHistory(w, n);
  const int added = static_cast<int>(history.size());
  c.rows.insert(c.rows.begin(), std::make_move_iterator(history.begin()),
                std::make_move_iterator(history.end()));
  return added;
}

}  // namespace urmsg::demo
