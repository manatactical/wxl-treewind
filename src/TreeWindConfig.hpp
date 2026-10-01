// Tree motion: the .ini side of the module. The wind and filter rows are read from
// wxl-treewind.ini next to the DLL, re-read live while the client runs, and written back by the
// overlay's Save button. Keeping the knobs in a file means a user can retune the sway without a
// rebuild and without the overlay. Copyright (C) 2026 WarcraftXL
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

namespace wxl::features::treewind
{
    /// Resolves wxl-treewind.ini beside the DLL and reads it into Wind()/Filter(). Creates the file
    /// from the built-in defaults when it is absent, so there is always something to edit. Call once
    /// from InstallTreeWind.
    void LoadTreeWindConfig();

    /// Re-reads the .ini when its last-write time changed, at most once a second. Cheap enough to
    /// call from the per-frame sway path.
    void ReloadTreeWindConfigIfChanged();

    /// Writes the live Wind()/Filter() rows back to the .ini, preserving any keys the module does
    /// not own.
    void SaveTreeWindConfig();

    /// Discards live edits and re-reads the .ini.
    void RevertTreeWindConfig();

    /// True when the live rows differ from the last values read from or written to the .ini.
    bool TreeWindConfigHasUnsavedChanges();

    /// The resolved .ini path, or an empty string before LoadTreeWindConfig.
    const char* TreeWindConfigPath();
}
