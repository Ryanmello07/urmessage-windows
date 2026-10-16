// THE IDENTITY SCREENS, DRAWN (identity phase 1, Task 6: slice 0).
//
// One builder for every overlay screen Demo/IdentityDemo.h models: it draws the model's blocks in
// order and nothing else, so what --diagnose reads off the model (Property 1) is what the window
// shows, and the shots read it back with UI Automation. MainWindow hosts the result over the whole
// content area, asks for capture exclusion when the model says so (Property 2), and carries out
// each button's Action.
//
// The word fields keep their settings here, because these are the screens Task 16 makes live: spell
// check and text prediction off, and a password input scope (SCOPE H1; F3:894-900). Masking a word
// when focus leaves it, the clipboard history and the remote-session warning are Task 16's.
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
  std::vector<std::wstring> expectedWords;
  winrt::Microsoft::UI::Xaml::Controls::TextBlock wordStatus{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox nameField{nullptr};
  winrt::Microsoft::UI::Xaml::Controls::TextBox codeField{nullptr};
};

using IdentityActionHandler = std::function<void(urmsg::demo::identity::Button const&)>;

IdentityScreenView MakeIdentityScreen(urmsg::demo::identity::ScreenModel const& model,
                                      IdentityActionHandler const& onAction);

// The typed-back check on this view's fields: the first wrong word is named in the status line and
// its field is marked; nothing is corrected. True when every word matches.
bool CheckIdentityWords(IdentityScreenView const& view, urmsg::RunMode mode);

// Demo only: fills every word field with the word it is checked against.
void FillIdentityWords(IdentityScreenView const& view);

}  // namespace urmsg::views
