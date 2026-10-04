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

// ---- hue math (fix round 1) -------------------------------------------
//
// Round 1's PaletteAvoidsStateColors did an exact byte compare against a
// Tint()ed colour, and Tint() mixes toward kCard, which is ACHROMATIC
// (R==G==B). Mixing any colour with grey scales every channel DIFFERENCE by
// the same factor and changes none of their signs, which is exactly what
// hue is a function of - so Tint() can change saturation and lightness but
// can mathematically never change hue. The byte-equality assert was
// therefore checking a condition Tint() guarantees can never occur: a gate
// that cannot fail. Four colours came out within 0.0-3.3 degrees of a
// reserved hue (see task-F5-report.md fix round 1 for the exact figures) -
// visually indistinguishable from the state colour they sat 1-2% off from.
//
// The fix compares HUE, not bytes, with a minimum separation - computed
// with scaled integers (kHueScale units per degree) so it is exact,
// deterministic, constexpr arithmetic, with no <cmath> and no floating
// point rounding to explain away in a static_assert failure.
constexpr int kHueScale = 100;  // values below are degrees * 100

constexpr int HueTimesScale(Color c) {
  const int r = c.R, g = c.G, b = c.B;
  const int cmax = (r > g) ? (r > b ? r : b) : (g > b ? g : b);
  const int cmin = (r < g) ? (r < b ? r : b) : (g < b ? g : b);
  const int delta = cmax - cmin;
  if (delta == 0) return 0;  // achromatic: undefined hue; not hit by this palette
  int h;
  if (cmax == r) {
    h = (g - b) * 60 * kHueScale / delta;
    if (h < 0) h += 360 * kHueScale;
  } else if (cmax == g) {
    h = (b - r) * 60 * kHueScale / delta + 120 * kHueScale;
  } else {
    h = (r - g) * 60 * kHueScale / delta + 240 * kHueScale;
  }
  return h;
}

// Circular distance between two hues, in the same degrees*kHueScale units.
constexpr int HueDistance(int h1, int h2) {
  int d = h1 - h2;
  if (d < 0) d = -d;
  const int wrap = 360 * kHueScale - d;
  return d < wrap ? d : wrap;
}

// THE FLOOR, chosen before the palette below and not fitted to it. 30
// degrees is one step of a 12-hue colour wheel (360/12): small enough to
// still leave two full hue bands - teal/cyan and violet/magenta, the only
// two nothing in UrColors.h currently claims - for a six-colour palette to
// live in, large enough that no adjacent named hue FAMILY ("a bit more
// orange than kProGold", "a bit more cyan than kToggleAccent") reads as the
// state colour it is next to. Every entry below clears it with at least
// 12.6 degrees of measured margin to spare - see the per-entry comments.
constexpr int kMinHueSeparationDegrees = 30;

// The six colours this app reserves for state. kStatusIdle is new in this
// round: its own doc comment in UrColors.h names it the IDLE state of the
// same three-state connect indicator as kUrGreen (connected) and
// kStatusConnecting (connecting) - exactly as much state as either of those,
// and round 1's five-name list missed it.
constexpr std::array<Color, 6> kStateColors{
    urnw::colors::kDanger,      urnw::colors::kUrGreen,
    urnw::colors::kToggleAccent, urnw::colors::kStatusConnecting,
    urnw::colors::kProGold,      urnw::colors::kStatusIdle,
};

constexpr bool ClearsAllStateHues(Color c) {
  const int h = HueTimesScale(c);
  for (Color state : kStateColors) {
    if (HueDistance(h, HueTimesScale(state)) < kMinHueSeparationDegrees * kHueScale)
      return false;
  }
  return true;
}

// Five of these six are NOT existing UrColors.h hues, and that is itself
// fix round 1's finding: red is kDanger's, yellow/gold is kProGold's and
// kStatusConnecting's, green is kUrGreen's, and blue is kToggleAccent's and
// kStatusIdle's - between them the brand's named colours already occupy
// every hue family except magenta. Tint() cannot rescue a colour whose
// SOURCE hue is inside a reserved family (it cannot change hue at all - see
// above), so the replacements are new decorative-only RGB literals placed
// deliberately in the two open bands, each still folded toward kCard by the
// same Tint() so the whole palette shares one saturation/lightness
// treatment. Measured hue and margin above the 30 degree floor, after
// Tint(), verified independently in Python against colorsys (not by hand):
constexpr Color kIdenticonIris{255, 140, 54, 226};  // hue 270.0, margin 15.2 deg
constexpr Color kIdenticonJade{255, 66, 215, 140};  // hue 149.7, margin 12.7 deg
constexpr Color kIdenticonTeal{255, 66, 215, 215};  // hue 180.0, margin 14.3 deg
constexpr Color kIdenticonPlum{255, 215, 66, 215};  // hue 300.0, margin 37.3 deg
constexpr Color kIdenticonSage{255, 66, 215, 178};  // hue 165.2, margin 28.1 deg

constexpr std::array<Color, 6> kPalette{
    // unchanged from round 1: kUrPink's magenta already cleared every
    // reserved hue by 65+ degrees, so nothing here needed to move.
    Tint(urnw::colors::kUrPink, 48),  // orchid, hue 290.1
    Tint(kIdenticonIris, 90),  // iris  - replaces indigo (kUrElectricBlue was
                               // hue 224.6, 0.2 deg from kStatusIdle)
    Tint(kIdenticonJade, 90),  // jade  - replaces sand (kUrAmber was hue
                               // 42.9, 3.2 deg from kProGold)
    Tint(kIdenticonTeal, 90),  // teal  - replaces slate (kStatusIdle itself,
                               // hue 224.8 - the newly-reserved colour used
                               // to be the palette's own source)
    Tint(kIdenticonPlum, 90),  // plum  - replaces clay (kUrMutedCoral was
                               // hue 8.4, 1.1 deg from kDanger)
    Tint(kIdenticonSage, 90),  // sage  - replaces "pale olive" (kAccent was
                               // hue 68.0, 6.8 deg from kStatusConnecting -
                               // a FIFTH collision round 1's review did not
                               // list, found only by computing every entry's
                               // hue rather than trusting the four flagged)
};

// THE CONSTRAINT, AS CODE. An avatar that happens to be within one hue
// family of the danger red, the "on" green, the toggle/idle blue, the
// connecting yellow or the Pro gold is a colour that could be MISTAKEN for
// state in this product, and a person cannot tell a decorative use from a
// semantic one at a glance. kProGold is named here only as a thing the
// palette is proved NOT to be close to - the opposite of spending it.
constexpr bool PaletteClearsReservedHues() {
  for (Color c : kPalette) {
    if (!ClearsAllStateHues(c)) return false;
  }
  return true;
}
static_assert(PaletteClearsReservedHues(),
              "an identicon hue is within 30 degrees of a colour that carries state");

}  // namespace

constexpr IdenticonPattern MakeIdenticonPattern(urmsg::demo::Seed const& seed) {
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

namespace {

// Compile-time proof that MakeIdenticonPattern can still reach both ends of
// the [1,23] range P3 checks over 30 fixed real seeds at runtime. Not
// exhaustive over the 2^15 independent-bit combinations - that is possible
// but needs the compiler's constexpr step budget raised for the sake of one
// invariant, which is more build machinery than the property is worth - but
// it DOES catch the exact regression review measured: delete the
// corner-off/centre-on override at the end of MakeIdenticonPattern above and
// the reachable extremes move to 0 and 25. The natural range without the
// override is [6,20] - still inside [1,23] - so the runtime P3 pass over 30
// real seeds would not notice; neither extreme seed happens to be one of
// them. These two asserts fail to compile instead.
constexpr size_t PopCount(std::array<bool, 25> const& cells) {
  size_t n = 0;
  for (bool b : cells) {
    if (b) ++n;
  }
  return n;
}
constexpr urmsg::demo::Seed UniformSeed(uint8_t byte) {
  urmsg::demo::Seed s{};
  for (auto& b : s) b = byte;
  return s;
}
static_assert(PopCount(MakeIdenticonPattern(UniformSeed(0x00)).cells) == 1,
              "the minimum-density identicon no longer has exactly 1 cell set "
              "- the corner-off/centre-on override regressed");
static_assert(PopCount(MakeIdenticonPattern(UniformSeed(0xFF)).cells) == 23,
              "the maximum-density identicon no longer has exactly 23 cells set "
              "- the corner-off/centre-on override regressed");

}  // namespace

Border MakeIdenticon(urmsg::demo::Seed const& seed, double size) {
  const IdenticonPattern pattern = MakeIdenticonPattern(seed);
  const auto hue = kPalette[pattern.colorIndex];

  Border root;
  root.Width(size);
  root.Height(size);
  // ITS OWN radius, from the pure rule IdenticonCornerRadius (the .h): one
  // shape rule for every size, so callers must not set one. Border clips its
  // Child to this radius, which is what keeps the corner cells inside it.
  root.CornerRadius(CornerRadiusHelper::FromUniformRadius(IdenticonCornerRadius(size)));
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

void SetIdenticonPlateAlpha(Border const& identicon, uint8_t alpha) {
  if (!identicon) return;
  auto brush = identicon.Background().try_as<Media::SolidColorBrush>();
  if (!brush) return;
  auto color = brush.Color();
  color.A = alpha;
  brush.Color(color);
}

Border MakeIdenticonLattice(double size) {
  Border root;
  root.Width(size);
  root.Height(size);
  // The same shape rule MakeIdenticon owns (IdenticonCornerRadius), so a
  // lattice and an identicon at one size are the same silhouette. No plate:
  // the frame is the whole point.
  root.CornerRadius(CornerRadiusHelper::FromUniformRadius(IdenticonCornerRadius(size)));

  Grid grid;
  for (int i = 0; i < 5; ++i) {
    RowDefinition r;
    r.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    grid.RowDefinitions().Append(r);
    ColumnDefinition c;
    c.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    grid.ColumnDefinitions().Append(c);
  }
  // One brush for all 25 cells: kTextFaint at 0x4D, achromatic, so no hue
  // enters the app and the palette's separation proof has nothing new to
  // clear. 0x4D, not the 0x33 it shipped with: at 10dip (the DEMO chip) the
  // 0x33 lattice was sub-visible at 100% (polish B2). 0x4D is the plate-lift
  // step the conversation rows already spend on hover
  // (ConversationListView.cpp), so the lattice and the avatars move in one
  // established increment rather than a new one. The margin keeps adjacent
  // outlines from merging into a mesh.
  auto line = urnw::colors::MakeBrush(urnw::colors::WithAlpha(urnw::colors::kTextFaint, 0x4D));
  for (int row = 0; row < 5; ++row) {
    for (int col = 0; col < 5; ++col) {
      Border cell;
      cell.BorderBrush(line);
      cell.BorderThickness(ThicknessHelper::FromUniformLength(1));
      cell.CornerRadius(CornerRadiusHelper::FromUniformRadius(2));
      cell.Margin(ThicknessHelper::FromUniformLength(1));
      Grid::SetRow(cell, row);
      Grid::SetColumn(cell, col);
      grid.Children().Append(cell);
    }
  }
  root.Child(grid);
  return root;
}

}  // namespace urmsg
