//
// Aggregating shim — the UI module has been split across engine/ui/. This header
// is preserved so existing #include "engine/GameUiEngine.hpp" call sites keep
// working. New code should include the targeted ui/* headers directly.
//

#pragma once

#include "ui/UIBase.hpp"
#include "ui/UIElements.hpp"
#include "ui/UILayout.hpp"
#include "ui/UIState.hpp"
#include "ui/UIWindow.hpp"
#include "ui/GameUIEngine.hpp"
