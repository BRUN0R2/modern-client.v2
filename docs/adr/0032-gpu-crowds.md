# ADR-0032 — The GPU animates, culls and draws the crowds

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

The engine is meant to run thousands of entities. A test scene of 10,000 to 100,000 bodies of a player's model on
one floor (`crowdTick` of `nxBenchServer`, and the game itself on a stretched map) found where they stopped fitting
once the server's serial costs were gone.

Measured on 2026-11-03 (14 cores, 28 threads, RTX 4060, `next` windowed, uncapped). Each frame, on the CPU, every
body was found whether it may show, had its light found, had some thirty bones worked out and copied to memory the GPU
read over the bus, and took a bind group and a draw call of its own for each mesh. At 30,000 bodies a frame took 59 ms,
21 of them to record, and `next` used 70 % of the CPU and under 10 % of the GPU. Half of the workers' time went to
looking for work.

[05 §9](../05-rendering.md#9-studio-models) already plans instancing by model, mesh, skin and render mode, and compute
skinning; `nx.rhi` had no compute pipelines yet.

## Decision

**Models drawn alike take one draw.** `StudioRenderer` keeps the data of all a frame's models in one storage buffer that
the shader reads by instance, and draws each model, skin, body and facing once per mesh.

**The living bodies of a crowd are the GPU's** (`shaders/crowd.slang`, `StudioRenderer::recordCrowd`):

- As a model loads, the CPU works out each frame of each of its sequences by what `cs::poseBones` works them out with
  (`cs::framePosesOf`), and uploads them once, as a bone's position and quaternion.
- Once a tick, the CPU writes each body to mapped memory, in jobs: where its last move started and ended, how it turns,
  its sequence, its frame, and its light (found anew a tenth of a second apart, as often as a light style changes).
- Once a frame, a dispatch moves each body along its move, keeps those the frame may show (the view's planes and the
  map's tree against the visible leaves, as `BspWorld::mayShow`), and places each bone as `cs::poseBones` does: the
  frame estimated as `R_StudioEstimateFrame`, between two frames as `QuaternionSlerp`, in its parent. Another dispatch
  writes how many instances each mesh's indirect draw has.

A frame's CPU work does not grow with the crowd but for the bodies written once a tick. A body's corpse stays on the CPU:
there are few, and each fades, blended and sorted. Players stay there too: their legs and their aim blend sequences.

**What is far from every player moves and thinks less often**, as the original's monsters move at their thinks, a tenth
of a second apart, which its clients interpolate: within 1024 units of a player each tick, then every second, fourth and
eighth tick, in a step that takes the body as far; and zombies at each of their turns within 2048, past the farthest
noise they hear, then at each second, fourth and eighth. Near a player nothing changes.

**The server's rules stay on the CPU.** Collision and the agents' decisions are deterministic game logic over the map's
hulls and navigation mesh, which a client's GPU does not hold for a server, and which tests prove the same on any number
of workers.

## Alternatives considered

- **Animation baked to whole frames in a texture, read by the vertex shader.** Quicker on the GPU, but a body shows
  between frames only by blending matrices, which shrinks limbs, or by rounding to a frame, which steps. The compute
  shader plays as the original plays at any distance, and costs well under a millisecond for what the GPU has at most.
- **A coarser grid for what may show instead of the map's tree.** Simpler for the shader, but either hides what the
  tree shows or shows more; walking the tree gives what `mayShow` gives.
- **Keeping the poses on the CPU and only instancing the draws.** It removes the draw calls, not the cost that grows
  fastest, the poses, which also reach the GPU through the bus.
- **Moving the movement and the decisions to the GPU.** See above: they are the server's. With nothing else in the way
  they are what is left: about 16 ms a tick at 100,000 bodies in the same machine.

## Consequences

- At 10,000 bodies, 55 frames a second to about 500; at 30,000, 17 at 85 % of the game's speed to 157 at full speed; at
  100,000, 12 at 24 % to 21 at 58 %. A hundred thousand needs less than the 10 ms a tick that is left of a thread that
  also records frames: a simulation on a thread of its own is the next step.
- Each body takes 96 bytes a tick, and each model some megabytes of poses, in the GPU's memory.
- `nx.rhi` has compute pipelines, dispatches and indirect draws, and a device must draw from an instance indirectly
  (`drawIndirectFirstInstance`, which every Direct3D 11 card has).
- Two paths draw a studio model. A test holds them together: a body the GPU poses draws the same pixels as the same model
  the CPU poses (`ACrowdIsDrawnAsTheCpuDrawsTheModelsPosedTheSameWay`), and `framePosesOf` is what `poseBones` gives
  (`TheFramesOfASequenceAreWhatItsPosesWorkOut`).
- The original's light of a body holds a tenth of a second, and a body far away shows up to 80 ms behind where the
  server has it, within what a client's interpolation already shows.
