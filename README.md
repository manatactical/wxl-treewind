
# wxl-treewind

Makes the trees move.

Placed trees in the stock 3.3.5a client are completely rigid -- they never react to anything. This
module gives them the same bit of life the grass wind module gives ground cover:

- they sway gently in the wind, leaning downwind and drifting back;
- nearby trees share a travelling wind field, so a breeze visibly crosses a forest;
- every tree gets a slightly different phase and amplitude, so nothing sways in lockstep.

Purely visual (no gameplay impact). The sway is applied per instance on the way into the bone palette,
so there is no second draw pass and no measurable cost.

## How it works

The client fills a per-instance bone palette from the current pose each frame and then uploads it for
skinning. This module detours that build (`M2.BuildBonePalette`, plus the single-bone fast path) and,
before the engine derives its view root, rewrites the instance's world (`placement`) matrix with a
small rotation about the model origin. Because the origin of a tree model sits at the trunk base, the
base stays planted and the crown leans -- a whole-tree rigid tilt rather than per-branch movement,
kept to a fraction of a degree so it reads as a breeze instead of an earthquake.

Trees are recognised as map-placed static doodads (never units, weapons or spell effects) whose model
path carries a foliage keyword. By default the foliage name alone decides, so a tall rock spire or
ruin can never qualify. An optional tall-and-narrow bounds test (off by default) can be enabled as a
second required gate, and an optional maximum height leaves oversized trees rigid. Every test, and
every wind knob, is exposed on the in-game overlay panel under **Tree Wind**.

## Tuning

The wind row covers direction, speed, amplitude, wavelength, a second cross-swell, a constant
downwind lean, per-tree variance, gust depth and distance fade. The filter row controls what counts as
a tree (path keywords / tall-and-narrow bounds, minimum height and aspect, maximum height and
distance).

Every knob is also in `wxl-treewind.ini` next to the DLL, so the sway can be retuned from a text
editor without the overlay. The file is read live: save a change and the module picks it up within
about a second, no restart. The panel's **Save** button writes the current slider values back to the
file; **Revert** discards unsaved edits. If the file is missing it is written with the defaults on
the first load.

## Notes

- The tree's collision, selection and lighting are unchanged; only the visual transform sways.
- Every enabled filter must pass. The foliage-name test is the only gate on by default; enable
  **Match tall models** to also require tree-like bounds, or keep it off if a real tree is missed.
- **Max height** (world yards, default 125, 0 = unlimited) skips trees taller than the value, so giant
  world-tree models keep their stock pose instead of leaning their canopy across the zone.
