<p align="center">
  <img src="store/cover.png" alt="wxl-treewind" width="640">
</p>

# wxl-treewind

Makes the trees move.

Placed trees in the stock 3.3.5a client are completely rigid -- they never react to anything. This
module gives them the same bit of life the [grass module](../wxl-grasswind) gives ground cover:

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
path carries a foliage keyword and/or whose bounds are tall and narrow. Both tests, and every wind
knob, are exposed on the in-game overlay panel under **Tree Wind**.

## Tuning

The wind row covers direction, speed, amplitude, wavelength, a second cross-swell, a constant
downwind lean, per-tree variance, gust depth and distance fade. The filter row controls what counts as
a tree (path keywords / tall-and-narrow bounds, minimum height and aspect, maximum distance).

## Notes

- The tree's collision, selection and lighting are unchanged; only the visual transform sways.
- Some very wide, round trees may not pass the tall-and-narrow test on their own -- turn on
  **Match foliage names** (on by default) or lower **Min aspect** to catch them.
