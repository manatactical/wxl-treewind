// wxl-treewind: the extension-wide service table pointer and hook-install convenience, shared by
// every translation unit in this DLL.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "wxl/PluginApi.h"

/// The core hands this pointer to WXL_Load once and it lives for the process lifetime, so every call
/// site reaches it through here instead of threading an `api` parameter through the call chain.
namespace wxl_treewind
{
    extern const WXL_Api* g_api;

    /// True builds the tree-motion feature in; false leaves every placed tree stock-still.
    inline constexpr bool kEnabled = true;

    /// The names the core registers the bone-palette build points under (see
    /// wxl-core/src/runtime/HookPoints.cpp). Naming the point instead of hardcoding an address keeps
    /// this module on the SDK side of the client-version boundary.
    inline constexpr char kBuildPalettePoint[]       = "M2.BuildBonePalette";
    inline constexpr char kBuildPaletteSimplePoint[] = "M2.BuildBonePaletteSimple";

    /**
     * @brief Typed detour install over WXL_Api::HookAttachByName: detour and original share one
     *        function type, deduced, so wiring a hook to the wrong original no longer compiles.
     */
    template <class Fn>
    inline int HookAttachByName(const char* name, Fn* detour, Fn** original,
                                int priority = WXL_HOOK_DEFAULT_PRIORITY)
    {
        return g_api->HookAttachByName(name, reinterpret_cast<void*>(detour),
                                       reinterpret_cast<void**>(original), priority);
    }
}

// common/Log.hpp's WLOG_* macros need common/Log.cpp linked in, which is core/host/patcher-only; an
// extension has no such object file, so these route the same call-site syntax through WXL_Api::Log.
#define WLOG_TRACE(...) ::wxl_treewind::g_api->Log(WXL_LOG_TRACE, "wxl-treewind", __VA_ARGS__)
#define WLOG_DEBUG(...) ::wxl_treewind::g_api->Log(WXL_LOG_DEBUG, "wxl-treewind", __VA_ARGS__)
#define WLOG_INFO(...)  ::wxl_treewind::g_api->Log(WXL_LOG_INFO,  "wxl-treewind", __VA_ARGS__)
#define WLOG_WARN(...)  ::wxl_treewind::g_api->Log(WXL_LOG_WARN,  "wxl-treewind", __VA_ARGS__)
#define WLOG_ERROR(...) ::wxl_treewind::g_api->Log(WXL_LOG_ERROR, "wxl-treewind", __VA_ARGS__)
