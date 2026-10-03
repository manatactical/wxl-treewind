// Tree motion: tilt each placed tree's world matrix about its base with a small travelling-sine
// wind field. See TreeWind.hpp for what the feature is and why it rides the bone-palette build.
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

#include "TreeWind.hpp"

#include "TreeWindConfig.hpp"

#include "engine/events/Event.hpp"
#include "game/Camera.hpp"
#include "offsets/game/Doodad.hpp"
#include "offsets/game/M2.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace wxl::features::treewind
{
    namespace
    {
        namespace m2off = wxl::offsets::game::m2;
        namespace ddoff = wxl::offsets::game::doodad;
        namespace ev    = wxl::events;
        namespace cam   = wxl::game::camera;

        constexpr float    kPi      = 3.14159265358979f;
        constexpr float    kTwoPi   = 6.28318530717959f;
        constexpr float    kDeg2Rad = kPi / 180.0f;
        // ownerFlags bit 0x20: exclusive to map-placed static doodads (ADT/WMO placements), never set
        // by unit / mount / weapon / missile / spell-visual / UI creation paths (see offsets/game/M2.hpp).
        constexpr uint32_t kDoodadOwnerFlag = 0x20;

        WindSettings   g_wind;
        FilterSettings g_filter;
        BushSettings   g_bush;
        bool           g_installed = false;

        m2off::M2_BuildBonePaletteFn g_origFull   = nullptr;
        m2off::M2_BuildBonePaletteFn g_origSimple = nullptr;

        // The live placement of a static doodad is never rewritten by the engine, so the pristine
        // matrix only has to be captured once per instance. Keeping it here lets each frame rebuild
        // the sway from the untitled base instead of accumulating rotation frame after frame.
        //
        // The table is fixed-size while instances churn as chunks stream in and out, so each entry
        // also carries the tick it was last touched. Without that, retired instances' entries were
        // never reclaimed: once the table filled, no newly streamed tree could obtain a base and the
        // sway silently died off (the "fewer trees move the longer the session runs" decay).
        struct BaseEntry
        {
            void*    instance = nullptr;
            uint32_t model    = 0;
            DWORD    stamp    = 0; // GetTickCount of the last touch; drives stale-entry eviction
            float    base[12] = {}; // the top three rows of the placement (row 3 is the translation)
        };
        constexpr size_t kCacheSize  = 8192; // power of two; the probe wraps with a mask
        constexpr size_t kProbeLimit = 32;   // entries examined per lookup before giving up
        // An entry untouched for longer than this belongs to an instance the engine has retired. A
        // tree still in the scene has its palette built every frame, so its entry stays fresh.
        constexpr DWORD  kStaleMs    = 2000;
        BaseEntry        g_cache[kCacheSize];
        CRITICAL_SECTION g_cacheLock;
        bool             g_cacheReady = false;

        inline size_t CacheIndex(void* p)
        {
            // Pointer alignment is >= 4, so the low bits carry no entropy -- shift them off before
            // folding into the table.
            return (reinterpret_cast<uintptr_t>(p) >> 4) & (kCacheSize - 1);
        }

        inline float* PlacementOf(void* instance)
        {
            return reinterpret_cast<float*>(static_cast<char*>(instance) + m2off::kOffInstPlacement);
        }

        // The cached pristine placement for an instance, inserting it on first sight. Returns false
        // only when every slot the probe reaches is freshly used (a live, saturated neighbourhood);
        // the caller then leaves the tree stock for this frame.
        bool BaseFromCache(void* instance, uint32_t model, float out[12])
        {
            float* const cur = PlacementOf(instance);
            if (!g_cacheReady)
            {
                std::memcpy(out, cur, sizeof(float) * 12);
                return true;
            }

            const DWORD now = GetTickCount();
            bool found = false;
            EnterCriticalSection(&g_cacheLock);
            const size_t start = CacheIndex(instance);
            size_t victim      = kCacheSize; // stalest resident of this probe window
            DWORD  victimStamp = 0;
            for (size_t probe = 0; probe < kProbeLimit; ++probe)
            {
                const size_t idx = (start + probe) & (kCacheSize - 1);
                BaseEntry& e = g_cache[idx];
                if (e.instance == instance)
                {
                    if (e.model != model) // a reused slot with a new model: re-seed the base
                    {
                        e.model = model;
                        std::memcpy(e.base, cur, sizeof(e.base));
                    }
                    e.stamp = now;
                    std::memcpy(out, e.base, sizeof(e.base));
                    found = true;
                    break;
                }
                if (e.instance == nullptr)
                {
                    e.instance = instance;
                    e.model    = model;
                    e.stamp    = now;
                    std::memcpy(e.base, cur, sizeof(e.base));
                    std::memcpy(out, e.base, sizeof(e.base));
                    found = true;
                    break;
                }
                if (victim == kCacheSize || e.stamp < victimStamp)
                {
                    victimStamp = e.stamp;
                    victim      = idx;
                }
            }
            // Saturated probe window: take over the least-recently-seen slot, but only when it is old
            // enough to be a retired instance. A live tree is touched every frame, so its slot is
            // never eligible and its pristine base is never lost.
            if (!found && victim < kCacheSize && now - victimStamp > kStaleMs)
            {
                BaseEntry& e = g_cache[victim];
                e.instance = instance;
                e.model    = model;
                e.stamp    = now;
                std::memcpy(e.base, cur, sizeof(e.base));
                std::memcpy(out, e.base, sizeof(e.base));
                found = true;
            }
            LeaveCriticalSection(&g_cacheLock);
            return found;
        }

        void ClearCache()
        {
            if (!g_cacheReady) return;
            EnterCriticalSection(&g_cacheLock);
            std::memset(g_cache, 0, sizeof(g_cache));
            LeaveCriticalSection(&g_cacheLock);
        }

        void __cdecl OnWorldChanged(void*, const void*)
        {
            ClearCache(); // a map change retires every instance; drop their cached placements
        }

        // Case-insensitive substring test, bounded to the inline path stem the engine keeps.
        bool ContainsNoCase(const char* hay, const char* needle)
        {
            if (!hay || !needle || !*needle) return false;
            for (int i = 0; hay[i] && i < 512; ++i)
            {
                int h = i, n = 0;
                while (hay[h] && needle[n])
                {
                    char a = hay[h], b = needle[n];
                    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
                    if (a != b) break;
                    ++h; ++n;
                }
                if (!needle[n]) return true;
            }
            return false;
        }

        bool MatchesTreeKeyword(const char* path)
        {
            // Names are matched case-insensitively against the model path stem. Deliberately avoids
            // bare "fir" (matches "fire") and non-tree props ("bush", "shrub", "trunk",
            // "leaves") so only a whole tree qualifies. Bushes are claimed by their own keyword row
            // (see MatchesBushKeyword), not this one. Tune via the overlay panel or by extending this
            // list.
            static const char* const kKeywords[] = {
                "tree", "pines", "pine", "spruce", "cedar", "oak", "willow", "birch", "aspen",
                "palm", "canopy", "conifer", "maple", "eucalyptus", "redwood", "cypress",
                "larch", "juniper", "mangrove", "olive", "baobab", "treant",
            };
            for (const char* k : kKeywords)
                if (ContainsNoCase(path, k))
                    return true;
            return false;
        }

        // Low, wide foliage that the tree row's size gate exists to reject. Bushes are matched by name
        // only and never run through LooksTallThin/maxHeight, so a bush keeps swaying even though its
        // bounds are nothing like a tree's.
        bool MatchesBushKeyword(const char* path)
        {
            static const char* const kKeywords[] = {
                "bush", "shrub", "fern", "hedge", "thicket", "bramble", "undergrowth",
                "topiary", "bracken", "sapling",
            };
            for (const char* k : kKeywords)
                if (ContainsNoCase(path, k))
                    return true;
            return false;
        }

        // Words that mark a placed model as a dead, bare, burnt or cut-down tree -- or a loose tree
        // part (stump, snag, log, driftwood). Checked before the live-tree keyword so "DeadTree",
        // "BurntPine" and "Treestump" cannot sway just because they carry a tree word. Every entry
        // is a whole word that only appears in dead or felled wood; short/ambiguous tokens ("dry",
        // "sere") are left out so a living model is never filtered out by accident.
        bool MatchesDeadKeyword(const char* path)
        {
            static const char* const kDeadKeywords[] = {
                "dead", "deadwood", "burnt", "burned", "charred", "scorch", "wither",
                "wilted", "blight", "corrupt", "cursed", "bare", "leafless", "defoliated",
                "skeletal", "petrified", "stump", "snag", "log", "driftwood", "uprooted",
                "felled", "fallen", "rotten", "hollow",
            };
            for (const char* k : kDeadKeywords)
                if (ContainsNoCase(path, k))
                    return true;
            return false;
        }

        // Classifying a model's path is the hot cost of ApplySway, but the answer never changes for a
        // given model. The palette build runs on the game thread and the animate workers, so each
        // thread memoizes the models it has seen: the ~47 keyword scans happen once per model instead
        // of once per instance per frame. The path hash guards against a freed model's address being
        // reused for a different one.
        struct PathClass
        {
            uint32_t model = 0;
            uint32_t hash  = 0;
            bool     tree  = false;
            bool     bush  = false;
            bool     dead  = false;
        };
        constexpr size_t kPathMemoSize = 256; // power of two
        thread_local PathClass t_pathMemo[kPathMemoSize];

        uint32_t PathHash(const char* s)
        {
            uint32_t h = 2166136261u; // FNV-1a
            for (int i = 0; s[i] && i < 128; ++i)
            {
                h ^= static_cast<uint8_t>(s[i]);
                h *= 16777619u;
            }
            return h;
        }

        const PathClass& ClassifyPath(uint32_t model, const char* path)
        {
            PathClass& slot = t_pathMemo[(model >> 4) & (kPathMemoSize - 1)];
            const uint32_t hash = PathHash(path);
            if (slot.model != model || slot.hash != hash)
            {
                slot.model = model;
                slot.hash  = hash;
                slot.dead  = MatchesDeadKeyword(path);
                slot.bush  = MatchesBushKeyword(path);
                slot.tree  = MatchesTreeKeyword(path);
            }
            return slot;
        }

        // A placed model reads as a tree when its local bounds are tall relative to their widest
        // horizontal extent. Uses the MD20 bounding box (model-local, Z up).
        bool LocalExtents(const m2off::M2Model* mdl, float& ex, float& ey, float& ez)
        {
            if (!mdl || !mdl->header) return false;
            const auto* h = static_cast<const ddoff::MD20Header*>(mdl->header);
            ex = h->bboxMax[0] - h->bboxMin[0];
            ey = h->bboxMax[1] - h->bboxMin[1];
            ez = h->bboxMax[2] - h->bboxMin[2];
            return ex == ex && ey == ey && ez == ez; // reject NaN extents
        }

        bool LooksTallThin(const m2off::M2Model* mdl)
        {
            float ex = 0.0f, ey = 0.0f, ez = 0.0f;
            if (!LocalExtents(mdl, ex, ey, ez)) return false;
            if (ez < g_filter.minHeight) return false;
            const float horiz = ex > ey ? ex : ey;
            if (horiz < 0.01f) return true;
            return (ez / horiz) >= g_filter.minAspect;
        }

        // Largest squared length of the placement's three basis vectors: the model's world scale
        // squared. Comparing squared heights avoids a sqrt per tree when enforcing the height cap.
        float MaxAxisScaleSq(const float* placement)
        {
            float best = 0.0f;
            for (int r = 0; r < 3; ++r)
            {
                const float x = placement[r * 4 + 0];
                const float y = placement[r * 4 + 1];
                const float z = placement[r * 4 + 2];
                const float len = x * x + y * y + z * z;
                if (len > best) best = len;
            }
            return best;
        }

        // Deterministic 0..1 hash of a world position, so neighbouring trees get independent phase and
        // amplitude without needing per-instance storage.
        float Hash01(float x, float y)
        {
            const uint32_t ix = static_cast<uint32_t>(static_cast<int32_t>(x * 8.0f));
            const uint32_t iy = static_cast<uint32_t>(static_cast<int32_t>(y * 8.0f));
            uint32_t h = ix * 73856093u ^ iy * 19349663u;
            h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
            return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000u);
        }

        // Row-vector 3x3 rotation that tilts "up" toward (tx, ty): for small tilt the crown leans in
        // that world-plane direction while the base (the model origin) stays fixed.
        void BuildTiltRowMatrix(float tx, float ty, float m[9])
        {
            const float t2 = tx * tx + ty * ty;
            if (t2 < 1.0e-12f)
            {
                std::memset(m, 0, sizeof(float) * 9);
                m[0] = m[4] = m[8] = 1.0f;
                return;
            }

            const float theta = std::sqrt(t2);
            const float ax    = -ty / theta; // unit axis (sin a1, -cos a1, 0) rotated into place
            const float ay    = tx / theta;
            const float az    = 0.0f;
            const float s     = std::sin(theta);
            const float c     = std::cos(theta);
            const float ic    = 1.0f - c;

            // Column-vector Rodrigues matrix transposed into the engine's row-vector convention.
            m[0] = c + ax * ax * ic;      m[1] = ay * ax * ic + az * s; m[2] = az * ax * ic - ay * s;
            m[3] = ax * ay * ic - az * s; m[4] = c + ay * ay * ic;      m[5] = az * ay * ic + ax * s;
            m[6] = ax * az * ic + ay * s; m[7] = ay * az * ic - ax * s; m[8] = c + az * az * ic;
        }

        // The wind direction trig and wavenumbers depend only on the settings, not on a placement, so
        // they are computed once per settings change (live edits included) rather than four trig calls
        // per model per frame. Per-thread like the path memo, since the animate workers call in too.
        // Trees and bushes keep separate caches so alternating between them does not invalidate the
        // basis on every call.
        struct WindBasis
        {
            float dirDeg = -1.0f, crossDeg = -1.0f, wl = -1.0f, crossWl = -1.0f;
            float d1x = 1.0f, d1y = 0.0f, d2x = 1.0f, d2y = 0.0f, k1 = 1.0f, k2 = 1.0f;
        };
        thread_local WindBasis t_windBasis; // the tree row's cached basis
        thread_local WindBasis t_bushBasis; // the bush row's cached basis

        const WindBasis& WindBasisFor(WindBasis& cache, const WindSettings& w)
        {
            if (cache.dirDeg != w.directionDeg || cache.crossDeg != w.crossAngleDeg ||
                cache.wl != w.wavelength || cache.crossWl != w.crossWavelength)
            {
                cache.dirDeg   = w.directionDeg;
                cache.crossDeg = w.crossAngleDeg;
                cache.wl       = w.wavelength;
                cache.crossWl  = w.crossWavelength;
                const float a1 = w.directionDeg * kDeg2Rad;
                const float a2 = (w.directionDeg + w.crossAngleDeg) * kDeg2Rad;
                cache.k1  = kTwoPi / (w.wavelength > 0.1f ? w.wavelength : 0.1f);
                cache.k2  = kTwoPi / (w.crossWavelength > 0.1f ? w.crossWavelength : 0.1f);
                cache.d1x = std::cos(a1); cache.d1y = std::sin(a1);
                cache.d2x = std::cos(a2); cache.d2y = std::sin(a2);
            }
            return cache;
        }

        void ApplySway(void* instance)
        {
            ReloadTreeWindConfigIfChanged(); // a live edit to wxl-treewind.ini lands here
            if (!instance) return;

            char* inst = static_cast<char*>(instance);
            const uint32_t ownerFlags = *reinterpret_cast<uint32_t*>(inst + m2off::kOffInstOwnerFlags);

            const uint32_t modelPtr = *reinterpret_cast<uint32_t*>(inst + m2off::kOffInstModel);
            if (!modelPtr) return;
            auto* mdl = reinterpret_cast<m2off::M2Model*>(modelPtr);
            if (!mdl->header) return; // still loading / not parsed

            // Name classification is memoized per model: the keyword scans are the hot cost here and
            // the answer is fixed for a model's lifetime.
            const PathClass& pc = ClassifyPath(modelPtr, mdl->pathStem);

            // Pick the row that owns this model. A bush is claimed by name alone and is deliberately
            // exempt from the tree row's shape/size gates: it is short and wide, so LooksTallThin and
            // maxHeight exist precisely to reject it. If bush sway is switched off a bush-named model
            // falls through to the tree test, so one named for both can still sway as a tree.
            const bool isBush = pc.bush && g_bush.wind.enabled;
            const WindSettings& w = isBush ? g_bush.wind : g_wind;
            if (!w.enabled) return;

            bool sway = false;
            if (isBush)
            {
                if (g_bush.doodadsOnly && (ownerFlags & kDoodadOwnerFlag) == 0) return;
                if (g_bush.excludeDead && pc.dead) return;
                sway = true; // name is the only gate; no shape or size test on purpose
            }
            else
            {
                if (g_filter.doodadsOnly && (ownerFlags & kDoodadOwnerFlag) == 0) return;
                if (g_filter.excludeDead && pc.dead) return;

                // Every enabled gate must pass. The name test is what keeps keyword-less tall shapes
                // (rock spires, ruins, totems) out; the shape test trims wide, flat or tiny models
                // that merely happen to sit on a tree-like path.
                sway = g_filter.matchKeywords || g_filter.matchTallThin;
                if (g_filter.matchKeywords && !pc.tree) sway = false;
                if (sway && g_filter.matchTallThin && !LooksTallThin(mdl)) sway = false;
            }
            if (!sway) return;

            float* placement = PlacementOf(instance);

            // Reject oversized trees outright so a giant world-tree model keeps its stock pose
            // instead of leaning its whole canopy across the zone. Bushes never reach this test; they
            // are short by definition and have no height cap.
            if (!isBush && g_filter.maxHeight > 0.0f)
            {
                float ex = 0.0f, ey = 0.0f, ez = 0.0f;
                if (LocalExtents(mdl, ex, ey, ez) && ez > 0.0f)
                {
                    // worldHeight > cap  <=>  ez^2 * scale^2 > cap^2  (both sides non-negative)
                    const float cap = g_filter.maxHeight;
                    if (ez * ez * MaxAxisScaleSq(placement) > cap * cap) return;
                }
            }
            const float px = placement[12];
            const float py = placement[13];
            const float pz = placement[14];

            float camPos[3];
            cam::GetPosition(camPos);
            const float ddx = px - camPos[0];
            const float ddy = py - camPos[1];
            const float ddz = pz - camPos[2];
            const float dist2 = ddx * ddx + ddy * ddy + ddz * ddz;
            const float maxDistance = isBush ? g_bush.maxDistance : g_filter.maxDistance;
            if (maxDistance > 0.0f && dist2 > maxDistance * maxDistance)
                return;

            float base[12];
            if (!BaseFromCache(instance, modelPtr, base))
                return; // table contended; leave the model stock this frame

            const float t = static_cast<float>(GetTickCount() % 3600000u) * 0.001f;

            const WindBasis& wb = WindBasisFor(isBush ? t_bushBasis : t_windBasis, w);
            const float k1 = wb.k1, k2 = wb.k2;
            const float d1x = wb.d1x, d1y = wb.d1y;
            const float d2x = wb.d2x, d2y = wb.d2y;

            const float seed = Hash01(px, py) * kTwoPi;
            float var = w.variance;
            if (var < 0.0f) var = 0.0f;
            if (var > 1.0f) var = 1.0f;
            const float ampScale = 1.0f + (Hash01(py, px) - 0.5f) * 2.0f * var;

            float fade = 1.0f;
            if (w.distanceFade > 0.0f)
                fade = 1.0f / (1.0f + w.distanceFade * std::sqrt(dist2));

            const float phase1 = -k1 * w.speed * t;
            const float phase2 = -k2 * w.speed * 1.37f * t;
            const float gust   = 1.0f + w.gust * std::sin(t * 0.37f + seed * 0.5f);
            const float s1 = std::sin(k1 * (px * d1x + py * d1y) + phase1 + seed);
            const float s2 = std::sin(k2 * (px * d2x + py * d2y) + phase2 + seed * 0.6f);

            const float deg1 = (w.amplitudeDeg * s1 * gust + w.leanDeg) * ampScale * fade;
            const float deg2 = (w.crossAmplitudeDeg * s2 * gust) * ampScale * fade;

            const float tx = (deg1 * d1x + deg2 * d2x) * kDeg2Rad;
            const float ty = (deg1 * d1y + deg2 * d2y) * kDeg2Rad;

            float rot[9];
            BuildTiltRowMatrix(tx, ty, rot);

            // placement' = base * rot -- rotate the model about its own origin, translation preserved.
            for (int r = 0; r < 3; ++r)
            {
                const float b0 = base[r * 4 + 0];
                const float b1 = base[r * 4 + 1];
                const float b2 = base[r * 4 + 2];
                placement[r * 4 + 0] = b0 * rot[0] + b1 * rot[3] + b2 * rot[6];
                placement[r * 4 + 1] = b0 * rot[1] + b1 * rot[4] + b2 * rot[7];
                placement[r * 4 + 2] = b0 * rot[2] + b1 * rot[5] + b2 * rot[8];
            }

            g_installed = true;
        }

        // Pre-hook: tilt the instance before the engine derives its view root and bone palette from the
        // placement, so the palette the draw uploads carries the sway. Both palette entries share the
        // fastcall / 5-stack-arg / ret-0x14 shape (see offsets/game/M2.hpp).
        void __fastcall hkBuildBonePalette(void* instance, void* edx, void* a1, void* a2, void* a3,
                                           uint32_t a4, uint32_t a5)
        {
            ApplySway(instance);
            if (g_origFull) g_origFull(instance, edx, a1, a2, a3, a4, a5);
        }

        void __fastcall hkBuildBonePaletteSimple(void* instance, void* edx, void* a1, void* a2, void* a3,
                                                 uint32_t a4, uint32_t a5)
        {
            ApplySway(instance);
            if (g_origSimple) g_origSimple(instance, edx, a1, a2, a3, a4, a5);
        }
    }

    WindSettings&   Wind()    { return g_wind; }
    FilterSettings& Filter()  { return g_filter; }
    BushSettings&   Bush()    { return g_bush; }
    bool            Installed() { return g_installed; }

    bool InstallTreeWind()
    {
        if (!wxl_treewind::g_api) return false;

        LoadTreeWindConfig();

        if (!g_cacheReady)
        {
            InitializeCriticalSection(&g_cacheLock);
            g_cacheReady = true;
        }

        bool ok = false;
        if (wxl_treewind::HookAttachByName(wxl_treewind::kBuildPalettePoint, &hkBuildBonePalette, &g_origFull))
        {
            ok = true;
            WLOG_INFO("treewind: hooked %s", wxl_treewind::kBuildPalettePoint);
        }
        else
            WLOG_WARN("treewind: %s hook failed", wxl_treewind::kBuildPalettePoint);

        if (wxl_treewind::HookAttachByName(wxl_treewind::kBuildPaletteSimplePoint, &hkBuildBonePaletteSimple, &g_origSimple))
            WLOG_INFO("treewind: hooked %s", wxl_treewind::kBuildPaletteSimplePoint);
        else
            WLOG_WARN("treewind: %s hook failed", wxl_treewind::kBuildPaletteSimplePoint);

        // A map change retires every instance and can reuse their addresses, so the cache is dropped
        // at both boundaries rather than trusted across them.
        wxl_treewind::g_api->Subscribe(static_cast<uint32_t>(ev::Event::OnWorldLeave), &OnWorldChanged, nullptr);
        wxl_treewind::g_api->Subscribe(static_cast<uint32_t>(ev::Event::OnWorldEnter), &OnWorldChanged, nullptr);

        g_installed = ok;
        return ok;
    }

    void DrawPanel(const WXL_Api& api)
    {
        if (!api.UiCheckbox || !api.UiSliderFloat || !api.UiSeparator || !api.UiText ||
            !api.UiCollapsingHeader || !api.UiButton || !api.UiSameLine)
            return;

        int enabled = g_wind.enabled ? 1 : 0;
        if (api.UiCheckbox("Sway enabled", &enabled)) g_wind.enabled = enabled != 0;

        api.UiText(g_installed ? "status: swaying trees" : "status: waiting for a tree");
        api.UiSeparator();

        if (api.UiCollapsingHeader("Wind"))
        {
            api.UiSliderFloat("Direction (deg)", &g_wind.directionDeg, 0.0f, 360.0f);
            api.UiSliderFloat("Speed (yd/s)", &g_wind.speed, 0.0f, 12.0f);
            api.UiSliderFloat("Amplitude (deg)", &g_wind.amplitudeDeg, 0.0f, 6.0f);
            api.UiSliderFloat("Wavelength (yd)", &g_wind.wavelength, 2.0f, 80.0f);
            api.UiSliderFloat("Lean (deg)", &g_wind.leanDeg, 0.0f, 4.0f);
            api.UiSliderFloat("Cross amplitude (deg)", &g_wind.crossAmplitudeDeg, 0.0f, 3.0f);
            api.UiSliderFloat("Cross wavelength (yd)", &g_wind.crossWavelength, 2.0f, 40.0f);
            api.UiSliderFloat("Cross angle (deg)", &g_wind.crossAngleDeg, 0.0f, 180.0f);
            api.UiSliderFloat("Variance", &g_wind.variance, 0.0f, 1.0f);
            api.UiSliderFloat("Gust", &g_wind.gust, 0.0f, 1.0f);
            api.UiSliderFloat("Distance fade", &g_wind.distanceFade, 0.0f, 0.05f);
        }

        if (api.UiCollapsingHeader("What sways"))
        {
            int doodads = g_filter.doodadsOnly ? 1 : 0;
            if (api.UiCheckbox("Placed doodads only", &doodads)) g_filter.doodadsOnly = doodads != 0;
            int keywords = g_filter.matchKeywords ? 1 : 0;
            if (api.UiCheckbox("Match foliage names", &keywords)) g_filter.matchKeywords = keywords != 0;
            int tallThin = g_filter.matchTallThin ? 1 : 0;
            if (api.UiCheckbox("Match tall models", &tallThin)) g_filter.matchTallThin = tallThin != 0;
            int excludeDead = g_filter.excludeDead ? 1 : 0;
            if (api.UiCheckbox("Skip dead / bare trees", &excludeDead)) g_filter.excludeDead = excludeDead != 0;
            api.UiText("All enabled tests must pass (AND).");
            api.UiSliderFloat("Min height (yd)", &g_filter.minHeight, 0.0f, 40.0f);
            api.UiSliderFloat("Min aspect", &g_filter.minAspect, 0.5f, 6.0f);
            api.UiSliderFloat("Max height (yd, 0 = off)", &g_filter.maxHeight, 0.0f, 300.0f);
            api.UiSliderFloat("Max distance (yd)", &g_filter.maxDistance, 0.0f, 500.0f);
        }

        // Bushes are their own row: matched by name only, so the tree size / tall-thin gates above
        // never apply, and with an independent set of wind sliders.
        if (api.UiCollapsingHeader("Bushes"))
        {
            int bushEnabled = g_bush.wind.enabled ? 1 : 0;
            if (api.UiCheckbox("Bushes sway", &bushEnabled)) g_bush.wind.enabled = bushEnabled != 0;
            api.UiText("Matched by name only; the tree size filter never applies.");
            api.UiSliderFloat("Bush direction (deg)", &g_bush.wind.directionDeg, 0.0f, 360.0f);
            api.UiSliderFloat("Bush speed (yd/s)", &g_bush.wind.speed, 0.0f, 12.0f);
            api.UiSliderFloat("Bush amplitude (deg)", &g_bush.wind.amplitudeDeg, 0.0f, 6.0f);
            api.UiSliderFloat("Bush wavelength (yd)", &g_bush.wind.wavelength, 2.0f, 80.0f);
            api.UiSliderFloat("Bush lean (deg)", &g_bush.wind.leanDeg, 0.0f, 4.0f);
            api.UiSliderFloat("Bush cross amplitude (deg)", &g_bush.wind.crossAmplitudeDeg, 0.0f, 3.0f);
            api.UiSliderFloat("Bush cross wavelength (yd)", &g_bush.wind.crossWavelength, 2.0f, 40.0f);
            api.UiSliderFloat("Bush cross angle (deg)", &g_bush.wind.crossAngleDeg, 0.0f, 180.0f);
            api.UiSliderFloat("Bush variance", &g_bush.wind.variance, 0.0f, 1.0f);
            api.UiSliderFloat("Bush gust", &g_bush.wind.gust, 0.0f, 1.0f);
            api.UiSliderFloat("Bush distance fade", &g_bush.wind.distanceFade, 0.0f, 0.05f);
            int bushDoodads = g_bush.doodadsOnly ? 1 : 0;
            if (api.UiCheckbox("Bush: placed doodads only", &bushDoodads)) g_bush.doodadsOnly = bushDoodads != 0;
            int bushDead = g_bush.excludeDead ? 1 : 0;
            if (api.UiCheckbox("Bush: skip dead / bare", &bushDead)) g_bush.excludeDead = bushDead != 0;
            api.UiSliderFloat("Bush max distance (yd)", &g_bush.maxDistance, 0.0f, 500.0f);
        }

        api.UiSeparator();
        if (api.UiButton("Save to wxl-treewind.ini"))
            SaveTreeWindConfig();
        api.UiSameLine();
        if (api.UiButton("Revert"))
            RevertTreeWindConfig();
        api.UiText(TreeWindConfigHasUnsavedChanges() ? "Unsaved changes"
                                                    : "Matches wxl-treewind.ini (edited live)");
    }
}
