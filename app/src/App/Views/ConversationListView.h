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
#include "Views/ConversationRowModel.h"

namespace urmsg::views {

// 64, not the kit's UrPaneRowTallHeight of 44 (App.xaml:1005): a conversation
// row carries a 40dip identicon plus two text lines, and 44 would put 40 of
// identicon inside 44 of row. ONE height for the whole list, passed to every
// MakePaneTwoLineRowButton call -- the property MakePaneRow's comment
// (UrComponents.cpp:153-158) exists to protect.
inline constexpr double kConversationRowHeight = 64.0;

struct ConversationListView {
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  // The RECENT group header, kept so the entrance can include it at delay 0
  // (d3 2.6): the chrome must not pop a frame ahead of the rows it titles.
  winrt::Microsoft::UI::Xaml::FrameworkElement header{nullptr};
  // rows[i] is world.conversations[i]. Nothing reorders this vector: contract 1
  // limits MutableWorld() to appending a MessageRow and advancing one
  // DeliveryState, so an index stays valid for the life of the view.
  std::vector<urnw::kit::PaneTwoLineRowButton> rows;
  // names[i] is what rows[i] says to a screen reader, unselected and selected, taken from the
  // SAME conversation the row was drawn from when the list was built (ConversationRowNamesFor).
  // Selection reads it back and never looks a world up, so a row cannot be announced as some other
  // world's conversation (F1: it used to be named from demo::GetWorld()).
  std::vector<ConversationRowNames> names;
  // THE NOTICE above the rows (the conversation-list design 2.5, A12), or nullptr: a group on this
  // device's disk that did not restore. Not a row: nothing selects it, filters it or counts it.
  winrt::Microsoft::UI::Xaml::FrameworkElement notice{nullptr};
  // THE REQUESTS SLOT (the conversation-list design 3): an empty host above RECENT, collapsed. It
  // draws nothing and carries no copy and no name; the requests screens fill it in their own task.
  winrt::Microsoft::UI::Xaml::Controls::Grid requests{nullptr};
};

// The whole list: the notice when there is one, the RECENT group header, then one row per
// conversation.
// `onSelect` is called with the row's INDEX -- the same index
// SetConversationSelected takes, and an int captured by value cannot dangle the
// way a reference into world.conversations could.
// `entrance` false draws the rows at rest, with no entrance start pose: a list rebuilt after its
// entrance has played (the conversation-list design 2.6, A21).
ConversationListView MakeConversationList(urmsg::demo::World const& world,
                                          std::function<void(int)> onSelect,
                                          std::wstring const& notice = {}, bool entrance = true);

// Paint row `index` as the selected one and every other row as not. Pass -1 for
// "nothing is selected". Three channels, because one is not enough: a fill step,
// a 2px leading accent bar (a SHAPE change, so colour is never the only carrier)
// and the row's automation Name -- the same three kit::SetPaneListRowSelected
// carries, for the same reason (UrComponents.h:333-340). The Name comes from `v.names`, which the
// list took from the world it was built from, and from no other world.
void SetConversationSelected(ConversationListView& v, int index);

// Start the staggered row entrance: opacity PLUS an 8dip rise per row, and
// the RECENT header at delay 0 (d3 2.6). Call AFTER Window.Activate(), from
// MainWindow::StartReveal -- MakeConversationList already wrote the start pose,
// because it runs from the MainWindow constructor and WindowReveal.h:50-54 fixes
// that split for every reveal in this app.
//
// A no-op when motion::ShouldAnimate() is false, in which case neither the
// opacity nor the transform start pose was ever written and there is nothing
// to settle.
void AnimateConversationListEntrance(ConversationListView& v);

// Hide every row whose conversation does not match `query`, show the rest, and
// return how many are visible. Visibility only: a filter that rebuilt the list
// would throw away selection and every row's entrance opacity, and `rows[i]`
// would stop being `world.conversations[i]`.
//
// Returns std::size_t, and the caller uses it: the pane header's count has to
// say how many rows are on screen, not how many exist.
std::size_t ApplyConversationListFilter(ConversationListView& v,
                                        urmsg::demo::World const& world,
                                        std::wstring const& query);

// Show or hide the Advanced-Mode group-id chip on every group row (design 6.6).
// DENSITY ONLY -- one Visibility per row, no rebuild and no crossfade
// (contract 4). The chip exists on every row already, collapsed and empty on a
// DM, so this call cannot change the list's measured height or its row count.
void SetConversationListAdvanced(ConversationListView& v, bool advanced);

}  // namespace urmsg::views
