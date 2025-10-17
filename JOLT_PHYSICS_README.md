# Jolt Physics Integration

The `Simulation` class in `src/cubes/Simulation.cpp` now supports both ODE and Jolt Physics engines, selectable at compile time using a preprocessor define.

## Building with Different Physics Engines

### Using ODE (Default)

Build normally without any special options:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Using Jolt Physics

Enable the `USE_JOLT_PHYSICS` CMake option:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_JOLT_PHYSICS=ON
cmake --build build
```

The CMakeLists.txt includes an option that controls which physics engine to use:
- `USE_JOLT_PHYSICS=OFF` (default) - Uses ODE
- `USE_JOLT_PHYSICS=ON` - Uses Jolt Physics

## Implementation Details

The implementation uses `#ifdef USE_JOLT_PHYSICS` to conditionally compile either the Jolt Physics or ODE physics backend.

### Key Differences

**ODE Implementation:**
- Uses `dWorld`, `dSpace`, `dBody`, `dGeom` API
- Collision detection via `dSpaceCollide` with near callback
- Contact joints created manually in collision callback
- Parameters: ERP, CFM, contact surface layer, etc.

**Jolt Physics Implementation:**
- Uses `PhysicsSystem`, `BodyInterface`, `BodyID` API
- Collision detection handled automatically by physics system
- Contact management via `ContactListener` callbacks
- Different solver architecture (some ODE parameters don't have direct equivalents)

### Features Implemented

Both implementations support:
- Box rigid bodies with position, orientation, velocity
- Dynamic/sleeping body states
- Velocity clamping (linear and angular)
- Rest detection and automatic body deactivation
- Force and torque application
- Static infinite planes for ground/walls
- Configurable friction and elasticity
- Interaction pair tracking for networking

### Configuration Mapping

Some ODE configuration parameters map differently to Jolt:

| ODE Parameter | Jolt Equivalent | Notes |
|--------------|-----------------|-------|
| ERP | N/A | Jolt uses different constraint solver |
| CFM | N/A | Jolt uses different compliance model |
| MaxIterations | collision_steps | Passed to PhysicsSystem::Update |
| Gravity | SetGravity() | Direct mapping |
| Friction | mFriction | Direct mapping on bodies |
| Elasticity | mRestitution | Direct mapping on bodies |
| ContactSurfaceLayer | N/A | Jolt handles this differently |
| QuickStep | N/A | Jolt always uses optimized stepping |

## Performance Notes

- Jolt Physics is generally faster than ODE for large numbers of bodies
- Jolt uses multi-threading via JobSystem (configured for available CPU cores)
- Both implementations maintain determinism for networked physics
