# Headless Particles

A deterministic simulation of independent, unit-mass particles in a
100-by-100 box. It exercises repeated numeric updates and collection
transformations without graphics, file I/O, or external dependencies.

From the SDK or repository root:

```console
toy examples/particles/app
toy examples/particles/app -- 1000 100
```

Arguments are particle count and step count, defaulting to 200 and 100. Both
must be non-negative integers. The demo uses a timestep of 0.02 and vertical
acceleration of -9.81, then prints count, steps, position sums, and kinetic
energy. It does not print or retain every intermediate state.

## Model and Package

Import the `physics` directory relative to your Toy source file. The declared
package name is `particles`; a particle is a four-number vector `[x y vx vy]`.

| Word | Stack effect | Result |
| --- | --- | --- |
| `particles.seed` | `count -- particles` | Deterministic starting positions and velocities |
| `particles.step` | `particles dt gravity -- particles` | One updated population |
| `particles.advance` | `particles steps dt gravity -- particles` | Population after a fixed number of steps |
| `particles.summary` | `particles -- summary` | `[count sum-x sum-y kinetic-energy]` |

Each timestep first applies `vy += gravity * dt`, then advances position using
the new velocity. Coordinates are reflected elastically at 0 and 100. Multiple
wall crossings in one step are supported; exactly on a wall, velocity points
inward. With zero gravity, kinetic energy is conserved up to floating-point
rounding. This discrete model is not a continuous collision solver: it has
no particle-particle interactions, spatial index, or rendering, and with
gravity it does not promise exact conservation of mechanical energy.

`dt` must be finite and positive, and gravity finite. Particle records must
contain four finite numbers, initially within the box; choose magnitudes so
intermediate arithmetic and integer wall-tile counts remain representable.
The package validates parameters at the public entry point, not every field
on every update.

`map` expresses one new record per particle. `times` threads the evolving
population on the stack while timestep and gravity remain stable captures.
The gravity-induced velocity change is calculated once at the public entry
point, not once per particle.
The common case returns an in-bounds coordinate directly; reflection arithmetic
is only needed at or beyond a wall. Work is O(particles * steps), with O(particles)
live state when the caller does not retain history. Allocation traffic still
grows with the number of updates: this is a value-based simulation, not a
packed mutable numeric array.
