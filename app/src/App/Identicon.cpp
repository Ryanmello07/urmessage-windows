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
