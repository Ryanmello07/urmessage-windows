// The conversation list pane (design doc 6.1; Spec C 4.1 row anatomy).
//
// The kit grain, not MVVM: a struct of named elements plus free Make*/Set*
// functions, exactly as UrComponents.h does it. No class with virtuals, no
// observable type, no IDL -- so a list built here and a pane declared in markup
// are the same pane.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Demo/DemoWorld.h"
#include "UrComponents.h"

namespace urmsg::views {

// 64, not the kit's UrPaneRowTallHeight of 44 (App.xaml:1005): a conversation
// row carries a 40dip identicon plus two text lines, and 44 would put 40 of
// identicon inside 44 of row. ONE height for the whole list, passed to every
// MakePaneTwoLineRowButton call -- the property MakePaneRow's comment
// (UrComponents.cpp:153-158) exists to protect.
inline constexpr double kConversationRowHeight = 64.0;

struct ConversationListView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  // rows[i] is world.conversations[i]. Nothing reorders this vector: contract 1
  // limits MutableWorld() to appending a MessageRow and advancing one
  // DeliveryState, so an index stays valid for the life of the view.
  std::vector<urnw::kit::PaneTwoLineRowButton> rows;
};

// The whole list: the RECENT group header, then one row per conversation.
// `onSelect` is called with the row's INDEX -- the same index
// SetConversationSelected takes, and an int captured by value cannot dangle the
// way a reference into world.conversations could.
ConversationListView MakeConversationList(urmsg::demo::World const& world,
                                          std::function<void(int)> onSelect);

// Paint row `index` as the selected one and every other row as not. Pass -1 for
// "nothing is selected". Three channels, because one is not enough: a fill step,
// a 2px leading accent bar (a SHAPE change, so colour is never the only carrier)
// and the row's automation Name -- the same three kit::SetPaneListRowSelected
// carries, for the same reason (UrComponents.h:333-340).
void SetConversationSelected(ConversationListView& v, int index);

// The rest of contract 4's ConversationListView API (SetConversationListAdvanced)
// is declared by the task that IMPLEMENTS it. A declaration without a
// definition is a link error waiting for whoever builds next.

}  // namespace urmsg::views
