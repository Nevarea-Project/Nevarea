# Nevarea (Rust) — Public API Design

**Status:** design-only. No code until this reads clean.
**Decided 2026-08-11.** C++ Nevarea frozen and archived; R0.2/R0.3 not merged.

---

## 0. Decisions already made

| Decision | Choice |
|---|---|
| Language | Rust, clean-slate rewrite (not a port) |
| Backend bindings | `ash` (raw Vulkan). Not vulkano (stale, Vulkan-only), not wgpu-hal (someone else's abstraction) |
| Backends | Vulkan only — but API **shaped** so D3D12/Metal could slot in later |
| Resource model | RAII: owning type, `Drop` enqueues GPU-serial retirement. Raw handle stays reachable |
| Plugins | **None.** Nevarea is a substrate you are *expected* to wrap and abstract |
| Process | This document first. Code second |

### The gap statement

Not "no Rust RHI exists" — `ash`, `vulkano`, `wgpu` all exist. The real gap:

> wgpu implements **WebGPU**, a portability spec that **forbids buffer device addresses** (browser sandbox), can't expose timeline semaphores, and only partially does bindless. Nevarea's doctrine *requires* handing the user the raw device address. **wgpu can never be Nevarea — by spec, not by maturity.**
>
> Separately: wgpu has no DLSS/FSR/XeSS/NRD and structurally can't — DLSS needs extensions enabled *before* device creation, raw handles, and swapchain control for frame-gen. wgpu owns all three. Only reachable via the unstable `wgpu_hal::as_hal` backdoor.
>
> Nevarea's differentiator: **the escape hatch is a first-class, stable, documented feature — not an unstable backdoor.**

Nevarea itself provides **no** DLSS, no NRD, no upscaling. Those are host-side app features built *on* the escape hatch.

---

## 1. Doctrine (unchanged, language-independent)

Nevarea handles annoying **SETUP** and silently auto-picks the best internal **STRATEGY**. Beyond that it does **NOTHING** for you. A *wrapper*, not a *manager*.

1. **SETUP — Nevarea does it:** instance/adapter/device/queues, allocator, bindless infrastructure, sync primitives, frame lifecycle, per-resource creation boilerplate, staging uploads.
2. **STRATEGY — auto-picked, invisible, no semantic effect:** descriptor model, memory placement, internal sync strategy, present timing.
3. **WORK — the user does it:** geometry, buffer contents, draw submission, shaders, scene, materials, culling. Nevarea hands over the irreducible primitive and gets out of the way.

### 1.1 Profile contract — what Nevarea IS

> **Nevarea has no binding model.** Buffers are `u64` device addresses. Textures are `u32` bindless indices.
> There is **no** API to bind a resource to a slot, **no** vertex input state, **no** descriptor sets, **no** push descriptors.

BDA + bindless only. This is the identity, not a feature — it is why Nevarea is tiny, and it is where
extensibility comes from: **extensibility by absence.** There is no binding model to fight, so any engine's
data layout works. A stronger claim than a plugin system, at zero code.

### 1.1b THE PORTABILITY RULE — model the more dynamic shape

> **Always model the more dynamic / more granular API shape.**
> **Dynamic lowers to static. Static cannot be raised. Granular lowers to coarse. Coarse cannot be refined.**

Discovered by accident: shader-object-only was picked for one-path cleanliness, and turned out to be the
*only* shape that ports. D3D12 and Metal have no shader objects — but because every render state is dynamic,
those backends cache PSOs keyed on `(ShaderSet, RenderState, formats)` and materialize them lazily, with no
change to the public API. A pipeline-shaped API could never have been raised to shader objects.
Predicts the same for sync2-style granular barriers lowering to D3D12's coarser model.

**That ruling is now load-bearing for D3D12/Metal. Do not revisit it casually.**

### 1.1c The cost of supporting three APIs — where the hassle goes

A proprietary RHI's real advantage is **not** "quick `if` statements" — it is that it only supports *its own
engine's usage patterns*, so 90% of each API can stay unimplemented. A general RHI must handle every
combination someone might ask for.

**Nevarea absorbs that hassle deliberately. That IS the product.** The implementation should be full of
backend-specific branches; doctrine only forbids leaking them into the *public surface*.

The hassle is not uniform — it splits three ways, and only the third is a real tax:

1. **Spelling differences** — cheap, invisible. PSO caching; suspend/resume vs `MTLParallelRenderCommandEncoder`.
2. **Bounded semantic gaps** — absorbable at a known cost. Queue ownership (no-op elsewhere), D3D12 explicit
   residency, Metal purgeable state.
3. **Genuinely irreconcilable behaviour** — no `if` fixes it. Only two honest outlets: lowest-common-denominator,
   or `Support` capability query + the `nevarea-raw` escape hatch.

**Minimize category 3 deliberately; never discover it.** Both outlets already exist by design.

### 1.2 THE ABSTRACTION TEST — strategy or primitive?

Resolves "should Nevarea expose X?" for every backend capability:

> **Does enabling it change what the USER writes — shaders, draw calls, or data layout?**
> - **No** → **STRATEGY** (bucket 2) → auto-pick per-HW, hide it completely, never configurable.
> - **Yes** → new **PRIMITIVE** (bucket 3) → expose it, capability-gate it, never decide for them.

| Capability | Changes user code? | Verdict |
|---|---|---|
| Descriptor model (sets / heaps) | **No** — same shader, same `u32` slot regardless of backend path | **STRATEGY → hide** |
| Mesh shaders | **Yes** — task+mesh stages, different draw call | **PRIMITIVE → expose**, gated |
| Ray tracing / ray query | **Yes** — user writes RT shaders | **PRIMITIVE → expose** |
| Variable rate shading | **Yes** — user picks rates | **PRIMITIVE → expose** |
| Async compute | **Yes** — changes their sync | **PRIMITIVE → expose** (`QueueKind`) |
| Dynamic rendering vs legacy | No | **STRATEGY → hide** |
| Memory placement, pipeline cache, present timing | No | **STRATEGY → hide** |

**Descriptor model — decided, single path, no options (2026-08-11):**

- **Now: descriptor SETS.** One path. Sets + `VK_EXT_descriptor_indexing` for bindless.
- **`VK_EXT_descriptor_buffer` is NOT supported and never will be.** Heaps exist specifically to fix descriptor
  buffers' problems — supporting buffers means inheriting a known-bad design *and* carrying a dual path forever.
- **Later: migrate wholesale to descriptor HEAPS when mature.** Permanent switch, no fallback, no dual path,
  no user-facing option at any point.

This also settles the C++ **R6 honesty problem**: the old pitch "first RHI to support Descriptor Heaps and
Buffers" was FALSE (detected but never used). Fix by **not making the claim** rather than by building the path.

**THE REDUNDANCY TEST** — for every proposed item:
> Can the user already achieve this with OTHER *native Nevarea* functions (not raw Vulkan)?
> **YES → redundant → CUT.** **NO → KEEP**, even if it does heavy boilerplate.

Two hard overrides: dead/unused → cut. Hides a value the user owns, or locks a low-level capability → expose it / make it runtime.

**Non-negotiable:** raw extension request/query, feature-chain control, and the raw escape hatch. Nothing Nevarea sets up may lock the user out of changing it.

---

## 2. Why the rewrite is cheap: R0–R7 becomes free

The C++ audit findings are now the design spec, not a work queue.

| R-item | C++ = retrofit | Rust, designed from scratch |
|---|---|---|
| R0 persistent validation / NvResult | the frozen PR | **Free.** `Result` + `?`. The propagation gap is a compile error |
| R1 GPU-serial retirement | deferred-destroy off-by-one | Not free, but `Drop` centralises it to *one* place |
| R2 per-subresource state | one layout per whole image | **Free if designed now.** VK sync2 / D3D12 enhanced barriers / Metal converged |
| R3 draw-order preservation | bucket-by-pipeline reorders | **Free.** Don't write the bucketing |
| R4 feature-struct queries | extension-name sniffing | **Free.** Query the feature chain properly, once |
| R5 object graph | invert everything | **Free.** Build it this way from line one |
| R6 true C ABI + descriptor heaps | mangled C++ exports; heaps detected-but-unused | **Free.** `extern "C"` is a real C ABI |
| R7 headless test suite | needs restructure first | **Free.** Headless is the default; `cargo test` |

---

## 3. Object graph (R5 shape)

```
Instance ──> Adapter ──> Device ──> Queue ──> CommandList
                            └────> Surface ──> Swapchain    (optional)
```

Swapchain hangs off the side, never in the middle. Consequences: headless is the *default*, viewport is not swapchain-derived, and the test suite needs no window.

---

## 4. Public API surface (draft)

> **The complete surface now lives in `NEVAREA_RS_API.md`.** This document keeps the *why*; that one is the
> *what* and becomes `lib.rs`. Sections below are the original sketches plus the rationale sections
> (§4.9 lifetime, §4.9b the BDA lifetime hole, §4.10 primitive inventory, §4.11 swapchain sync,
> §4.12 multithreading) — those remain authoritative for reasoning and are referenced from the API file.

Signatures are illustrative, not final. `Result<T> = core::result::Result<T, nevarea::Error>`.

### 4.1 Entry

```rust
pub struct InstanceDescription<'a> {
    pub app_name: &'a str,
    pub validation: bool,
    /// Raw extension request — doctrine override, never gated.
    pub extensions: &'a [&'a CStr],
    /// Raw layer request — same doctrine override. Lets the user force or suppress
    /// layers Nevarea would otherwise auto-select, e.g. VK_LAYER_KHRONOS_shader_object (§4.10).
    pub layers: &'a [&'a CStr],
}

impl Instance {
    pub fn new(desc: &InstanceDescription) -> Result<Instance>;
    pub fn adapters(&self) -> Vec<Adapter>;
}
```

### 4.2 Adapter — capability truth (R4)

Capabilities are queried from **feature structs**, never from extension-name presence.

```rust
impl Adapter {
    pub fn info(&self) -> AdapterInfo;              // name, vendor, device type, limits
    pub fn supports(&self, f: Feature) -> bool;     // real feature-struct query
    pub fn create_device(&self, desc: &DeviceDescription) -> Result<Device>;
}

pub struct DeviceDescription<'a> {
    pub queues: &'a [QueueRequest],
    pub extensions: &'a [&'a CStr],
    /// Raw `pNext` injection at device creation — the ONE hook an
    /// after-the-fact escape hatch structurally cannot provide.
    /// This is what makes host-side DLSS/NRD possible.
    pub feature_chain: Option<&'a mut dyn FeatureChain>,
}
```

### 4.3 Device + resources (RAII, handle reachable)

```rust
impl Device {
    pub fn queue(&self, kind: QueueKind) -> Option<Queue>;
    pub fn create_buffer(&self, desc: &BufferDescription) -> Result<Buffer>;
    pub fn create_image(&self, desc: &ImageDescription) -> Result<Image>;
    pub fn create_sampler(&self, desc: &SamplerDescription) -> Result<Sampler>;
    pub fn create_pipeline(&self, desc: &PipelineDescription) -> Result<Pipeline>;
    pub fn create_acceleration_structure(&self, desc: &AccelDescription) -> Result<AccelStruct>;
    pub fn wait_idle(&self) -> Result<()>;
}

pub struct Buffer { /* Arc<DeviceInner> + BufferHandle */ }

impl Buffer {
    pub fn device_address(&self) -> u64;   // doctrine: the user owns this value
    pub fn bindless_index(&self) -> u32;
    pub fn size(&self) -> u64;

    // --- reachable primitive: an engine wrapping Nevarea takes lifetime back
    pub fn handle(&self) -> BufferHandle;              // borrow, still RAII-owned
    pub fn into_raw(self) -> BufferHandle;             // forget Drop, caller owns it now
    pub unsafe fn from_raw(d: &Device, h: BufferHandle) -> Buffer;  // re-adopt
}

impl Drop for Buffer { /* enqueue GPU-serial retirement — R1, one place */ }
```

`Image`, `Sampler`, `Pipeline`, `AccelStruct` follow the identical shape.

### 4.4 Commands — order preserved by construction (R3)

```rust
impl CommandList {
    pub fn begin_rendering(&mut self, d: &RenderingDescription) -> RenderPass<'_>;
    pub fn barrier(&mut self, buf: &[BufferBarrier], img: &[ImageBarrier]);  // R2, explicit
    pub fn dispatch(&mut self, x: u32, y: u32, z: u32);
    pub fn copy_buffer(&mut self, src: &Buffer, dst: &Buffer, regions: &[BufferCopy]);
}

pub struct RenderPass<'a>;  // Drop ends rendering
impl RenderPass<'_> {
    pub fn bind_pipeline(&mut self, p: &Pipeline);
    pub fn set_viewport(&mut self, vp: Viewport);       // NOT swapchain-derived
    pub fn push_constants(&mut self, data: &[u8]);
    pub fn draw(&mut self, ..);
    pub fn draw_indexed(&mut self, ..);
    pub fn draw_indirect(&mut self, ..);
}
```

Commands execute in recording order. No bucketing, no sorting, no reordering — ever. That is the user's job.

### 4.5 Sync — timeline everywhere

Portable across all three targets: VK timeline semaphore / `ID3D12Fence` / `MTLSharedEvent`.

```rust
impl Timeline {
    pub fn value(&self) -> u64;
    pub fn wait(&self, value: u64, timeout: Duration) -> Result<()>;
    pub fn signal(&self, value: u64) -> Result<()>;
}

impl Queue {
    pub fn submit(
        &self,
        lists:   &[CommandList],
        waits:   &[(&Timeline, u64)],
        signals: &[(&Timeline, u64)],
    ) -> Result<u64>;
}
```

### 4.6 Escape hatch — a SEPARATE CRATE (decided 2026-08-11)

**Pure `nevarea` contains no raw/unsafe surface at all.** The escape hatch is opt-in and lives entirely
outside the core crate. Still fully supported — just not in pure Nevarea.

```
nevarea         # 100% safe. No vk types, no unsafe in the public API.
nevarea-raw     # opt-in. ash handles, into_raw/from_raw, DLSS/NRD/Streamline plumbing.
```

```rust
// nevarea-raw
pub trait DeviceRawExt {
    /// ash::Device, vk::PhysicalDevice, vk::Instance, queue families.
    fn vk(&self) -> VulkanDevice<'_>;
}
pub trait BufferRawExt {
    fn vk_buffer(&self) -> vk::Buffer;
    fn into_raw(self) -> BufferHandle;                       // forget Drop, caller owns it
    unsafe fn from_raw(d: &Device, h: BufferHandle) -> Self; // re-adopt
}
impl CommandListRawExt for CommandList { fn vk_command_buffer(&mut self) -> vk::CommandBuffer; }
```

**Honest mechanism caveat:** Rust has no friend-crate. A separate crate cannot reach `nevarea`'s private
fields, so core must expose *some* hook. Two ways, pick one:

- **(a) `#[doc(hidden)] pub mod __internal`** in core, semver-exempt, documented as "not public API".
  `nevarea-raw` builds on it. Core's *docs* stay 100% safe — the goal is met socially, not absolutely.
- **(b) `raw` cargo feature** on core instead of a separate crate. Technically cleanest, but the surface
  then lives *in* Nevarea, which is what we're trying to avoid.

Leaning **(a)** — it matches the stated intent. Cost: `__internal` is technically reachable, so "entirely
outside" is aspirational.

**What stays in CORE regardless:** `DeviceDescription::feature_chain` (§4.2). A companion crate cannot
retrofit a `pNext` chain onto an already-created device, so this one hook must live in core — which is
exactly what makes host-side DLSS/NRD possible without a raw dependency.

When D3D12/Metal land: `nevarea-raw` gains backend-typed accessors (`device.vk()` / `device.d3d12()`),
returning `Option`.

### 4.7 Errors

```rust
#[derive(thiserror::Error, Debug)]
pub enum Error {
    InvalidArgument, DeviceLost, InitializationFailed,
    FeatureNotPresent, OutOfMemory, SurfaceLost,
    Vulkan(vk::Result),
}
```

R0 dissolves: `?` makes propagation the path of least resistance, and a dropped error is a compile warning.

### 4.8 Threading

- `Device: Send + Sync` — shareable.
- `CommandList: Send + !Sync` — record per-thread, submit from anywhere.
- Resources `Send + Sync`; share with `Arc<Buffer>`.

### 4.9 Resource lifetime & retirement — RESOLVED (R1 + R1.2)

**The C++ bug was the clock, not the trigger.** Deferred-destroy keyed on `current_frame`, which advances
*post-submit* → off-by-one. **Key on a monotonic timeline value instead** and the bug class stops existing.

```rust
impl Device {
    /// Drain the retirement queue. Called automatically on submit and present;
    /// public for headless users who submit rarely. Doctrine: nothing hidden.
    pub fn collect(&self);
}
```

**Mechanism — one queue, one clock, covers memory AND descriptor slots:**

1. Device owns a monotonic `Timeline`, signalled by every queue submit.
2. `CommandList` holds an **`Arc` clone of every resource it references.**
   This kills the only hazard: a resource dropped while an *un-submitted* list still references it.
   If the last `Arc` drops, no unsubmitted list can reference it, and every submitted list's value
   is `<= last_submitted_value`. Airtight, with no invariant for the user to remember.
3. Final `Arc` drop → push `(resource, bindless_slot, retire_at)`.
4. Drain = one `vkGetSemaphoreCounterValue`, then pop everything whose `retire_at` has passed.
   Cheap enough to run unconditionally on submit/present.

**CORRECTION (G1, 2026-08-11) — this section assumed ONE clock. That is unsound with multiple queues.**
Queue B signalling 6 before queue A signals 5 would make a wait on 6 falsely imply 5 completed. Therefore:

- `SubmitValue` is `{ queue: QueueId, value: u64 }` — every queue has its own timeline.
- `retire_at` is a **set** of per-queue values, not a single number.
- **A resource touched by two queues must outlive both** — retire only once *every* queue that used it has
  passed its recorded value.

Async compute and async transfer are Tier 1 (§4.10), so multi-queue is the normal case, not an edge case.

**Descriptor slots retire in the same queue, on the same value, in the same code path** — no second
mechanism. A new resource physically cannot claim slot 7 while last frame's shaders still read slot 7.

**Null-descriptor backfill (STRATEGY, Nevarea just does it):** on retire, write a null descriptor into the
slot before freeing it (`VK_EXT_robustness2` `nullDescriptor`, else a 1×1 magenta debug image). A stale index
then reads something *defined* — silent corruption becomes visible magenta.

---

### 4.9b Lifetime hole created by the profile contract — IMPORTANT

§4.9 claims the `Arc`-on-CommandList scheme is "airtight". **Under §1.1 that claim is false**, and the reason
is structural, not a bug:

- Buffers are used by **`u64` device address**, written into push constants or a shader struct.
- Textures are used by **`u32` bindless index**.

Nevarea sees a number, not a reference. It **cannot** know a command list uses that resource, so it cannot
keep it alive. Rust cannot track a lifetime laundered through an integer — this is exactly a raw pointer.

**Resolution (no way to make this fully safe without breaking §1.1):**

1. **Document the contract:** `device_address()` and `bindless_index()` return values that do **not**
   participate in lifetime tracking. Holding the number does not keep the resource alive; the user must keep
   the owning `Buffer` / `Image` alive themselves.
2. **Provide the explicit escape valve:**
   ```rust
   impl CommandList {
       /// Retain `r` until this list's GPU work completes. Needed when the resource is referenced
       /// only via a device address or bindless index (§1.1), which Nevarea cannot see.
       pub fn keep_alive(&mut self, r: &impl Resource);
   }
   ```
3. `Arc`-tracking still applies to everything Nevarea *does* see — pipelines, attachments, images passed to
   `barrier()`, buffers in `copy_buffer()`.

**Silver lining:** because BDA+bindless means command lists reference very few objects, `Arc` traffic during
parallel recording is small — which removes the atomic-contention worry in §4.12.

### 4.9c Tooling & observability — the AAA bar

Carried over from the C++ board (#54–60), which the first Rust design pass dropped. Everything below is
run through the redundancy test, so this stays small.

#### IN CORE — the user cannot obtain these any other way

| Tool | Why it cannot be user-side |
|---|---|
| **Device-fault breadcrumbs** | When a shipped title TDRs, you need to know *which draw*. Needs `VK_EXT_device_fault` + `VK_NV_device_diagnostic_checkpoints` / `VK_AMD_buffer_marker` wired into command recording — only Nevarea sees that. `Error::DeviceLost` must carry the breadcrumb trail, not just say "lost". **This is the single most valuable AAA tool and the most commonly missing.** |
| **Memory report / budget** | `VK_EXT_memory_budget` + allocator internals. Per-allocation names, live totals, leak list at shutdown. |
| **Calibrated timestamps** | `VK_EXT_calibrated_timestamps` — without a calibrated GPU↔CPU domain, GPU zones cannot be correlated with CPU zones. Prerequisite for Tracy. |
| **Instrumentation hooks** | A user wrapper can only see calls it makes; it cannot see Nevarea's *internal* submits, staging uploads, or retirement. A minimal trait closes that blind spot. |

#### OPTIONAL CRATE — `nevarea-tracy`

Tracy is a third-party dependency; forcing it into core is bloat. Core exposes a minimal
`trait Instrument` (gpu zone begin/end, cpu zone, frame mark, plot); `nevarea-tracy` implements it.
Same pattern as `nevarea-raw` / `nevarea-compat`. Anyone can implement the trait for Optick, Superluminal,
or an in-house profiler.

#### STRATEGY — Nevarea just does it, invisibly

- **Shader binary disk cache.** `vkGetShaderBinaryDataEXT` gives shader-object binaries; cache and reuse them.
  Invisible, no semantic effect — pure bucket 2.
- **Async shader compilation** off the render thread (largest stutter source in shipping engines).

#### CUT — redundant under the test

| Cut | Achievable natively how |
|---|---|
| Shader hot-reload API | Recreate a `ShaderSet` — with shader objects that is already cheap. A dedicated API adds nothing. |
| RenderDoc/PIX capture trigger | The user calls the RenderDoc API with the instance from `nevarea-raw`. |
| Custom log callback | Use the `tracing` crate — the Rust ecosystem standard. Nevarea emits spans/events; the app chooses a subscriber. No bespoke logging API. |

### 4.10 Primitive inventory — everything that must be IN Nevarea

Requirement: like acceleration structures and mesh shaders, **every** capability that changes what the user
writes must be a first-class Nevarea primitive, capability-gated. Nothing gets left out and reached for
through the escape hatch. This is that list.

Ruling for each row is §1.2's test: *does it change the user's shaders, draw calls, or data layout?*
"HW" = lowest NVIDIA tier, since the GTX 950M (Maxwell) is a live target.

#### Tier 1 — core. Nevarea is incomplete without these.

| Primitive | What it changes for the user | HW | Status |
|---|---|---|---|
| Compute dispatch | compute shaders | Maxwell | have |
| Acceleration structures | RT geometry input | Turing (SW fallback possible) | have |
| Timeline sync | explicit cross-queue sync | Maxwell | have |
| Async compute / transfer queues | their submission + sync structure | Maxwell | `QueueKind` |
| **Indirect draw / dispatch + multi-draw indirect** | GPU-driven: args live in a buffer | Maxwell | **ADD** |
| **Ray query (inline RT)** | RT from any shader stage | Turing | **ADD** |
| **Ray tracing pipelines + SBT** | RT shader stages, shader binding table | Turing | **ADD** |
| **Mesh + task shaders** | task/mesh stages, `draw_mesh_tasks` | Turing | **ADD** |
| **Tessellation** | tess control/eval stages, patch topology | Maxwell | **ADD** |
| **Variable rate shading** | per-draw / per-primitive / image-based rates | Turing | **ADD** |
| **Queries** (timestamp, occlusion, pipeline stats) | query pools + readback in their loop | Maxwell | **ADD** |
| **Sparse / tiled resources** | their whole memory management model | Maxwell | **ADD** |
| **Multiview** | one draw → N layers (VR, cubemaps, cascades) | Maxwell | **ADD** |
| **HDR output / swapchain colorspace** | output transfer function semantics | Maxwell | **ADD** |
| **Subgroup ops** | shader-side only — needs a capability query, no API surface | Maxwell | **query only** |

#### Tier 2 — advanced RT / GPU-driven. High value for the path tracer specifically.

| Primitive | What it changes | HW |
|---|---|---|
| **Shader Execution Reordering** (`VK_NV_ray_tracing_invocation_reorder`) | big path-tracer coherence win; explicit reorder call in shader | Ada |
| **Opacity micromaps** (`VK_EXT_opacity_micromap`) | alpha-tested geometry in AS builds | Ada |
| **Displacement micromaps** | AS build input | Ada |
| **RT motion blur** | AS build input, time-varying transforms | Turing+ (NV) |
| **RT position fetch** | read vertex positions from the AS in-shader | Turing |
| **Cooperative matrix** (`VK_KHR_cooperative_matrix`) | matrix ops in shaders (ML/denoise) | Turing |
| **Cooperative vector** (`VK_NV_cooperative_vector`) | neural shading — the DLSS-era primitive | Turing |
| **Device-generated commands** | GPU writes its own command stream | Turing |

#### Tier 2b — latency protocol

| Primitive | What it changes | Extension |
|---|---|---|
| **Reflex / low-latency** | the shape of the user's frame loop (see below) | `VK_NV_low_latency2` |

**Reflex is not present timing.** That distinction is why it is a PRIMITIVE and not STRATEGY. It is a protocol
the application *participates in*:

- `vkLatencySleepNV` must be called at a specific point — **before input sampling** — so the CPU is throttled
  to match GPU pace rather than running ahead and queuing latency.
- The app marks phases: `SIMULATION_START/END`, `RENDERSUBMIT_START/END`, `PRESENT_START/END`, `INPUT_SAMPLE`.

Both change the user's frame loop structure, so Nevarea cannot do it for them (§1.2). Nevarea exposes the
markers and the sleep; the user places them.

**Shape it generically, back it with NV first.** AMD Anti-Lag 2 and Intel XeLL are different APIs solving the
same problem — a vendor-neutral `LatencyMarker` enum + `device.latency_sleep()` keeps the door open without a
rewrite. Capability-gated; a no-op where unsupported.

Ties to [[dlss-nrd-plan]]'s "every latency knob runtime-modifiable" requirement.

#### Tier 3 — raster control. Real primitives, deferred to post-v1 (see rulings below).

| Primitive | What it actually does | Used for | Availability |
|---|---|---|---|
| **Conservative rasterization** | rasterizes a triangle if it touches a pixel **at all**, not just at the pixel centre | voxelization for GI, hit-testing, decals, occlusion culling | `VK_EXT_conservative_rasterization`; needs 2nd-gen Maxwell (GM20x+) |
| **Fragment shader interlock** | guarantees **ordered, mutually exclusive** fragment shader execution per pixel | order-independent transparency, programmable blending | `VK_EXT_fragment_shader_interlock`; broad desktop |
| **Fragment density map** | renders the centre sharp and the periphery at lower resolution | VR foveated rendering | `VK_EXT_fragment_density_map`; mostly mobile/VR — NVIDIA desktop prefers VRS |
| **Custom sample locations** | controls **where MSAA samples sit** inside a pixel | TAA jitter patterns, checkerboard rendering | `VK_EXT_sample_locations`; broad desktop |
| **Depth bounds test** | early-rejects fragments outside a given depth range | deferred light volumes, decals | **core Vulkan** (`VkPhysicalDeviceFeatures::depthBounds`) — not an extension |

Each is a flag or small struct inside a pipeline/pass description — none is a subsystem. That is exactly why
they defer safely (see the sequencing rule in the rulings below).

#### CUT — legacy, superseded, or forbidden by §1.1

| Cut | Why |
|---|---|
| Geometry shaders | universally slow; mesh shaders supersede |
| Transform feedback | legacy; compute supersedes |
| Push descriptors, descriptor set binding API | §1.1 — no binding model |
| Vertex input state | §1.1 — BDA pulls vertices |
| Legacy render passes, subpasses, input attachments | dynamic rendering supersedes |
| Binary semaphores | timeline supersedes |
| `VK_EXT_descriptor_buffer` | §1.2 — heaps exist to fix it |

#### STRATEGY — real, but hidden (§1.2)

Extended dynamic state (always use where available) · pipeline caching · memory placement ·
descriptor model (sets → heaps) · present timing.

#### Rulings — settled 2026-08-11

**Video encode/decode → OUT OF SCOPE (user problem), with one required hook.**
The line: *Nevarea is the render / compute / copy path. Video codecs are a **separate hardware engine** with
their own state machine* — DPB slot management, codec parameter sets, H.264/265/AV1 specifics. Not part of the
draw path. **But** doctrine forbids locking users out, so core must expose
`QueueKind::VideoDecode` / `QueueKind::VideoEncode`. Everything past that is `nevarea-raw` territory.
One API item instead of a subsystem.

**Shader objects → PRIMITIVE.** Consistent with the C++ decision (`NEVAREA_PROGRESS.md:43`, #12: pipelines
immutable; change state via recreate *or* dynamic state / shader objects). Never was hidden strategy.

> **Resolution of the dual-path collision** (same trap as descriptor buffers — one path only):
> - **Graphics + compute → shader objects ONLY.** No `Pipeline` type in those domains.
> - **Ray tracing → `Pipeline` only.** RT *cannot* use shader objects; `VkPipeline` is mandatory there.
>
> Two domains, one path each. Not a dual path.
>
> **RESOLVED 2026-08-11 — option (a), with `VK_LAYER_KHRONOS_shader_object` as an invisible fallback.**
>
> **Nevarea always calls the shader-object entry points. Exactly one code path, always.** Where
> `VK_EXT_shader_object` is missing (AMD on older drivers, some Intel, most mobile), the Khronos layer
> *implements the extension* underneath.
>
> **This is not a dual path** — the layer provides the extension, it does not add a branch. The no-dual-path
> rule from the descriptor ruling holds unchanged.
>
> **Doctrine placement: bucket 2 STRATEGY.** Auto-enable the layer when the extension is absent and the loader
> can find it. Invisible, no semantic effect on output, never user-configured. The user can still force or
> suppress it via the raw layer list in `InstanceDescription` (§4.1) — nothing is locked away.
>
> Two caveats to build around:
> - **The layer is compatibility, not perf-parity.** It emulates shader objects by building pipelines on the
>   fly — trading back the stutter that shader objects exist to remove. Correct everywhere, *fast* where
>   native. Capability queries must therefore report **native vs emulated**, not just supported.
> - **Discoverability is a packaging problem.** The layer ships with the Vulkan SDK; end users do not have the
>   SDK. Nevarea can only enable what the loader already finds. Redistributing the layer binary + manifest is
>   opt-in and belongs **outside core** — `nevarea-compat`, same principle as `nevarea-raw` (§4.6). The layer
>   has its own driver prerequisites; verify per target rather than assuming.

**Reflex / low-latency → PRIMITIVE.** No doctrine conflict after all: Reflex is *not* present timing. It's a
protocol the app participates in — `vkLatencySleepNV` before input sampling, plus latency markers around
simulation / render / present. Structural to the user's frame loop → primitive by §1.2. Shape the API
generically (AMD Anti-Lag 2 and Intel XeLL are different APIs); back it with `VK_NV_low_latency2` first.

**Tier 3 → DEFER ALL to post-v1.** Kept on the list, not in v1 scope. Rationale is a general sequencing rule:

> **Defer anything whose later addition is non-breaking. Spend the design budget on what is load-bearing.**

All five Tier-3 items are flags or small structs inside pipeline/pass descriptions — purely additive. The sync
model and object graph are not. (Note: conservative rasterization needs 2nd-gen Maxwell / GM20x; the GTX 950M
is GM107, so it likely lacks it anyway.)

(Each Tier-3 item is documented in its table above.)

### 4.11 Swapchain sync — RESOLVED (§7.3 finding 2)

**The constraint:** `vkAcquireNextImageKHR` signals a **binary** semaphore, and `vkQueuePresentKHR` waits on a
**binary** semaphore. Timeline semaphores are **not permitted** in either. This is not negotiable.

**The solution: opaque `Wait` / `Signal` types. ZERO extra submits.**

An earlier draft bridged binary→timeline with two empty submits per frame. **That was wrong** — it assumed a
submit's wait list must hold timeline values. Only the *public type* must be opaque. `Wait` and `Signal` are
opaque enums, internally *either* timeline-or-binary, so the swapchain's binary semaphores go directly into
the user's own submit. Nothing leaks, nothing extra is submitted.

```rust
pub struct Wait<'a>   { /* opaque: Timeline(&Timeline, u64) | Binary(vk::Semaphore) */ }
pub struct Signal<'a> { /* same */ }

let frame = swapchain.acquire()?;
let done  = queue.submit(&[cmd], &[frame.ready()], &[frame.present_signal()])?;
swapchain.present(frame)?;   // binary already signalled by the user's own submit
```

**On the multithreaded join:** `vkQueueSubmit` is *externally synchronized* — parallel submits to one queue
serialize on a mutex and gain nothing. Real parallelism is **parallel recording, few submits**. So "N threads
submit and nobody knows who is last" is a scenario with no performance benefit to begin with. Someone is
always last and the user always knows; if a topology genuinely needs a join, the user adds one explicitly and
pays the cost only when it is real.

#### Production-grade requirements (industry bar, not "works on my machine")

1. **Present-semaphore reuse is a known Vulkan wart.** Without `VK_EXT_swapchain_maintenance1` there is *no*
   guarantee telling you when a present semaphore is safe to reuse — "the image can't be re-acquired until
   present completes" does **not** imply the semaphore is free. **Use `VK_EXT_swapchain_maintenance1` present
   fences where available**; otherwise fall back to one present semaphore per image plus a wait on that
   image's last-known timeline value before reuse. Never assume.
2. **Acquire-semaphore pool sized to the swapchain IMAGE COUNT**, not frames-in-flight — `acquire` returns
   arbitrary indices. Classic reuse hazard. Internal, bucket 1.
3. **`OUT_OF_DATE` / `SUBOPTIMAL` are first-class API states, not errors to guess at.** Both `acquire` and
   `present` can return them. Resize must be expressible without the user pattern-matching on a Vulkan code:
   ```rust
   pub enum Acquired { Frame(Frame), OutOfDate, Suboptimal(Frame) }
   ```
   Also handle zero-extent (minimized window) and recreate-with-`oldSwapchain` for a stall-free resize.
4. **Frames-in-flight is RUNTIME, never compile-time.** The C++ `MAX_FRAMES_IN_FLIGHT` macro was a
   doctrine violation (§1 hard override: no compile-time locks on low-level capability).

Also required for a complete swapchain: present-mode selection (FIFO / MAILBOX / IMMEDIATE),
`VK_KHR_present_id` + `present_wait` for frame pacing (C++ #107), HDR colorspace (§4.10 Tier 1),
and multiple swapchains for multi-window (C++ #98).

### 4.12 Multithreading model

Goal: actually exploit Rust, not just be memory-safe. Three axes of parallelism, plus what the type system
enforces for free.

**1. Parallel command recording**

```rust
let lists: Vec<CommandList> = chunks.par_iter()          // rayon
    .map(|chunk| {
        let mut cmd = device.create_command_list(QueueKind::Graphics)?;
        record(&mut cmd, chunk);
        cmd
    })
    .collect::<Result<_>>()?;

let done = queue.submit(&lists, &[frame.ready()], &[])?;  // one submit, order = slice order (R3)
```

- `CommandList: Send + !Sync` — movable between threads, never shared. **The `!Sync` bound makes the classic
  "recorded into another thread's command pool" bug a compile error.** This is the concrete Rust win.
- **Command pools are a `thread × frames-in-flight` MATRIX**, not merely thread-local. A pool keyed only by
  thread gets reset while frame N-1 is still executing from it — corruption. Reset is keyed on the same
  retirement timeline as §4.9: one clock for everything.
- **Primary command buffers, not secondaries.** With dynamic rendering, multiple primaries submitted together
  preserve order (R3) and avoid the inheritance-info complexity secondaries require. Industry practice.
- **Pipeline / shader compilation must be async and off the render thread** — it is the single largest source
  of frame-time stutter in shipping engines. Compile on a worker pool; the C++ board tracked this as #57.

**2. Parallel resource creation**

`Device: Send + Sync`, all creation takes `&self`. Requires internal synchronization on:
- the allocator (`gpu-allocator` is not internally synced → `Mutex`),
- the bindless slot free-list (`Mutex` or atomic free-list),
- the retirement queue (`Mutex`).

These are uncontended in practice; creation is not a per-frame hot path.

**3. Parallel submission**

`VkQueue` requires external synchronization. `Queue: Send + Sync` with an internal `Mutex<vk::Queue>` — the
lock is held only for the duration of `vkQueueSubmit`.

**The join point is the timeline** — which is why §4.11 is shaped the way it is:

```rust
let values: Vec<u64> = threads.map(|t| queue.submit(&t.lists, &[], &[])).collect::<Result<_>>()?;
swapchain.present(frame, values.into_iter().max().unwrap())?;
```

**Scoped threads** let recording borrow scene data directly, with no `Arc` and no clone:

```rust
std::thread::scope(|s| {
    for chunk in scene.chunks(n) { s.spawn(|| record(chunk)); }  // borrows `scene`
});
```

**Summary of the marker traits — these ARE the design:**

| Type | Bounds | Why |
|---|---|---|
| `Device` | `Send + Sync` | shared by all threads; internal locks on allocator/slots/retire |
| `Queue` | `Send + Sync` | internal mutex; Vulkan demands external sync |
| `CommandList` | `Send + !Sync` | per-thread pool — `!Sync` makes cross-thread recording a compile error |
| `Buffer` / `Image` / etc. | `Send + Sync` | immutable once created; share via `Arc<Buffer>` |
| `Timeline` | `Send + Sync` | the cross-thread join primitive |
| `Swapchain` | `Send + !Sync` | acquire/present are inherently serial; keep them on one thread |

## 5. Redundancy test, pre-applied

| Item | Achievable natively otherwise? | Verdict |
|---|---|---|
| `Mesh` | Yes — buffers + draw call | **CUT** |
| `Vertex` / `VertexLayout` | Yes — user owns buffer contents entirely | **CUT** |
| `present_image` | Yes — swapchain present | **CUT** |
| Render graph | Yes — user records in order | **CUT** (this is WORK) |
| Shader compiler | N/A — user owns shaders; take opaque blob + stage | **CUT** (use Slang host-side) |
| Material / scene / camera | Yes — pure WORK | **CUT** |
| `upload_buffer` / `upload_image` | **No** — staging is the only native path | **KEEP** (heavy boilerplate is fine) |
| Explicit `barrier` | **No** | **KEEP** |
| `Timeline` | **No** | **KEEP** |
| Bindless index accessor | **No** | **KEEP** |
| `device_address()` | **No**, and hiding it violates doctrine | **KEEP** |

---

## 6. "D3D12/Metal-shaped" — what it actually costs

Nearly nothing. The modern APIs converged:

| Concept | Vulkan | D3D12 | Metal |
|---|---|---|---|
| GPU address | buffer device address | GPU VA | `gpuAddress` |
| Bindless | descriptor indexing | descriptor heaps | argument buffers |
| Timeline sync | timeline semaphore | `ID3D12Fence` (already monotonic) | `MTLSharedEvent` |
| No render passes | dynamic rendering | `OMSetRenderTargets` | render command encoder |
| Barriers | sync2 | enhanced barriers | converged |

**The one real divergence: shaders** — SPIR-V vs DXIL vs MSL. Resolution: accept an **opaque blob + stage**, compile nothing, ship no shader compiler. Keeps Nevarea tiny *and* portable. Users reach for Slang if they want one source.

---

## 7. Usage sketches — falsification pass

Not real code. The API written from the *caller's* side to expose awkwardness before any of it exists.
**Findings are in §7.3 and they matter more than the sketches.**

### 7.1 Triangle

```rust
use nevarea::*;

#[repr(C)]
struct Push { vertices: u64 }   // §1.1: no vertex input state — the shader pulls via BDA

fn main() -> Result<()> {
    let instance = Instance::new(&InstanceDescription {
        app_name:   "triangle",
        validation: cfg!(debug_assertions),
        extensions: &[],
    })?;

    // Nevarea does NOT rank adapters — picking is policy, the user owns it.
    let adapter = instance.adapters().into_iter()
        .find(|a| a.info().kind == DeviceKind::Discrete)
        .ok_or(Error::InitializationFailed)?;

    let device = adapter.create_device(&DeviceDescription {
        queues:        &[QueueRequest::graphics()],
        extensions:    &[],
        feature_chain: None,
    })?;
    let queue = device.queue(QueueKind::Graphics).unwrap();

    let verts: [[f32; 4]; 3] = [ /* ... */ ];
    let vbuf = device.create_buffer(&BufferDescription {
        size:   size_of_val(&verts) as u64,
        usage:  BufferUsage::STORAGE | BufferUsage::TRANSFER_DST,
        memory: MemoryKind::DeviceLocal,
    })?;
    device.upload_buffer(&vbuf, bytemuck::bytes_of(&verts))?;   // staging is the only native path → KEEP

    let surface       = instance.create_surface(&window)?;
    let mut swapchain = device.create_swapchain(&surface, &SwapchainDescription::default())?;

    let pipeline = device.create_pipeline(&PipelineDescription::graphics(GraphicsPipeline {
        vertex:         ShaderBlob::spirv(include_bytes!("tri.vert.spv")),
        fragment:  Some(ShaderBlob::spirv(include_bytes!("tri.frag.spv"))),
        color_formats:  &[swapchain.format()],
        depth_format:   None,
        push_constants: size_of::<Push>() as u32,
        ..Default::default()
    }))?;

    loop {
        let frame    = swapchain.acquire()?;
        let mut cmd  = device.create_command_list(QueueKind::Graphics)?;

        cmd.barrier(&[], &[ImageBarrier::to_color_attachment(&frame.image)]);
        {
            let mut pass = cmd.begin_rendering(&RenderingDescription {
                color: &[ColorAttachment {
                    image: &frame.image,
                    load:  Load::Clear([0.0, 0.0, 0.0, 1.0]),
                    store: Store::Store,
                }],
                depth: None,
                area:  frame.extent(),
            });
            pass.bind_pipeline(&pipeline);
            pass.set_viewport(Viewport::full(frame.extent()));   // NOT swapchain-derived (R3)
            pass.push_constants(bytemuck::bytes_of(&Push { vertices: vbuf.device_address() }));
            pass.draw(3, 1);
        } // RenderPass::drop ends rendering

        cmd.barrier(&[], &[ImageBarrier::to_present(&frame.image)]);
        queue.submit(&[cmd], &[], &[])?;
        swapchain.present(frame)?;
    }
}
```

### 7.2 Compute dispatch

```rust
let img = device.create_image(&ImageDescription {
    extent: [1920, 1080], format: Format::Rgba16Float,
    usage:  ImageUsage::STORAGE | ImageUsage::SAMPLED,
    ..Default::default()
})?;

let pipeline = device.create_pipeline(&PipelineDescription::compute(
    ShaderBlob::spirv(include_bytes!("trace.comp.spv")),
))?;

let mut cmd = device.create_command_list(QueueKind::Compute)?;
cmd.barrier(&[], &[ImageBarrier::to_general(&img)]);
cmd.bind_pipeline(&pipeline);                                   // ← see finding 3
cmd.push_constants(bytemuck::bytes_of(&Push { target: img.bindless_index() }));
cmd.dispatch(1920 / 8, 1080 / 8, 1);

let value = queue.submit(&[cmd], &[], &[(&timeline, next)])?;
timeline.wait(value, Duration::from_secs(1))?;
```

### 7.3 What the sketches broke — REAL FINDINGS

1. **"Binary semaphores → CUT" is WRONG.** `VK_KHR_swapchain` acquire/present *requires* binary
   semaphores; timeline semaphores cannot be used there. The cut in §4.10 is invalid as written.
   **Fix:** binary semaphores stay, but **internal only** — bucket 1 SETUP. `frame` carries its own
   acquire/present sync opaquely and `swapchain.present(frame)` consumes it. The user still only ever
   sees timelines. Update §4.10's CUT row to "not exposed" rather than "does not exist".

2. **`queue.submit(&[cmd], &[], &[])` in 7.1 is a lie.** With finding 1, swapchain sync has to be
   threaded through submit somehow. Either `submit` learns about frames, or `Frame` implicitly
   contributes its wait/signal. Needs an explicit decision — this is the single ugliest spot in the API.

3. **`bind_pipeline` / `push_constants` exist on both `CommandList` (compute) and `RenderPass`
   (graphics).** Duplicated surface, asymmetric shape. **Fix:** add `cmd.begin_compute() -> ComputePass<'_>`
   for symmetry, and hoist the shared calls into a trait both passes implement.

4. **`device.create_command_list()` per frame = allocation churn.** Command pool recycling is bucket 1/2
   (SETUP + STRATEGY) — Nevarea must pool internally and the user must never see it. Confirm the
   `CommandList` type can be cheaply recycled given it holds `Arc` clones (§4.9).

5. **`ImageBarrier::to_color_attachment` / `to_present` / `to_general` helpers** — convenient, but verify
   against §5: is a hand-built barrier still expressible? If the helpers are the *only* path, that's
   hidden control and violates doctrine. They must be sugar over a fully public struct.

## 8. Open questions

1. ~~**Crate name**~~ — **RESOLVED 2026-08-11:** `crates.io/api/v1/crates/nevarea` returns 404, so the name is unclaimed. Names are first-come; publish a `0.0.0` placeholder to reserve it if that matters.
2. ~~**Allocator**~~ — **RESOLVED: `gpu-allocator`.** Pure Rust, no C++ dependency in the build — deleting the C++ toolchain is half the point of the rewrite.
3. ~~**Retirement queue mechanism**~~ — **RESOLVED, see §4.9.** Timeline-keyed, `Arc`-on-CommandList, auto-drain on submit/present + public `collect()`.
4. **`into_raw`/`from_raw` contract** — precise safety invariants to document. (Naming: no `Owned<T>` wrapper — the resource type *is* the owning type.)
5. ~~**Bindless slot quarantine**~~ — **RESOLVED, see §4.9.** Same queue, same clock, same code path, plus null-descriptor backfill.
6. **Repo** — new repo, or reuse `Nevarea-repo` with the C++ tree archived on a branch/tag?
7. ~~**Hardware floor**~~ — **RESOLVED. Non-issue.** (An earlier draft wrongly claimed Maxwell lacks BDA and descriptor indexing. It does not — NVIDIA ships Vulkan 1.3 back to Maxwell, and the C++ Nevarea runs BDA+bindless on a GTX 950M today. "Modern Vulkan features" and "modern hardware" are different axes.) Real Maxwell gaps: **mesh shaders** (Turing+) and **descriptor buffers** (irrelevant — never supported, see §1.2). Capability-gate per §4.10; no floor change needed.
8. **Primitive inventory (§4.10)** — rulings are settled; none of it is written into §4's signatures yet.
9. ~~**Shader-object trilemma**~~ — **RESOLVED, see §4.10.** Shader-object-only (one path always), with `VK_LAYER_KHRONOS_shader_object` auto-enabled as an invisible STRATEGY fallback where the extension is absent. Not a dual path — the layer *implements* the extension. Follow-ups: capability queries must report **native vs emulated**, and layer redistribution goes in `nevarea-compat`, outside core.
10. ~~**Swapchain sync in `submit`**~~ — **RESOLVED, see §4.11.** Bridge binary↔timeline internally; public API stays pure timeline; timeline value is the multithreaded join point.
11. **`ComputePass`** (§7.3 finding 3) — add for symmetry with `RenderPass`, hoist shared calls into a trait.
12. **`keep_alive` ergonomics** (§4.9b) — BDA addresses and bindless indices launder lifetimes past Rust. Is explicit `cmd.keep_alive(&buf)` good enough, or does it need a frame-scoped retention set? This is the one place §1.1 costs real safety.
13. **`gpu-allocator` locking** — confirm it needs an external `Mutex` for the `Device: Sync` guarantee in §4.12, and whether that's a contention risk under parallel creation.

## 9. Platform portability — arch, OS, toolchain

Portability here means the **host**: CPU architecture, operating system, and build toolchain.
Graphics-API portability (D3D12/Metal) is §1.1b and §6 — a different axis.

The goal is not "compiles everywhere by accident." It is a **stated tier list** Nevarea can actually
keep, plus a small set of rules that make the wide tiers free instead of expensive.

### 9.1 The five decisions that buy almost all of it

These are cheap now and near-impossible to retrofit.

**P1 — Load Vulkan dynamically, never link it.** `ash::Entry::load()` (dlopen), *not* ash's `linked`
feature. Consequences: no Vulkan SDK needed to build, no `vulkan-1.lib` path in the build, works on
any distro/BSD/Android where a loader exists, and a missing loader is a clean
`Error::InitializationFailed` instead of a failure to start the process. This single decision is
worth more than every other item in this section.
Probe order must include `libvulkan.so.1`, `libvulkan.so`, `vulkan-1.dll`, `libvulkan.dylib`,
`libMoltenVK.dylib` — the last one matters because macOS users frequently have MoltenVK without the
loader installed.

**P2 — Zero non-Rust build dependencies. Permanently.** No `cc`, no `bindgen`, no `shaderc`, no
vendored C++. The moment one appears, cross-compiling to `aarch64-linux-android` or
`x86_64-unknown-freebsd` stops being `cargo build --target` and starts being a toolchain project.
Current dependency audit — all pure Rust, all clean: `ash`, `gpu-allocator`, `raw-window-handle`,
`bitflags`.
This retroactively promotes `nevarea-tracy` from "avoid bloat" to a **portability requirement** —
Tracy's client is C++, so it must stay outside core (§4.9c).
It also confirms **SPIR-V bytes in, nothing else**: shipping a GLSL/HLSL compiler would drag in C++.

**P3 — Nevarea spawns zero threads and picks zero filesystem paths.** Both are OS-policy decisions
that belong to the host app (and are exactly what makes libraries unusable on Android/consoles).
The shader binary disk cache (§4.9c) therefore takes **bytes from the user**, never a path it guessed.
⚠️ This collides with "async shader compile" being listed as invisible STRATEGY in §4.9c — a library
that spawns no threads cannot compile in the background by itself. Resolution is an open item (§9.6).

**P4 — Nevarea owns no window.** It already takes a raw native handle; in Rust that becomes a
`raw-window-handle` impl. Tiny, zero-dep, ecosystem-standard trait crate — so winit, SDL2, GLFW, tao,
Android's `ANativeWindow`, or a hand-rolled Win32 loop all work with no Nevarea dependency on any of
them. Windowing therefore contributes **zero** portability surface.

**P5 — Portability enumeration is not optional.** If `VK_KHR_portability_enumeration` is available,
Nevarea must enable it *and* set `ENUMERATE_PORTABILITY_BIT`, and must enable
`VK_KHR_portability_subset` on any device that reports it (the spec mandates this). Skipping it is
the single most common "works on everything except Mac" bug: MoltenVK devices simply never appear in
`enumerate_physical_devices`, so the failure looks like "no GPU found," not "unsupported."

### 9.2 CPU architecture

| Concern | Ruling |
|---|---|
| **Memory ordering** | The real hazard. x86 is TSO, so a missing `Acquire`/`Release` on the retirement/timeline atomics is **invisible on your machine and corrupts on ARM**. Every atomic in the retirement path gets an explicitly justified ordering, and the multithreaded recording path is tested under `loom` — which finds it on x86, where hardware never will. |
| **Pointer width** | `u64` device addresses must **never** be cast to `usize` or to a pointer. `VkDeviceSize` is `u64` on every target. A `as usize` on a BDA is a truncation bug on any 32-bit target and a lifetime lie on all of them. Grep-ban it. |
| **64-bit atomics** | Timeline values are `u64`. `AtomicU64` is unavailable on armv7/riscv32 without target features. Rather than carry a `Mutex<u64>` fallback forever: **Nevarea is 64-bit-only, stated up front.** Add `compile_error!` on `target_pointer_width = "32"` with the reason. A bindless-BDA RHI on a 32-bit target is not a real user. |
| **Endianness** | Every Vulkan implementation is little-endian; push-constant/`&[u8]` blobs assume LE. `compile_error!` on big-endian rather than silently producing wrong pixels. |
| **SIMD / intrinsics** | None. Nevarea does no math. Nothing to port. |

### 9.3 Operating systems

| OS | Loader / surface | Status |
|---|---|---|
| **Windows** (x86_64, aarch64) | `vulkan-1.dll`, `VK_KHR_win32_surface` | Tier 1 |
| **Linux** (x86_64, aarch64, riscv64) | `libvulkan.so.1`; **Xlib + Xcb + Wayland enabled together** | Tier 1 / Tier 2 |
| **macOS** (aarch64, x86_64) | MoltenVK, `VK_EXT_metal_surface` | Tier 2 — see below |
| **Android** (aarch64) | system loader, `VK_KHR_android_surface` | Tier 3, builds + runs |
| **FreeBSD / OpenBSD / NetBSD / DragonFly** | Mesa, `libvulkan.so.1`, X11/Wayland | Tier 3, builds |
| **iOS** | MoltenVK | Tier 3 |
| **wasm / browser** | — | **Never supported.** WebGPU forbids buffer device addresses, so the browser cannot host Nevarea for exactly the spec reason wgpu cannot *be* Nevarea (§0 gap statement). Say this in the README; it is a design statement, not a gap. |

**Linux detail worth its own line:** one binary must serve X11 and Wayland. Instance extensions are
chosen at *instance* creation, surfaces at *surface* creation — so enable **every** present surface
extension and dispatch on the `raw-window-handle` variant later. Enabling only the "current" display
server is a classic bug that only shows up on the other one.

**macOS honest ruling.** MoltenVK works and should be supported, but it is Tier 2 and stays Tier 2:
it is a translation layer whose feature set trails, and the *real* Mac story is the native Metal
backend §1.1b is already shaped for. Known frictions: no `VK_EXT_shader_object` (covered free by the
Khronos layer already chosen in §4.10 — the fallback paid for itself twice), no
`VK_EXT_calibrated_timestamps` (§9.4), `VK_KHR_buffer_device_address` requires a recent MoltenVK on
Metal 3, and portability-subset limits must surface through `Adapter::supports` as
`Support::Emulated` rather than being silently absorbed.

### 9.4 Platform-specific details the API must not paper over

- **Calibrated timestamps** (§4.9c) need a *host* clock domain, and it differs per OS:
  `QUERY_PERFORMANCE_COUNTER` on Windows, `CLOCK_MONOTONIC_RAW` on Linux/BSD/Android. Where the
  extension is absent (MoltenVK, older drivers), `calibrate_timestamps()` returns
  `Err(FeatureNotPresent)` and GPU zones fall back to uncalibrated timestamp-period math. It must not
  fake a calibration.
- **Device-fault breadcrumbs** are three different extensions across vendors
  (`VK_NV_device_diagnostic_checkpoints`, `VK_AMD_buffer_marker`, `VK_EXT_device_fault`). All three
  behind one `DeviceFault` type; `Vec<Breadcrumb>` is simply empty where none is present.
- **Validation layers** are a *developer-machine* feature, not a platform feature. Absent layers must
  be a warning-and-continue, never an init failure — otherwise every non-SDK machine, including all
  of Tier 3, fails to start.

### 9.5 Support tiers — the promise being made

- **Tier 1** — CI-tested every commit, breakage blocks: `x86_64-pc-windows-msvc`,
  `x86_64-unknown-linux-gnu`.
- **Tier 2** — built + headless-tested: `aarch64-unknown-linux-gnu`, `aarch64-apple-darwin`,
  `x86_64-apple-darwin`.
- **Tier 3** — `cargo check --target` only, community-fixed: FreeBSD, `aarch64-linux-android`,
  `riscv64gc-unknown-linux-gnu`, iOS.
- **Never** — 32-bit, big-endian, wasm.

**Headless CI is what makes this affordable.** Mesa **lavapipe** is a software Vulkan 1.3
implementation with BDA and descriptor indexing, and it runs on ARM. Combined with the swapchain
being the *last* milestone (§10), every correctness test — upload/readback, compute, bindless,
barriers, even a rendered triangle compared pixel-by-pixel — runs on a GPU-less runner on any
architecture. That is how the ARM ordering bug in §9.2 gets caught by machines instead of by users.

### 9.6 Open items created by this section

- **P3 vs async compile** — no threads, but §4.9c promised background compilation. Options: (a) drop
  async compile from core and let the user call `create_shader_set` on their own thread — `ShaderSet`
  creation is `Send`, so this is already possible and is the redundancy test's answer; (b) accept a
  user-supplied executor trait. **(a) looks correct**: it is zero API surface and doctrine-clean.
  §4.9c needs amending.
- Whether portability-subset limits map onto `Support::Emulated` or need a distinct variant.

## 10. Build order — the milestone plan

Two rules govern the order.

1. **The swapchain is last, not first.** Inverting the object graph (R5) means a window is optional,
   so every milestone up to a rendered triangle is testable headless, in CI, on every architecture.
   The C++ version put presentation at the center and paid for it in every subsequent decision.
2. **Each milestone ends with something that runs and something CI asserts.** A milestone is not done
   because the types compile.

**The design loop, per milestone:** re-read the `NEVAREA_RS_API.md` section for the *next* milestone
before starting it → implement → write the falsification sketch as a real example → **amend the API
doc with what the implementation taught**. The paper design is a hypothesis; each milestone is the
experiment. Sections stay unwritten until their milestone is next, deliberately.

⚠️ **This supersedes the earlier "write the whole `lib.rs` with `todo!()` bodies" plan.** A frozen
full skeleton would lock every decision in before any of them has been falsified — the opposite of
the point. Grow the surface milestone by milestone instead.

| M | Scope | Runs | CI asserts | Design decision under test |
|---|---|---|---|---|
| **M0** | Workspace, `nevarea` crate, `Error`, dynamic loader (P1) | `Entry::load()` succeeds or errors cleanly | `cargo public-api` shows **zero** `ash`/`vk` types; `cargo check` on all Tier 1–3 targets | The API firewall, P1, P2 |
| **M1** | `Instance`, `Adapter`, `Feature`/`Support`, portability enumeration (P5) | `list_adapters` example | runs under lavapipe; MoltenVK device enumerates on macOS | Capability query design — **the thing C++ got wrong (R4)** |
| **M2** | `Device`, `Queue`, `QueueRequest`, feature chain | headless device creation | device created on lavapipe, all Tier 1–2 | Object graph (R5) |
| **M3** | `gpu-allocator`, `Buffer`, BDA, upload/readback, `MemoryReport` | upload → readback roundtrip | bytes match; zero leaks at shutdown | Allocator integration, `MemoryReport` |
| **M4** | `CommandList`, `submit`, timelines, `SubmitValue`, retirement | compute shader doubles a buffer | CPU-verified result; retirement drains | **Whole sync + lifetime design (§4.9), still headless** |
| **M5** | Bindless set 0, `Image`, views, barriers, layouts | compute writes a storage image | pixels compared | Bindless indices, slot quarantine, barrier API (R2) |
| **M6** | `ShaderSet`, shader objects, `RenderPass`, dynamic rendering, global layout | **headless triangle → PNG** | pixel-compare; **also passes with the Khronos shader-object layer forced on** | Shader-object-only ruling — proves the one-path claim |
| **M7** | `Surface`, `Swapchain`, present, resize, opaque `Wait`/`Signal` | triangle on screen | Win32 + X11 + Wayland from one binary | §4.11 zero-extra-submit sync |
| **M8** | Pool matrix, `ParallelRendering`, `RecordedPart`, `Send`/`!Sync` | N-thread record stress | **aarch64 runner** + `loom` model | §4.12 threading + §9.2 ordering |
| **M9** | Breadcrumbs, calibrated timestamps, `Instrument`, `nevarea-tracy` | deliberate fault → named draw | `DeviceFault` reports the right breadcrumb | §4.9c tooling |
| **M10** | Advanced primitives one at a time: RT/accel → mesh → indirect → VRS → queries → sparse | one headless test each | each capability-gated, each skipped-not-failed when absent | §4.10 inventory |
| **M11** | `nevarea-raw`, `nevarea-compat`, docs, `0.1.0` | `cargo add nevarea` | docs.rs builds | Escape-hatch contract (§4.6) |

**Where the design is most likely to move:** M4 and M5. Everything before them is scaffolding that
the paper design almost certainly got right; M4 is the first point where retirement, timelines, and
ownership all have to be true simultaneously, and M5 is where bindless slot reuse meets real
in-flight frames. Budget re-design time there, not at M0–M2.
