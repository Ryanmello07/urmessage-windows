// THE CONVERSATION LIST'S PURE RULES, and their --diagnose lines (the conversation-list design,
// scratchpad/convlist/DESIGN.md, accepted as R59).
//
// A live device holds many groups. What the list shows, in what order, which conversation an action
// goes to, which loop a push wakes, when a new conversation may be made, and which one opens: every
// one of those is decided here, by a pure function, so --diagnose can hold each to its rule before
// the apartment exists and a mutant can put the one-group design back and be seen to fail.
//
// PURE C++, NO winrt, NO XAML, NO SDK: CollectDiagnostics calls this before winrt::init_apartment,
// and the live worker calls the same functions on its own thread.
//
// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

namespace urmsg::live {

// This file's --diagnose lines, in the "  key              : PASS  detail" shape.
std::vector<std::wstring> CollectConversationRulesDiagnostics();

}  // namespace urmsg::live
