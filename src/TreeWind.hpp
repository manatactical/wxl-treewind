// Tree motion: the client's placed trees are rigid, so WXL gives them the same ambient wind the grass
// module gives the ground cover. A detour on the per-instance bone-palette build tilts each tree's
// live world (placement) matrix about its own base by a small oscillating angle, so the trunk stays
// planted and the canopy drifts with the wind. Whole-tree rigid tilt, not per-branch movement -- the
// same "slightly moves" remit as the grass, at a fraction of a degree so nothing reads as unnatural.
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

#include "ExtensionApi.hpp"

namespace wxl::features::treewind
{
    /// Wind row: a directional two-wave sway field over the trees.
    struct WindSettings
    {
        bool  enabled           = true;   // master switch for the sway
        float directionDeg      = 35.0f;  // wind heading in the world XY plane, degrees
        float speed             = 3.0f;   // wave travel speed, yards per second
        float amplitudeDeg      = 0.60f;  // primary wave tilt at the crown, degrees
        float wavelength        = 26.0f;  // primary wave length, yards
        float crossAmplitudeDeg = 0.22f;  // secondary cross-swell tilt, degrees
        float crossWavelength   = 9.0f;   // secondary wave length, yards
        float crossAngleDeg     = 40.0f;  // secondary wave heading offset from the primary, degrees
        float leanDeg           = 0.25f;  // constant downwind lean, degrees
        float variance          = 0.50f;  // per-tree amplitude spread, 0 (uniform) .. 1 (+/-50%)
        float gust              = 0.35f;  // slow gust modulation depth, 0 none .. 1 full
        float distanceFade      = 0.00f;  // per-yard sway attenuation, 0 disables
    };

    /// Filter row: which placed models count as a tree. Every enabled gate must pass (AND), so a
    /// keyword-less tall rock spire is rejected by the name test even though its bounds are tree-like.
    struct FilterSettings
    {
        bool  doodadsOnly   = true;  // only map-placed static doodads (never units, weapons, spell FX)
        bool  matchKeywords = true;  // model path contains a foliage keyword (tree/pine/canopy/...)
        bool  matchTallThin = false; // model bounds are tall and narrow enough to read as a tree
        float minHeight     = 8.0f;  // tall-thin gate: minimum model-local height, yards
        float minAspect     = 1.80f; // tall-thin gate: height / widest horizontal extent
        float maxHeight     = 125.0f; // skip trees taller than this, world yards (0 = unlimited)
        float maxDistance   = 0.0f;  // ignore trees farther than this, yards (0 = unlimited)
    };

    /// The single global settings rows, read and written on the game thread only (no synchronization
    /// needed). Valid whether or not the detour installed.
    WindSettings&   Wind();
    FilterSettings& Filter();

    /// True once the bone-palette detour installed (so tuning actually reaches the trees).
    bool Installed();

    /// Installs the detours. Called once from WXL_Load; returns false when no hook attached.
    bool InstallTreeWind();

    /// Draws the module's overlay panel body; called by the core while the overlay is open.
    void DrawPanel(const WXL_Api& api);
}
