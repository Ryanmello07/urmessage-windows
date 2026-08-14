// SPDX-License-Identifier: MPL-2.0
#pragma once

#include "App.xaml.g.h"

namespace winrt::URmessage::implementation {

struct App : AppT<App> {
  App();
  void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

 private:
  // The one window. Held so it is not collected the instant OnLaunched returns.
  winrt::Microsoft::UI::Xaml::Window window_{nullptr};
};

}  // namespace winrt::URmessage::implementation
