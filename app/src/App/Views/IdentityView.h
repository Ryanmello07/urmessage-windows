// THE IDENTITY SCREENS, DRAWN (identity phase 1, Task 6: slice 0).
//
// One builder for every overlay screen Demo/IdentityDemo.h models: it draws the model's blocks in
// order and nothing else, so what --diagnose reads off the model (Property 1) is what the window
// shows, and the shots read it back with UI Automation. MainWindow hosts the result over the whole
// content area, asks for capture exclusion when the model says so (Property 2), and carries out
// each button's Action.
//
// The word fields keep their settings here, from the block's FieldPolicy and nothing else: spell
// check and text prediction off, and a password input scope (SCOPE H1; F3:894-900). A live screen's
// fields (Task 16) are masked as well: each word is hidden when focus leaves its field, with one
// switch that shows them all, and nothing in them can be copied or cut, so no word reaches the
// clipboard or its history. The remote-session warning is a line of the model.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

#include "Demo/IdentityDemo.h"
#include "RunMode.h"

namespace urmsg::views {

struct IdentityScreenView {
  urmsg::demo::identity::Screen screen = urmsg::demo::identity::Screen::None;
  winrt::Microsoft::UI::Xaml::FrameworkElement root{nullptr};
  // the word fields, in order, and the words they are checked against
  std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBox> wordFields;
  // the live screens' fields (Task 16), masked, in place of `wordFields`
  std::vector<winrt::Microsoft::UI::Xaml::Controls::PasswordBox> maskedFields;
  winrt::Microsoft::UI::Xaml::Controls::CheckBox revealToggle{nullptr};
  std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> wordNumbers;  // marked when wrong
  std::vector<std::wstring> expectedWords;
  winrt::Microsoft::UI::Xaml::Controls::TextBlock wordStatus{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox nameField{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox codeField{nullptr};
};

using IdentityActionHandler = std::function<void(urmsg::demo::identity::Button const&)>;
// The list's own check of one word (urnet_message_device_check_word), for a live field to mark a
// word that is not on the list as it is left. Empty on a fabricated screen.
using CheckWordFn = std::function<bool(std::wstring const&)>;

IdentityScreenView MakeIdentityScreen(urmsg::demo::identity::ScreenModel const& model,
                                      IdentityActionHandler const& onAction,
                                      CheckWordFn const& checkWord = {});

// What every word field holds, in order, masked or not. The caller wipes what it took.
std::vector<std::wstring> ReadIdentityFields(IdentityScreenView const& view);

// Empty every word field, before the view is let go.
void ClearIdentityFields(IdentityScreenView const& view);

// The typed-back check on this view's fields: the first wrong word is named in the status line and
// its field is marked; nothing is corrected. True when every word matches.
bool CheckIdentityWords(IdentityScreenView const& view, urmsg::RunMode mode);

// Demo only: fills every word field with the word it is checked against.
void FillIdentityWords(IdentityScreenView const& view);

}  // namespace urmsg::views
