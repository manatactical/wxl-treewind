// Tree motion: wxl-treewind.ini reading, live reload and writing.
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

#include "TreeWindConfig.hpp"

#include "ExtensionApi.hpp"
#include "TreeWind.hpp"

#include <windows.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace wxl::features::treewind
{
    namespace
    {
        // Trees and bushes are separate rows with their own sections and their own values, so a bush
        // can be tuned without touching the tree sway or loosening the tree size filter.
        constexpr const char* kTreeSection = "TreeWind";
        constexpr const char* kBushSection = "Bush";
        constexpr const char* kFileName    = "wxl-treewind.ini";
        constexpr DWORD       kReloadEveryMs = 1000; // one file-time probe a second is plenty

        // The slider ranges the overlay panel uses, kept here too so a hand-edited file cannot push a
        // value somewhere the render path or the UI does not expect.
        struct WindFloat
        {
            const char* key;
            float WindSettings::*member;
            float lo;
            float hi;
        };
        struct FilterFloat
        {
            const char* key;
            float FilterSettings::*member;
            float lo;
            float hi;
        };
        struct BushFloat
        {
            const char* key;
            float BushSettings::*member;
            float lo;
            float hi;
        };
        struct WindBool
        {
            const char* key;
            bool WindSettings::*member;
        };
        struct FilterBool
        {
            const char* key;
            bool FilterSettings::*member;
        };
        struct BushBool
        {
            const char* key;
            bool BushSettings::*member;
        };

        const WindFloat kWindFloats[] = {
            {"Direction", &WindSettings::directionDeg, 0.0f, 360.0f},
            {"Speed", &WindSettings::speed, 0.0f, 12.0f},
            {"Amplitude", &WindSettings::amplitudeDeg, 0.0f, 6.0f},
            {"Wavelength", &WindSettings::wavelength, 2.0f, 80.0f},
            {"Lean", &WindSettings::leanDeg, 0.0f, 4.0f},
            {"CrossAmplitude", &WindSettings::crossAmplitudeDeg, 0.0f, 3.0f},
            {"CrossWavelength", &WindSettings::crossWavelength, 2.0f, 40.0f},
            {"CrossAngle", &WindSettings::crossAngleDeg, 0.0f, 180.0f},
            {"Variance", &WindSettings::variance, 0.0f, 1.0f},
            {"Gust", &WindSettings::gust, 0.0f, 1.0f},
            {"DistanceFade", &WindSettings::distanceFade, 0.0f, 0.05f},
        };

        // Bushes share the wind field shape and slider ranges with the trees; only the values differ.
        const FilterFloat kFilterFloats[] = {
            {"MinHeight", &FilterSettings::minHeight, 0.0f, 40.0f},
            {"MinAspect", &FilterSettings::minAspect, 0.5f, 6.0f},
            {"MaxHeight", &FilterSettings::maxHeight, 0.0f, 300.0f},
            {"MaxDistance", &FilterSettings::maxDistance, 0.0f, 500.0f},
        };

        // Bushes have no shape gate, so their filter row is only distance plus the two shared gates.
        const BushFloat kBushFloats[] = {
            {"MaxDistance", &BushSettings::maxDistance, 0.0f, 500.0f},
        };

        const WindBool kWindBools[] = {
            {"Enabled", &WindSettings::enabled},
        };

        const FilterBool kFilterBools[] = {
            {"DoodadsOnly", &FilterSettings::doodadsOnly},
            {"MatchKeywords", &FilterSettings::matchKeywords},
            {"MatchTallThin", &FilterSettings::matchTallThin},
            {"ExcludeDead", &FilterSettings::excludeDead},
        };

        const BushBool kBushBools[] = {
            {"DoodadsOnly", &BushSettings::doodadsOnly},
            {"ExcludeDead", &BushSettings::excludeDead},
        };

        std::string    g_path;
        unsigned long long g_stamp     = 0;
        DWORD          g_lastCheck = 0;
        WindSettings   g_savedWind;
        FilterSettings g_savedFilter;
        BushSettings   g_savedBush;
        bool           g_loaded    = false;

        unsigned long long FileStamp(const std::string& path)
        {
            WIN32_FILE_ATTRIBUTE_DATA data;
            if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
                return 0;
            return (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                   data.ftLastWriteTime.dwLowDateTime;
        }

        float Clamped(float v, float lo, float hi)
        {
            return v >= lo ? (v <= hi ? v : hi) : lo;
        }

        float ReadFloat(const std::string& path, const char* section, const char* key, float fallback)
        {
            char buf[64] = {};
            GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), path.c_str());
            if (!buf[0])
                return fallback;
            char* end = nullptr;
            const float v = std::strtof(buf, &end);
            if (end == buf || v != v) // unparsable or NaN
                return fallback;
            return v;
        }

        std::string SettingText(float value)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.4g", value);
            return buf;
        }

        void WriteSetting(const std::string& path, const char* section, const char* key,
                          const std::string& text)
        {
            WritePrivateProfileStringA(section, key, text.c_str(), path.c_str());
        }

        void ReadWind(const std::string& path, const char* section, WindSettings& wind)
        {
            for (const WindBool& s : kWindBools)
                wind.*s.member =
                    GetPrivateProfileIntA(section, s.key, wind.*s.member ? 1 : 0, path.c_str()) != 0;
            for (const WindFloat& s : kWindFloats)
                wind.*s.member = Clamped(ReadFloat(path, section, s.key, wind.*s.member), s.lo, s.hi);
        }

        void WriteWind(const std::string& path, const char* section, const WindSettings& wind)
        {
            for (const WindBool& s : kWindBools)
                WriteSetting(path, section, s.key, wind.*s.member ? "1" : "0");
            for (const WindFloat& s : kWindFloats)
                WriteSetting(path, section, s.key, SettingText(wind.*s.member));
        }

        void ReadFilter(const std::string& path, const char* section, FilterSettings& filter)
        {
            for (const FilterBool& s : kFilterBools)
                filter.*s.member =
                    GetPrivateProfileIntA(section, s.key, filter.*s.member ? 1 : 0, path.c_str()) != 0;
            for (const FilterFloat& s : kFilterFloats)
                filter.*s.member =
                    Clamped(ReadFloat(path, section, s.key, filter.*s.member), s.lo, s.hi);
        }

        void WriteFilter(const std::string& path, const char* section, const FilterSettings& filter)
        {
            for (const FilterBool& s : kFilterBools)
                WriteSetting(path, section, s.key, filter.*s.member ? "1" : "0");
            for (const FilterFloat& s : kFilterFloats)
                WriteSetting(path, section, s.key, SettingText(filter.*s.member));
        }

        void ReadBush(const std::string& path, const char* section, BushSettings& bush)
        {
            ReadWind(path, section, bush.wind);
            for (const BushBool& s : kBushBools)
                bush.*s.member =
                    GetPrivateProfileIntA(section, s.key, bush.*s.member ? 1 : 0, path.c_str()) != 0;
            for (const BushFloat& s : kBushFloats)
                bush.*s.member = Clamped(ReadFloat(path, section, s.key, bush.*s.member), s.lo, s.hi);
        }

        void WriteBush(const std::string& path, const char* section, const BushSettings& bush)
        {
            WriteWind(path, section, bush.wind);
            for (const BushBool& s : kBushBools)
                WriteSetting(path, section, s.key, bush.*s.member ? "1" : "0");
            for (const BushFloat& s : kBushFloats)
                WriteSetting(path, section, s.key, SettingText(bush.*s.member));
        }

        bool SameWind(const WindSettings& a, const WindSettings& b)
        {
            for (const WindBool& s : kWindBools)
                if (a.*s.member != b.*s.member)
                    return false;
            for (const WindFloat& s : kWindFloats)
                if (a.*s.member != b.*s.member)
                    return false;
            return true;
        }

        bool SameFilter(const FilterSettings& a, const FilterSettings& b)
        {
            for (const FilterBool& s : kFilterBools)
                if (a.*s.member != b.*s.member)
                    return false;
            for (const FilterFloat& s : kFilterFloats)
                if (a.*s.member != b.*s.member)
                    return false;
            return true;
        }

        bool SameBush(const BushSettings& a, const BushSettings& b)
        {
            if (!SameWind(a.wind, b.wind))
                return false;
            for (const BushBool& s : kBushBools)
                if (a.*s.member != b.*s.member)
                    return false;
            for (const BushFloat& s : kBushFloats)
                if (a.*s.member != b.*s.member)
                    return false;
            return true;
        }

        std::string ModuleDirectory()
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCSTR>(&ModuleDirectory), &module))
                return std::string();
            char path[MAX_PATH] = {};
            const DWORD n = GetModuleFileNameA(module, path, MAX_PATH);
            std::string dir(path, n);
            const size_t slash = dir.find_last_of("\\/");
            return slash == std::string::npos ? std::string() : dir.substr(0, slash + 1);
        }
    }

    void LoadTreeWindConfig()
    {
        if (g_loaded)
            return;

        const std::string dir = ModuleDirectory();
        g_path = dir + kFileName;
        g_loaded = true;

        WindSettings   wind   = Wind();   // built-in defaults as the fallback for missing keys
        FilterSettings filter = Filter();
        BushSettings   bush   = Bush();
        ReadWind(g_path, kTreeSection, wind);
        ReadFilter(g_path, kTreeSection, filter);
        ReadBush(g_path, kBushSection, bush);
        Wind()   = wind;
        Filter() = filter;
        Bush()   = bush;
        g_savedWind   = wind;
        g_savedFilter = filter;
        g_savedBush   = bush;

        // A first run has no file to edit, so lay one down from the defaults.
        if (FileStamp(g_path) == 0)
        {
            WriteWind(g_path, kTreeSection, wind);
            WriteFilter(g_path, kTreeSection, filter);
            WriteBush(g_path, kBushSection, bush);
            WLOG_INFO("treewind: wrote default config to %s", g_path.c_str());
        }
        g_stamp = FileStamp(g_path);
        WLOG_INFO("treewind: loaded %s", g_path.c_str());
    }

    void ReloadTreeWindConfigIfChanged()
    {
        if (!g_loaded)
            return;

        const DWORD now = GetTickCount();
        if (now - g_lastCheck < kReloadEveryMs)
            return;
        g_lastCheck = now;

        const unsigned long long stamp = FileStamp(g_path);
        if (stamp == g_stamp)
            return;
        g_stamp = stamp;

        WindSettings   wind   = Wind();
        FilterSettings filter = Filter();
        BushSettings   bush   = Bush();
        ReadWind(g_path, kTreeSection, wind);
        ReadFilter(g_path, kTreeSection, filter);
        ReadBush(g_path, kBushSection, bush);
        Wind()   = wind;
        Filter() = filter;
        Bush()   = bush;
        g_savedWind   = wind;
        g_savedFilter = filter;
        g_savedBush   = bush;
        WLOG_INFO("treewind: reloaded %s", g_path.c_str());
    }

    void SaveTreeWindConfig()
    {
        if (!g_loaded)
            return;

        WriteWind(g_path, kTreeSection, Wind());
        WriteFilter(g_path, kTreeSection, Filter());
        WriteBush(g_path, kBushSection, Bush());
        g_stamp = FileStamp(g_path);
        g_savedWind   = Wind();
        g_savedFilter = Filter();
        g_savedBush   = Bush();
        WLOG_INFO("treewind: saved %s", g_path.c_str());
    }

    void RevertTreeWindConfig()
    {
        if (!g_loaded)
            return;

        WindSettings   wind   = Wind();
        FilterSettings filter = Filter();
        BushSettings   bush   = Bush();
        ReadWind(g_path, kTreeSection, wind);
        ReadFilter(g_path, kTreeSection, filter);
        ReadBush(g_path, kBushSection, bush);
        Wind()   = wind;
        Filter() = filter;
        Bush()   = bush;
        g_savedWind   = wind;
        g_savedFilter = filter;
        g_savedBush   = bush;
        g_stamp = FileStamp(g_path);
        WLOG_INFO("treewind: reverted to %s", g_path.c_str());
    }

    bool TreeWindConfigHasUnsavedChanges()
    {
        return !SameWind(Wind(), g_savedWind) || !SameFilter(Filter(), g_savedFilter) ||
               !SameBush(Bush(), g_savedBush);
    }

    const char* TreeWindConfigPath()
    {
        return g_path.c_str();
    }
}
