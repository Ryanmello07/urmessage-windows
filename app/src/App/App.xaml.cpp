// SPDX-License-Identifier: MPL-2.0
#include "pch.h"

#include "App.xaml.h"

#include <string>

#include "Demo/DemoSwitches.h"
#include "Log.h"
#include "MainWindow.xaml.h"
#include "Startup.h"
#include "Strings.h"
#include "WindowShell.h"

// NOTE: App.g.cpp is deliberately NOT included here, unlike MainWindow.g.cpp in
// MainWindow.xaml.cpp. App.vcxproj's UrmCompileGeneratedXamlImpl comment (copied
// from the VPN client) claims a `#if __has_include("App.g.cpp")` was added to
// that project's App.xaml.cpp; it was not, and it must not be — cppwinrt emits
// no factory_implementation::App for an Application subclass, so App.g.cpp
// references symbols App.g.h never declares and will not compile. The global
// maker below is what module.g.cpp actually needs.

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;

// cppwinrt's generated module.g.cpp (the activation-factory dispatcher) links
// against the global ::winrt_make_URmessage_App(). Unlike MainWindow (a
// Microsoft.UI.Xaml.Window), cppwinrt emits NO factory_implementation::App or
// composition ctor for App (a Microsoft.UI.Xaml.Application subclass) —
// App.g.cpp references them but App.g.h / URmessage.2.h never declare them.
// That is correct: App is a singleton created via Application::Start ->
// winrt::make<implementation::App> (main.cpp) and is NEVER activated by name,
// so this maker is never invoked. Returning null is the honest answer — App has
// no activation factory — and it lets URmessage.exe link. Global namespace, to
// match module.g.cpp's extern declaration.
void* winrt_make_URmessage_App() { return nullptr; }

namespace winrt::URmessage::implementation {

App::App() {
  // An exception that reaches XAML ends the process, and a window that never
  // appears is indistinguishable from an app that was never started, so this
  // handler is the last chance to name the cause. Registered in every
  // configuration, not only _DEBUG: Release is the build the owner runs.
  // Handled() is deliberately NOT set: masking it would leave a half-dead app.
  UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e) {
    const std::wstring message{e.Message()};
    urnw::LogError("app: unhandled exception: {}", urnw::Narrow(message));
#if defined(_DEBUG)
    if (IsDebuggerPresent()) __debugbreak();
#endif
    urnw::FailVisible(L"URmessage hit an unexpected error and has to close.", message);
  });
  urnw::LogInfo("app: XAML Application constructed");
}

void App::OnLaunched(LaunchActivatedEventArgs const&) {
  // main.cpp already opened the log (its first instruction) — this is the entry
  // marker for the XAML side of the handoff, and the proof for wWinMain that
  // XAML got this far at all.
  urnw::MarkLaunched();
  urnw::LogInfo("app: OnLaunched");

  try {
    window_ = make<MainWindow>();

    // The native shell: caption-button colours in the brand, rounded corners,
    // the compact 480x760 default, and placement that survives a restart. It
    // needs the HWND, which only exists once the Window does. Deliberately NO
    // system backdrop — see the long note in WindowShell.cpp: Mica draws BEHIND
    // XAML, so showing it means clearing the opaque #101010 root, at which
    // point the desktop wallpaper REPLACES the brand colour instead of tinting
    // it. That removal is load-bearing and must not be undone here.
    if (auto native = window_.try_as<::IWindowNative>()) {
      HWND hwnd = nullptr;
      native->get_WindowHandle(&hwnd);
      if (hwnd) {
        // --demo, and only --demo, opens wide enough for the third pane.
        // A normal launch is unchanged: 480x760, centred, WindowShell.h:21.
        const auto demo = urmsg::demo::ParseDemoOptions();
        const int w = demo.enabled ? urnw::shell::kDemoWidthDips : 0;
        const int h = demo.enabled ? urnw::shell::kDemoHeightDips : 0;
        urnw::shell::ApplyNativeShell(window_, hwnd, w, h);
      }
    }

    window_.Activate();

    // The reveal was ARMED in MainWindow's constructor — it has to write its
    // start pose before the first composed frame, or the window appears settled
    // and then jumps. Starting it is the other half, and it belongs AFTER
    // Activate() returns.
    if (auto self = window_.try_as<URmessage::MainWindow>()) {
      winrt::get_self<MainWindow>(self)->StartReveal();
    }
    urnw::LogInfo("app: main window activated");
  } catch (winrt::hresult_error const& e) {
    urnw::FailVisible(L"URmessage could not create its main window.",
                      std::wstring{e.message()});
    if (auto app = Application::Current()) app.Exit();
    return;
  } catch (const std::exception& e) {
    urnw::FailVisible(L"URmessage could not create its main window.",
                      urnw::Widen(e.what()));
    if (auto app = Application::Current()) app.Exit();
    return;
  }

  // Here, and not earlier: the window has already resolved its localized
  // strings, so the resource loader is cached either way and this only reads
  // what the UI itself got. Run before that and this probe would BE the first
  // lookup, and a probe failing for its own reasons would leave every string in
  // the UI rendering as a key id for the rest of the process (see Startup.h).
  urnw::LogInfo("startup: {}", urnw::Narrow(urnw::ResourceProbe()));
  urnw::LogInfo("app: launch complete");
}

}  // namespace winrt::URmessage::implementation
