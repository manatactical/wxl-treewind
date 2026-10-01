// wxl-treewind: extension entry points and the overlay-panel bridge.
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

#include "ExtensionApi.hpp"
#include "TreeWind.hpp"

#include "wxl/PluginApi.h"

namespace
{
    void __cdecl DrawPanel(void*)
    {
        if (wxl_treewind::g_api)
            wxl::features::treewind::DrawPanel(*wxl_treewind::g_api);
    }
}

extern "C" __declspec(dllexport) const WXL_PluginInfo* __cdecl WXL_Query()
{
    static const WXL_PluginInfo info = {
        sizeof(WXL_PluginInfo),
        WXL_API_VERSION,
        "wxl-treewind",
        1,
        WXL_CLIENT_BUILD,
    };
    return &info;
}

extern "C" __declspec(dllexport) int __cdecl WXL_Load(const WXL_Api* api)
{
    if (!api || api->apiVersion != WXL_API_VERSION) return 0;

    wxl_treewind::g_api = api;

    if constexpr (wxl_treewind::kEnabled)
        wxl::features::treewind::InstallTreeWind();

    if (api->UiAddPanel)
        api->UiAddPanel("Tree Wind", &DrawPanel, nullptr);

    api->Log(WXL_LOG_INFO, "wxl-treewind", "treewind ready");
    return 1;
}
