# RealWaterSimulator — Presentation V2 (5-min talk + defense)

A script for a 5-minute talk plus the answers to back it up. Each topic gives a plain
explanation, the line to actually say, and the exact code that proves it (`file:line`)
for when the panel drills in. You can present off the **Say** lines alone; the citations
are the safety net.

Companion docs: `TECHNICAL_OVERVIEW.md` (full prose), `PRESENTATION.md` (20-slide deck).
Reference figure: **~112 FPS on an RTX 3090** at 1024×768 (our measured number).

---

## The project in three sentences

We build a realistic ocean by summing thousands of small waves on the GPU every frame.
We light it like film — reflections, sun sparkle, foam — and float boats on it that bob,
tilt, and leave wakes. It all runs live and interactive, over 100 FPS, with sliders for
weather, sun, and rain.

Everything below is detail and proof.

---

# Part 1 — The 5-minute run-of-show

Seven beats, about 4:45. The ocean is the star; everything else proves the ocean is real
and usable. Talk to the idea, not the code.

| # | Beat | Time | The one thing to land |
|---|------|------|------------------------|
| 1 | Title + the three sentences | 0:30 | Thousands of real waves summed on the GPU every frame, lit like film, with boats that really float — live at 100+ FPS. |
| 2 | The big idea | 0:45 | An ocean is just many simple waves added together. We get the mix from real ocean science and rebuild the surface every frame. |
| 3 | How the GPU builds the waves | 1:15 | A five-step pipeline on the card. Two clever bits: an FFT method that scales to 512×512, and foam that appears exactly where waves break. |
| 4 | Why it looks good | 0:45 | One rule — mirror-like at grazing angles, dark head-on (Fresnel) — plus sky reflection, sun sparkle, foam. |
| 5 | Boats that live in it | 0:45 | The water lifts and tilts the boats; the boats push back and leave wakes. Two-way interaction. |
| 6 | Making it fast | 0:45 | Three tricks: the FFT method, a no-stall height readback, and rain that runs entirely on the GPU. |
| 7 | "Demo time" | 0:10 | Stop the slides, drive the boat. |

Leave out of the five minutes (save for questions): how models load, the skybox, where the
cliffs sit, the full slider list. Good answers if asked, clutter if presented.

---

# Part 2 — The defense, with proof

The whole thing runs from [src/main.cpp](src/main.cpp). Each frame is a checklist: update the
water, move the boats, read the water height back, float the boats, draw. In code that's the
physics loop ([main.cpp:549-668](src/main.cpp#L549-L668)) → rain
([main.cpp:673](src/main.cpp#L673)) → height readback ([main.cpp:678](src/main.cpp#L678)) →
buoyancy ([main.cpp:683-691](src/main.cpp#L683-L691)) → render
([main.cpp:783-903](src/main.cpp#L783-L903)).

---

## Beats 2 & 3 — The ocean

A real ocean surface is the sum of thousands of individual waves, each a different size and
direction. We pick which waves exist, and how big, from a real wind-wave formula, then add
them all up into a height field every frame. The tool that sums thousands of waves efficiently
is the **FFT** (Fast Fourier Transform): give it the mix of wave sizes, it returns the actual
bumpy surface.

**Say:** "The sea is the sum of thousands of waves. We get the mix of wave sizes from a real
wind-wave formula, then an inverse FFT turns that mix into the surface — every frame, on the
GPU. Everything else reads off this surface."

The pipeline is five stages (point at the diagram):

```
1. RECIPE      2. CLOCK        3. MIX          4. FFT            5. RESULT
which waves    advance each    combine into    add them all      height map +
exist, how     wave a tick     this frame's    up into a real    surface tilt +
big            in time         wave field      surface           foam
```

**Implementation.** Driver: [src/ocean/GPUFFTOcean.cpp](src/ocean/GPUFFTOcean.cpp), created at
[main.cpp:207](src/main.cpp#L207) at resolution **512** over a **1024 m** patch
([main.cpp:81,85-86](src/main.cpp#L81)). The per-frame run is `update()`
([GPUFFTOcean.cpp:206-318](src/ocean/GPUFFTOcean.cpp#L206-L318)):

1. **Recipe** (the spectrum) — which waves exist and how tall, built once at
   [GPUFFTOcean.cpp:166-182](src/ocean/GPUFFTOcean.cpp#L166-L182) from the real
   **Elfouhaily/IFREMER** ocean formula
   ([fft_initial_spectrum.comp:49-77](assets/shaders/fft_initial_spectrum.comp#L49-L77)).
   Bigger wind → bigger, longer waves.
2. **Clock** — each wave advances at its own speed
   ([fft_phase.comp:37](assets/shaders/fft_phase.comp#L37)). This is what animates it.
3. **Mix** — combine recipe and time into this frame's wave field, and add the sideways
   choppiness push ([fft_spectrum.comp:35-54](assets/shaders/fft_spectrum.comp#L35-L54)).
4. **FFT** — sum all the waves into a real surface, rows then columns
   ([GPUFFTOcean.cpp:245-291](src/ocean/GPUFFTOcean.cpp#L245-L291)).
5. **Result** — pack into a height map
   ([fft_displacement.comp](assets/shaders/fft_displacement.comp#L19-L28)) and compute the
   surface tilt and where it foams
   ([fft_normal.comp](assets/shaders/fft_normal.comp#L27-L51)). It's drawn on a large flat grid
   bent into shape per-vertex ([standard.vert:30,39](assets/shaders/standard.vert#L30-L39)).

### The two parts worth highlighting

**The FFT method (Stockham).** The straightforward way to run an FFT on a GPU loads a whole row
into a small on-chip scratchpad and does every step there — which runs out of room past about
256 points. Stockham instead does one step per dispatch, bouncing the data between two textures.
No scratchpad limit, so we run a full **512×512** (the code supports up to 1024). The doubling-step
loop is [GPUFFTOcean.cpp:252-264](src/ocean/GPUFFTOcean.cpp#L252-L264), the butterfly math is
[fft_horizontal.comp:33-64](assets/shaders/fft_horizontal.comp#L33-L64), and the reasoning is
written into the code at [fft_horizontal.comp:1-15](assets/shaders/fft_horizontal.comp#L1-L15).

**Foam where waves break.** When a wave grows steep enough to fold over itself, that's a breaking
crest. One number — the **Jacobian** of the horizontal displacement — goes negative exactly there.
We watch for it and paint foam, so whitecaps are automatic and physically placed rather than faked.
Computed at [fft_normal.comp:43-49](assets/shaders/fft_normal.comp#L43-L49), rendered at
[standard.frag:200-203](assets/shaders/standard.frag#L200-L203).

### Likely questions
- **What's the wave recipe?** A real oceanography model (Elfouhaily/IFREMER): long swells plus tiny
  capillary ripples, biased along the wind
  ([fft_initial_spectrum.comp:49-76](assets/shaders/fft_initial_spectrum.comp#L49-L76)).
- **What do the sliders do?** Wind speed, direction, sea maturity, and overall energy change the
  recipe and rebuild it ([GPUFFTOcean.cpp:184-204](src/ocean/GPUFFTOcean.cpp#L184-L204)); wave
  height, choppiness, and time scale are applied afterward with no rebuild.
- **Why a separate choppiness?** Plain height gives round swells; choppiness nudges points sideways
  toward the crests to sharpen them into realistic peaks
  ([fft_spectrum.comp:50-51](assets/shaders/fft_spectrum.comp#L50-L51)).

---

## Beat 4 — Why the water looks good

Water has one signature: look straight down and it's dark and see-through; look across it toward
the horizon and it's a bright mirror. That rule is **Fresnel**. We blend a dark see-through body
colour with a bright sky reflection by that rule, add sun sparkle and foam, and do the math in HDR
(extra bright) then dim at the end — which gives the crisp, photographic contrast.

**Say:** "The look is one rule — mirror-like at grazing angles, dark head-on. We blend a sky
reflection with a depth-tinted body colour, add sun glints and foam, and tonemap it like a photo."

**Implementation** (all in [standard.frag](assets/shaders/standard.frag)): the Fresnel blend
([:156](assets/shaders/standard.frag#L156)); the reflection is the sky cube bounced off the wave,
no reflection cameras ([:162-167](assets/shaders/standard.frag#L162-L167)); the body colour runs
dark navy in the dips to teal at the crests ([:171-176](assets/shaders/standard.frag#L171-L176));
glow through backlit waves ([:182-185](assets/shaders/standard.frag#L182-L185)); sun sparkle, a
tight glint plus a broad shimmer ([:195-197](assets/shaders/standard.frag#L195-L197)); foam, haze,
and the tonemap ([:200-211](assets/shaders/standard.frag#L200-L211)).

**Likely questions.** Real reflections? They reflect the sky, sampled per-pixel — cheap and stable,
and reused to light the boats and rocks ([object.frag:64-65](assets/shaders/object.frag#L64-L65)).
The sun is a direction set by two sliders ([main.cpp:828-831](src/main.cpp#L828-L831)).

---

## Beat 5 — Boats that live in the water

It's a two-way exchange. Water to boat: we check the wave height under 15 points on the hull and
let the boat ride up, pitch, and roll to match. Boat to water: a moving boat keeps dropping small
ripples behind its stern, which spread into a wake. Long ships ride smoothly because they span many
waves and average out the chop; the small jet-ski feels every bump — same as reality.

**Say:** "Boats and water interact both ways. Each boat samples the wave height under its hull and
solves how high it floats and how it tilts. Every moving boat injects ripples at its stern into a
second water simulation, which become its wake."

**Water lifting and tilting the boat** ([src/water/BoatPhysics.cpp](src/water/BoatPhysics.cpp), run
each frame at [main.cpp:683-691](src/main.cpp#L683-L691)): we sample the water height under 15 points
(5×3) on the hull ([BoatPhysics.cpp:45-57](src/water/BoatPhysics.cpp#L45-L57)); the float height
follows their average on a soft spring so it never jitters or sinks
([BoatPhysics.cpp:60-65](src/water/BoatPhysics.cpp#L60-L65)); tilt follows the slope across the hull
— higher water at the bow lifts the nose ([BoatPhysics.cpp:67-79](src/water/BoatPhysics.cpp#L67-L79))
— capped so it can't flip ([BoatPhysics.cpp:82-83](src/water/BoatPhysics.cpp#L82-L83)). Averaging is
the clever part: a long ship spans many waves so they cancel, while the short jet-ski bobs lively.

**The wake** ([main.cpp:613-639](src/main.cpp#L613-L639)): each moving boat drops a ripple behind its
stern, sized to the boat. Those go into a second, separate water sim — a classic pond-ripple (wave
equation) simulation on a 256×256 grid ([src/ocean/GPUDisturbance.cpp](src/ocean/GPUDisturbance.cpp),
math at [disturbance_propagate.comp:24-30](assets/shaders/disturbance_propagate.comp#L24-L30)). The
**C key** drops a ripple too, for an interactive splash
([main.cpp:484-490](src/main.cpp#L484-L490)). The water surface adds these on top of the FFT waves
([standard.vert:43](assets/shaders/standard.vert#L43),
[standard.frag:69-80](assets/shaders/standard.frag#L69-L80)).

**Likely questions.** Two water systems because the FFT is the big open-ocean swell and the ripple
sim is for local effects the FFT can't make — wakes and splashes — layered on top. The boat code gets
the wave height from the GPU via a no-stall readback (next beat). Steering is simple flocking — stay
in bounds, avoid each other, avoid the rock — with a hard backstop so boats never collide or clip the
cliffs ([main.cpp:561-666](src/main.cpp#L561-L666)).

---

## Beat 6 — Making it fast

Three bottlenecks, three fixes. The FFT method already let us go big. The boats need the water height
on the CPU, but demanding it immediately freezes the pipeline — so we read last frame's height, which
is ready instantly and close enough. Rain could be tens of thousands of drops, so instead of the CPU
moving each one, the GPU moves them all at once and draws them with no per-drop work.

**Say:** "Three decisions bought real-time: the FFT method, a no-stall 'read last frame's height' trick
for the boats, and rain that lives entirely on the GPU — one update, two draws, for 120,000 drops."

**No-stall height read (double-buffered PBO):** start copying this frame's height map and immediately
read last frame's, which is already done
([GPUFFTOcean.cpp:320-356](src/ocean/GPUFFTOcean.cpp#L320-L356)). The naive "read now" version dropped
us to ~55 FPS; this is nearly free, costing only heights that are one frame old (invisible for floating).

**GPU rain:** all drops live in one GPU buffer; one compute shader moves every drop each frame
([GPURain.cpp:47-62](src/water/GPURain.cpp#L47-L62) →
[rain_update.comp](assets/shaders/rain_update.comp#L39-L72)); two draw calls render streaks and splashes
straight from that buffer with no vertex uploads
([GPURain.cpp:96-114](src/water/GPURain.cpp#L96-L114)). As a bonus, far-away rain ripples are skipped
with a cheap distance check before the expensive math
([standard.frag:94-138](assets/shaders/standard.frag#L94-L138)) — this fixed stuttering on weaker laptops.

**Likely questions.** CPU time goes almost nowhere — boat steering, 15-point buoyancy per boat, and the
one quick height copy; the heavy work is all on the GPU. Physics is fixed at 1/60s so it behaves the same
regardless of frame rate ([main.cpp:549-551](src/main.cpp#L549-L551)); rain is visual, so it updates once
per frame.

---

## Appendix A — Backup answers (not in the five minutes)

**Loading the 3D models (boats, rocks, cliffs).** They're **glTF** files — effectively the JPEG of 3D
models. A model is built from many parts, each with its own position; we walk that tree and bake every
part into final world coordinates, or a multi-part ship would collapse to a pile at the origin. Loader:
[src/graphics/Model.cpp](src/graphics/Model.cpp) (cgltf) — walk the parts
([:187-198](src/graphics/Model.cpp#L187-L198)), bake transforms into vertices
([:143-153](src/graphics/Model.cpp#L143-L153)); the yacht is 23 parts, the ship 62
([:118-119](src/graphics/Model.cpp#L118-L119)). Surfaces use the model's own colour/normal/roughness
maps ([:168-183](src/graphics/Model.cpp#L168-L183)), with a trick to get crisp surface bumps without
extra tangent data ([object.frag:27-42](assets/shaders/object.frag#L27-L42)).

**The sky.** A large cube with a sky photo on each inside face, drawn so it always sits infinitely far
behind everything; the same photo is reused as the water's reflection. Loaded at
[main.cpp:40-76](src/main.cpp#L40-L76), drawn behind everything at
[main.cpp:883-896](src/main.cpp#L883-L896) via the depth=1 trick
([skybox.vert:12](assets/shaders/skybox.vert#L12)). Four skies, switchable live.

**The water grid.** About 2 million triangles (a 1025×1025 grid), 1 m between points, 1024 m across
([OceanMesh.cpp:7-56](src/ocean/OceanMesh.cpp#L7-L56)).

---

## Appendix B — Be honest about these

- **112 FPS** is our number on an RTX 3090 — say "on our test machine," not a guarantee.
- The water grid is **1025×1025** points — say "about 2 million triangles."
- The **K/L keys** only nudge wind in a narrow range; the **panel slider** has the full range
  ([main.cpp:463,721](src/main.cpp#L463)). Demo wind from the slider to show calm→storm.
- Foam and wake-churn are shader effects, not particle systems.
- The ocean grid is a fixed size — no level-of-detail. If asked how you'd scale it, that's the honest
  next step.

---

# Part 3 — The live demo (after the five minutes)

Have it already running before you talk. About 90 seconds, building to a finish. Sliders are in the
panel ([main.cpp:693-781](src/main.cpp#L693-L781)).

1. **Start calm.** Left-click to take control, fly low to the water (WASD + mouse) so the waves have
   scale. Boats cruising.
2. **Drag `wind speed` up** (Water panel) — calm turns to storm. The best moment: "I'm changing the wave
   recipe; everything else is the same FFT." Watch whitecaps appear as crests start to break.
3. **Sweep the sun** low to the horizon (`sun azimuth`/`elevation`) to light up the sparkle.
4. **Press `C`** for a splash; point out a boat's wake and how it tilts on the bigger swell you just made.
5. **Crank `spawn rate`** — rain, splashes, ripples. "120,000 drops, all on the GPU."
6. **Pull back** for a wide shot of the whole bay. End there.

Safety net: press `R` beforehand to record a backup clip
([main.cpp:523-531](src/main.cpp#L523-L531)) in case the projector or GPU misbehaves. If a slider looks
wrong, reset to roughly wind 15, wave height 1, choppiness 1.5 (the startup values).
