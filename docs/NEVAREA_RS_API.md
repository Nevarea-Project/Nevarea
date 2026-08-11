# Nevarea (Rust) — Full Public API Surface

Companion to `NEVAREA_RS_DESIGN.md` (which holds the *why*). This file is the *what* — it becomes `lib.rs`.

**Status:** draft 1, complete surface. Not yet falsified against usage — do that next (§13).
All rulings from the design doc are applied: shader-object-only, BDA+bindless, no binding model,
RAII with reachable handles, timeline sync, `nevarea-raw` split.

---

## 0. Crate layout

```
nevarea          # 100% safe. No ash types anywhere in the public API.
nevarea-raw      # opt-in. Raw ash handles, into_raw/from_raw, DLSS/NRD plumbing.
nevarea-compat   # opt-in. Redistributes VK_LAYER_KHRONOS_shader_object + manifest.
```

---

## 1. Errors

The backend never leaks into core (§4.6 firewall) — so **no `vk::Result` here.**

```rust
pub type Result<T> = core::result::Result<T, Error>;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("invalid argument: {0}")]          InvalidArgument(&'static str),
    #[error("initialization failed")]          InitializationFailed,
    #[error("feature not present: {0:?}")]     FeatureNotPresent(Feature),
    /// Carries the breadcrumb trail — WHICH DRAW died, not merely that the device did.
    /// See §19. This is the difference between a shippable RHI and a hobby one.
    #[error("device lost: {0}")]               DeviceLost(DeviceFault),
    #[error("out of memory ({0:?})")]          OutOfMemory(MemoryKind),
    #[error("surface lost")]                   SurfaceLost,
    #[error("timeout")]                        Timeout,
    #[error("backend error: {0}")]             Backend(BackendError),
}

/// Opaque backend failure. `nevarea-raw` can recover the underlying `vk::Result`.
pub struct BackendError(/* private */);
impl BackendError { pub fn raw_code(&self) -> i32; }
```

---

## 2. Instance / Adapter

```rust
pub struct InstanceDescription<'a> {
    pub app_name:   &'a str,
    pub validation: bool,
    /// Raw extension request — doctrine override, never gated.
    pub extensions: &'a [&'a CStr],
    /// Raw layer request. Force or suppress layers Nevarea would auto-select
    /// (e.g. VK_LAYER_KHRONOS_shader_object).
    pub layers:     &'a [&'a CStr],
}

pub struct Instance { /* .. */ }

impl Instance {
    pub fn new(desc: &InstanceDescription) -> Result<Self>;
    /// Unordered. Nevarea does NOT rank adapters — picking is policy, the user owns it.
    pub fn adapters(&self) -> Vec<Adapter>;
    pub fn create_surface(&self, window: &impl HasWindowHandle) -> Result<Surface>;
}

#[derive(Clone, Debug)]
pub struct AdapterInfo {
    pub name:      String,
    pub vendor:    Vendor,
    pub kind:      DeviceKind,        // Discrete | Integrated | Virtual | Cpu
    pub driver:    String,
    pub limits:    Limits,
}

pub struct Adapter { /* .. */ }

impl Adapter {
    pub fn info(&self) -> AdapterInfo;
    /// Queried from FEATURE STRUCTS, never from extension-name presence (design §R4).
    pub fn supports(&self, f: Feature) -> Support;
    pub fn queue_families(&self) -> &[QueueFamily];
    /// F4 — required to pick a present-capable adapter/family before device creation.
    pub fn supports_present(&self, s: &Surface) -> bool;
    pub fn create_device(&self, desc: &DeviceDescription) -> Result<Device>;
}

/// Native vs emulated matters: the shader-object layer is correct but trades back stutter.
#[derive(Copy, Clone, PartialEq, Debug)]
pub enum Support { No, Emulated, Native }
```

### Features

```rust
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum Feature {
    // tier 1
    RayQuery, RayTracingPipeline, AccelerationStructure,
    MeshShader, Tessellation, VariableRateShading,
    Sparse, Multiview, HdrOutput, MultiDrawIndirect,
    TimestampQuery, OcclusionQuery, PipelineStatisticsQuery,
    // tier 2
    ShaderExecutionReordering, OpacityMicromap, DisplacementMicromap,
    RayTracingMotionBlur, RayTracingPositionFetch,
    CooperativeMatrix, CooperativeVector, DeviceGeneratedCommands,
    // tier 2b
    LowLatency,
    // tier 3 (post-v1)
    ConservativeRaster, FragmentShaderInterlock, FragmentDensityMap,
    SampleLocations, DepthBounds,
    // infrastructure
    ShaderObject,
}
```

---

## 3. Device

```rust
pub struct DeviceDescription<'a> {
    pub queues:     &'a [QueueRequest],
    pub extensions: &'a [&'a CStr],
    /// Raw `pNext` injection at device creation. The ONE hook a companion crate cannot
    /// retrofit — this is what makes host-side DLSS/NRD possible without a raw dependency.
    pub feature_chain: Option<&'a mut dyn FeatureChain>,
    /// 0 = let Nevarea choose. RUNTIME, never a compile-time constant.
    pub frames_in_flight: u32,
}

pub unsafe trait FeatureChain {
    /// Append backend feature structs to the device creation chain.
    unsafe fn append(&mut self, chain: &mut dyn core::any::Any);
}

/// F4 — was referenced everywhere and never defined.
pub struct QueueRequest<'a> {
    pub kind:    QueueKind,
    pub count:   u32,
    /// Family must support presenting to this surface. None = headless / don't care.
    pub present: Option<&'a Surface>,
}

impl<'a> QueueRequest<'a> {
    pub fn graphics() -> Self;
    pub fn compute()  -> Self;
    pub fn transfer() -> Self;
    pub fn with_present(self, s: &'a Surface) -> Self;
    pub fn count(self, n: u32) -> Self;
}

impl Device {
    pub fn queue(&self, kind: QueueKind) -> Option<Queue>;
    pub fn adapter(&self) -> &Adapter;

    // --- resources
    pub fn create_buffer(&self, d: &BufferDescription) -> Result<Buffer>;
    pub fn create_image(&self, d: &ImageDescription) -> Result<Image>;
    pub fn create_sampler(&self, d: &SamplerDescription) -> Result<Sampler>;
    pub fn create_acceleration_structure(&self, d: &AccelDescription) -> Result<AccelStruct>;

    // --- shaders (no `Pipeline` for graphics/compute — shader objects only)
    pub fn create_shaders(&self, d: &ShaderSetDescription) -> Result<ShaderSet>;
    pub fn create_ray_tracing_pipeline(&self, d: &RayTracingPipelineDescription)
        -> Result<RayTracingPipeline>;

    // --- uploads (staging is the only native path -> KEEP, heavy boilerplate is fine)
    // F7: carries a SubmitValue so the caller chooses to stall or pipeline against other work.
    // H2: uploads run on the TRANSFER queue, so under EXCLUSIVE sharing (G5) the consuming
    //     queue MUST acquire ownership first. The upload hands you that barrier — it cannot
    //     be forgotten silently because the type is #[must_use].
    pub fn upload_buffer(&self, dst: &Buffer, data: &[u8]) -> Result<BufferUpload>;
    pub fn upload_image(&self, dst: &Image, data: &[u8], r: ImageRegion) -> Result<ImageUpload<'_>>;
    /// I2 — batched scene-loading path; coalesces N uploads into one barrier call.
    pub fn upload_batch(&self) -> UploadBatch<'_>;
    pub fn readback_buffer(&self, src: &Buffer, out: &mut [u8]) -> Result<()>;   // inherently blocking

    // --- commands
    // H3: command lists come from a QUEUE, never the device — see Queue below.
    pub fn create_timeline(&self, initial: u64) -> Result<Timeline>;
    pub fn create_query_pool(&self, kind: QueryKind, count: u32) -> Result<QueryPool>;

    // --- swapchain
    pub fn create_swapchain(&self, s: &Surface, d: &SwapchainDescription) -> Result<Swapchain>;

    // --- lifetime (§4.9)
    /// Drain the retirement queue. Called automatically on submit and present.
    pub fn collect(&self);
    pub fn wait(&self, v: SubmitValue) -> Result<()>;
    pub fn wait_idle(&self) -> Result<()>;

    // --- latency protocol (Reflex, §4.10 tier 2b). No-op where unsupported.
    pub fn latency_sleep(&self) -> Result<()>;
    pub fn set_latency_marker(&self, m: LatencyMarker);

    // --- tooling / observability (design §4.9c)
    pub fn memory_report(&self) -> MemoryReport;
    /// GPU<->CPU correlation. Required to align GPU zones with CPU zones in a profiler.
    pub fn calibrate_timestamps(&self) -> Result<Calibration>;
    pub fn set_instrument(&self, i: Arc<dyn Instrument>);
}

/// I1 — BUFFERS ARE `CONCURRENT`, so there is no acquire and no `#[must_use]`.
/// See §9: only images pay for exclusivity.
pub struct BufferUpload { /* .. */ }
impl BufferUpload { pub fn value(&self) -> SubmitValue; }

/// Images stay EXCLUSIVE (compression), so the consuming queue must acquire ownership.
#[must_use = "the consuming queue must acquire ownership of an uploaded image — record acquire()"]
pub struct ImageUpload<'a> { /* .. */ }

impl<'a> ImageUpload<'a> {
    pub fn value(&self) -> SubmitValue;
    pub fn acquire(&self, q: QueueId, to: ImageState) -> ImageBarrier<'a>;
}

/// I2 — the scene-loading path. ~1000 single-shot uploads would mean ~1000 `#[must_use]`
/// values and ~1000 separate barriers; the batch coalesces them into ONE barrier call.
impl Device {
    pub fn upload_batch(&self) -> UploadBatch<'_>;
}

pub struct UploadBatch<'a> { /* .. */ }

impl<'a> UploadBatch<'a> {
    pub fn buffer(&mut self, dst: &'a Buffer, data: &[u8]);
    /// `to` is per-image: different textures legitimately want different destination states.
    pub fn image(&mut self, dst: &'a Image, data: &[u8], r: ImageRegion, to: ImageState);
    /// One transfer submit for the whole batch.
    pub fn finish(self) -> Result<UploadedBatch<'a>>;
}

#[must_use = "uploaded images must be acquired by the consuming queue — record acquire_all()"]
pub struct UploadedBatch<'a> { /* .. */ }

impl<'a> UploadedBatch<'a> {
    pub fn value(&self) -> SubmitValue;
    /// All image acquires, each using the per-image `to` state given at batch time.
    /// Buffers need none (I1). Feed straight into `cmd.barrier(&[], ..)`.
    pub fn acquire_all(&self, q: QueueId) -> Vec<ImageBarrier<'a>>;
}

#[derive(Copy, Clone, Debug)]
pub enum LatencyMarker {
    SimulationStart, SimulationEnd,
    RenderSubmitStart, RenderSubmitEnd,
    PresentStart, PresentEnd,
    InputSample,
}
```

---

## 4. Queues & submission

```rust
#[derive(Copy, Clone, PartialEq, Debug)]
pub enum QueueKind { Graphics, Compute, Transfer, VideoDecode, VideoEncode }

/// Opaque: internally a timeline value OR a swapchain binary semaphore.
/// This is what keeps swapchain sync at ZERO extra submits (design §4.11).
pub struct Wait<'a>   { /* private */ }
pub struct Signal<'a> { /* private */ }

impl<'a> Wait<'a> {
    pub fn timeline(t: &'a Timeline, value: u64) -> Self;
    /// F3 — pipeline stage the wait applies at. Without this the only safe default is
    /// ALL_COMMANDS, which over-serializes every frame. Default: ALL_COMMANDS.
    pub fn at(self, s: Stage) -> Self;
}
impl<'a> Signal<'a> {
    pub fn timeline(t: &'a Timeline, value: u64) -> Self;
    pub fn at(self, s: Stage) -> Self;
}

// I5 — BITFLAGS, not an enum. A wait routinely applies at several stages
// (COLOR_ATTACHMENT_OUTPUT | FRAGMENT_SHADER) and VkPipelineStageFlags2 is a mask.
// As a plain enum, F3's fix could not express the common case and would silently
// force over-broad waits — reintroducing the over-serialization F3 removed.
bitflags::bitflags! {
    #[derive(Copy, Clone, PartialEq, Debug)]
    pub struct Stage: u64 {
        const ALL_COMMANDS             = 1 << 0;
        const TOP_OF_PIPE              = 1 << 1;
        const BOTTOM_OF_PIPE           = 1 << 2;
        const VERTEX_SHADER            = 1 << 3;
        const FRAGMENT_SHADER          = 1 << 4;
        const COMPUTE_SHADER           = 1 << 5;
        const COLOR_ATTACHMENT_OUTPUT  = 1 << 6;
        const EARLY_FRAGMENT_TESTS     = 1 << 7;
        const LATE_FRAGMENT_TESTS      = 1 << 8;
        const TRANSFER                 = 1 << 9;
        const RAY_TRACING_SHADER       = 1 << 10;
        const ACCEL_STRUCTURE_BUILD    = 1 << 11;
        const TASK_SHADER              = 1 << 12;
        const MESH_SHADER              = 1 << 13;
    }
}

/// F1 — OWNING, and passed BY VALUE. Submitting consumes the lists, which makes
/// double-submit and record-after-submit impossible, and hands them to the retirement
/// queue so the command pool is reset exactly when the timeline passes (design §4.9).
/// I4 — `lists` is PRIVATE. A public Vec would let `lists.insert(3, other)` split a
/// parallel run, which would make H5's newtype purely decorative.
pub struct SubmitDescription<'a> {
    lists:   Vec<CommandList>,        // executed in order — never reordered (R3)
    waits:   Vec<Wait<'a>>,
    signals: Vec<Signal<'a>>,
}

impl<'a> SubmitDescription<'a> {
    pub fn new() -> Self;
    pub fn wait(self, w: Wait<'a>) -> Self;
    pub fn signal(self, s: Signal<'a>) -> Self;
    pub fn push(self, list: CommandList) -> Self;
    /// H5 — a parallel render pass must be submitted as an UNBROKEN, ordered run
    /// (suspend/resume is only valid across consecutive lists). Taking `ParallelLists`
    /// whole makes splicing other lists into the run impossible.
    pub fn extend_parallel(self, lists: ParallelLists) -> Self;
}

/// A point in a SPECIFIC queue's timeline.
/// G1 — a single device-wide counter is unsound across queues: queue B signalling 6
/// before queue A signals 5 would make `wait(6)` falsely imply 5 completed.
#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub struct SubmitValue { pub queue: QueueId, pub value: u64 }

impl Queue {
    pub fn submit(&self, d: SubmitDescription) -> Result<SubmitValue>;   // by value
    pub fn kind(&self) -> QueueKind;
    pub fn id(&self) -> QueueId;

    /// H3 — command lists are created FROM A QUEUE, never from the device. The queue family
    /// is then implied by construction, so submitting a compute-family list to a graphics
    /// queue (UB) is unrepresentable rather than merely validated.
    pub fn create_command_list(&self) -> Result<CommandList>;

    /// One render pass recorded across `parts` threads. See §7.
    pub fn begin_parallel_rendering(&self, d: &RenderingDescription, parts: u32)
        -> Result<ParallelRendering>;
}

impl Timeline {
    pub fn value(&self) -> Result<u64>;
    pub fn wait(&self, value: u64, timeout: Duration) -> Result<()>;
    pub fn signal(&self, value: u64) -> Result<()>;
}
```

---

## 5. Resources

Every resource is RAII with a reachable raw handle (design §4.3). `Drop` enqueues GPU-serial retirement.

```rust
/// J1 — no `bindless_index()` here either: images have two, buffers have none
/// (they are used by address). The trait exists for `keep_alive`, not for indices.
pub trait Resource: private::Sealed {
    fn debug_name(&self) -> Option<&str>;
}

pub struct BufferDescription {
    pub size:   u64,
    pub usage:  BufferUsage,     // bitflags
    pub memory: MemoryKind,      // DeviceLocal | HostVisible | HostCoherent | Readback
    pub name:   Option<&'static str>,   // debug label
}

impl Buffer {
    /// §1.1 — buffers are used BY ADDRESS. Does NOT participate in lifetime tracking (§4.9b).
    pub fn device_address(&self) -> u64;
    pub fn size(&self) -> u64;
    pub fn map(&self) -> Result<MappedSlice<'_>>;   // HostVisible only
    pub fn handle(&self) -> BufferHandle;
}

pub struct ImageDescription {
    pub extent:  [u32; 3],
    pub format:  Format,
    pub usage:   ImageUsage,
    pub mips:    u32,
    pub layers:  u32,
    pub samples: u32,
    pub name:    Option<&'static str>,
}

impl Image {
    /// §1.1 — textures are used BY INDEX. Does NOT participate in lifetime tracking (§4.9b).
    /// F5 — Option: swapchain images are not bindless-registered and cannot be.
    ///
    /// J1 — TWO indices, not one. An image used as both sampled and storage occupies two
    /// DIFFERENT descriptor arrays at two binding points (binding 0 sampled, binding 1
    /// storage, binding 2 samplers — the §6.1 global layout). A single `bindless_index()`
    /// has no correct answer and would silently index the wrong array in the shader,
    /// so it does not exist.
    pub fn sampled_index(&self) -> Option<u32>;
    pub fn storage_index(&self) -> Option<u32>;
    pub fn extent(&self) -> [u32; 3];
    pub fn format(&self) -> Format;
    /// G4 — internally CACHED by range. Calling this per-frame in a loop must not churn
    /// or leak. Cache lifetime is tied to the image (STRATEGY: invisible, no semantic effect).
    pub fn view(&self, r: SubresourceRange) -> Result<&ImageView>;
    pub fn handle(&self) -> ImageHandle;
}

impl Sampler    { pub fn bindless_index(&self) -> u32; }
impl AccelStruct { pub fn device_address(&self) -> u64; }
```

---

## 6. Shaders & render state

**No `Pipeline` type for graphics/compute** — shader objects only (design §4.10).

```rust
#[derive(Copy, Clone, PartialEq, Debug)]
pub enum ShaderStage {
    Vertex, Fragment, Compute,
    Task, Mesh,
    TessControl, TessEval,
}

pub struct ShaderCode<'a> {
    pub spirv: &'a [u32],       // opaque blob — Nevarea ships NO shader compiler
    pub entry: &'a str,
}

pub struct ShaderSetDescription<'a> {
    pub stages: &'a [(ShaderStage, ShaderCode<'a>)],
    // F2/G3: no `push_constants` field. The push range lives in the ONE global layout
    // below and is sized to the device maximum, so a per-set size would be both
    // redundant and a source of cross-set mismatch errors.
}

pub struct ShaderSet { /* .. */ }
impl ShaderSet { pub fn is_emulated(&self) -> bool; }   // layer vs native (§4.10)
```

### 6.1 The global layout — F2

The API depends on this everywhere and never stated it. **Nevarea creates exactly ONE pipeline layout for
the entire device**, and every shader set, RT pipeline, and dispatch uses it:

- **set 0** = the bindless descriptor set (sampled images, storage images, samplers, accel structures)
- **one push-constant range**, `stageFlags = ALL`, sized to `Limits::max_push_constants`

Consequences that are now explicit rather than implied:

1. **The bindless set is bound once per command list**, automatically, by Nevarea. Nothing in the public API
   binds it — there is no binding model (§1.1), and `bindless_index()` would otherwise reference nothing.
2. `push_constants` needs no stage argument — the range is `ALL` by construction.
3. Every shader must declare the same descriptor set layout. This is a **contract on user shaders**, and
   Nevarea should publish the exact GLSL/Slang declaration block for users to include.
4. There is no per-shader-set push size, so no cross-set mismatch is possible (G3).

```rust
impl Limits { pub fn max_push_constants(&self) -> u32; }
```

```rust
```

All render state is dynamic (shader objects require it). One struct, one call — Nevarea diffs
internally and issues only what changed (STRATEGY).

```rust
#[derive(Clone, Default)]
pub struct RenderState<'a> {
    pub topology:          Topology,
    pub primitive_restart: bool,                            // J5
    pub polygon:           PolygonMode,
    pub line_width:        f32,                             // J5
    pub cull:              CullMode,
    pub front_face:        FrontFace,
    pub depth_test:        bool,
    pub depth_write:       bool,
    pub depth_compare:     CompareOp,
    pub depth_bias:        Option<DepthBias>,
    pub depth_clamp:       bool,                            // J5
    pub depth_bounds:      Option<core::ops::Range<f32>>,   // tier 3, gated
    pub stencil:           Option<StencilState>,            // J5 — front/back ops, masks, ref
    pub blend:             &'a [Option<BlendState>],        // one per colour attachment
    pub logic_op:          Option<LogicOp>,                 // J5
    /// Per-draw VRS. Image-based VRS is on the PASS (J2); per-primitive is a shader output.
    pub shading_rate:      Option<ShadingRate>,
}
```

---

## 7. Command recording

```rust
/// F6 — split into two traits. RayTracingPass binds a PIPELINE, not shaders, so folding
/// `bind_shaders` into one trait forced RT to re-declare push_constants/keep_alive —
/// re-creating the very duplication Encoder exists to remove.

/// Implemented by ALL three passes.
pub trait Encoder {
    fn push_constants(&mut self, data: &[u8]);
    /// Retain `r` until this list's GPU work completes. REQUIRED when a resource is
    /// referenced only via a device address or bindless index (§4.9b) — Nevarea cannot see those.
    fn keep_alive(&mut self, r: &impl Resource);
}

/// Render + compute only (shader-object domains).
pub trait ShaderBind: Encoder {
    fn bind_shaders(&mut self, s: &ShaderSet);
}

impl CommandList {
    // --- state transitions (explicit, per-subresource — design R2)
    pub fn barrier(&mut self, buffers: &[BufferBarrier], images: &[ImageBarrier]);

    // --- transfer
    pub fn copy_buffer(&mut self, src: &Buffer, dst: &Buffer, regions: &[BufferCopy]);
    pub fn copy_image(&mut self, src: &Image, dst: &Image, regions: &[ImageCopy]);
    pub fn blit(&mut self, src: &Image, dst: &Image, r: BlitRegion, filter: Filter);
    pub fn fill_buffer(&mut self, dst: &Buffer, offset: u64, size: u64, value: u32);

    // --- passes
    pub fn begin_rendering(&mut self, d: &RenderingDescription) -> RenderPass<'_>;
    pub fn begin_compute(&mut self) -> ComputePass<'_>;
    pub fn begin_ray_tracing(&mut self) -> RayTracingPass<'_>;

    // --- acceleration structures
    pub fn build_acceleration_structure(&mut self, d: &AccelBuildDescription);

    // --- queries
    pub fn reset_queries(&mut self, pool: &QueryPool, range: Range<u32>);
    pub fn write_timestamp(&mut self, pool: &QueryPool, index: u32, stage: Stage);
    pub fn begin_query(&mut self, pool: &QueryPool, index: u32);
    pub fn end_query(&mut self, pool: &QueryPool, index: u32);

    // --- debug
    pub fn push_label(&mut self, name: &str, color: [f32; 4]);
    pub fn pop_label(&mut self);

    pub fn keep_alive(&mut self, r: &impl Resource);
}
```

### Render pass

```rust
pub struct RenderingDescription<'a> {
    pub color:     &'a [ColorAttachment<'a>],
    pub depth:     Option<DepthAttachment<'a>>,
    pub stencil:   Option<StencilAttachment<'a>>,       // J3
    pub area:      Rect2D,
    pub view_mask: u32,        // multiview: one draw -> N layers. 0 = off.
    /// J2 — image-based VRS. Distinct from per-draw (`RenderState::shading_rate`) and
    /// from per-primitive (a shader output). Three separate mechanisms; the API models
    /// all three rather than implying one covers the rest.
    pub shading_rate_attachment: Option<&'a ImageView>,
}

pub struct ColorAttachment<'a> {
    pub view:    &'a ImageView,
    pub load:    Load,          // Clear([f32;4]) | Load | DontCare
    pub store:   Store,         // Store | DontCare
    pub resolve: Option<&'a ImageView>,
}

/// J3 — depth resolve is standard with MSAA and was missing.
pub struct DepthAttachment<'a> {
    pub view:         &'a ImageView,
    pub load:         Load,
    pub store:        Store,
    pub resolve:      Option<&'a ImageView>,
    pub resolve_mode: ResolveMode,     // SampleZero | Min | Max | Average
}

pub struct StencilAttachment<'a> {
    pub view:  &'a ImageView,
    pub load:  Load,
    pub store: Store,
}

pub struct RenderPass<'a> { /* Drop ends rendering */ }

impl Encoder for RenderPass<'_> { /* .. */ }

impl RenderPass<'_> {
    pub fn set_render_state(&mut self, s: &RenderState);
    pub fn set_viewport(&mut self, vp: Viewport);      // NOT swapchain-derived (R3)
    pub fn set_scissor(&mut self, r: Rect2D);

    pub fn bind_index_buffer(&mut self, b: &Buffer, offset: u64, ty: IndexType);

    pub fn draw(&mut self, vertices: Range<u32>, instances: Range<u32>);
    pub fn draw_indexed(&mut self, indices: Range<u32>, base_vertex: i32, instances: Range<u32>);
    pub fn draw_indirect(&mut self, args: &Buffer, offset: u64, count: u32, stride: u32);
    pub fn draw_indexed_indirect(&mut self, args: &Buffer, offset: u64, count: u32, stride: u32);
    /// GPU-driven: draw count itself lives in a buffer.
    pub fn draw_indirect_count(&mut self, args: &Buffer, ao: u64,
                               count: &Buffer, co: u64, max: u32, stride: u32);

    /// Mesh shading. Feature::MeshShader.
    pub fn draw_mesh_tasks(&mut self, x: u32, y: u32, z: u32);
    pub fn draw_mesh_tasks_indirect(&mut self, args: &Buffer, offset: u64, count: u32, stride: u32);
}
```

### Parallel rendering — G2 RESOLVED

One render pass, recorded across N threads. **Nevarea owns the correctness**, the user cannot mispair
anything.

```rust
impl Queue {
    /// I3 — hands out the parts DIRECTLY. Everything below is consuming, so a part
    /// cannot be recorded twice and the assembled run cannot be reused.
    pub fn begin_parallel_rendering(&self, d: &RenderingDescription, parts: u32)
        -> Result<Vec<RenderPart>>;
}

impl RenderPart {
    pub fn index(&self) -> u32;
    /// H1 — the closure form is LOAD-BEARING, not sugar. The command list is allocated
    /// from the CALLING thread's pool inside `record`, so allocation and recording
    /// provably happen on the same thread. Allocating all parts up front on the spawning
    /// thread would mean N threads recording from ONE pool — undefined behaviour.
    /// I3 — takes `self`: recording a part twice is unrepresentable.
    pub fn record<F>(self, f: F) -> Result<RecordedPart>
    where F: FnOnce(&mut RenderPass<'_>);
}

pub struct RecordedPart { /* .. */ }

/// H5 — a newtype, not Vec<CommandList>: the run must stay unbroken and ordered.
pub struct ParallelLists { /* private */ }

impl ParallelLists {
    /// Validates that every part is present exactly once, orders them, and sets
    /// SUSPENDING/RESUMING. Errors on a missing or duplicated part.
    pub fn assemble(parts: Vec<RecordedPart>) -> Result<Self>;
}
```

```rust
let parts = queue.begin_parallel_rendering(&desc, n)?;

let recorded: Vec<RecordedPart> = parts.into_par_iter()
    .map(|part| {
        let i = part.index();
        part.record(|pass| {                       // allocates on THIS thread
            pass.set_render_state(&state);
            for obj in chunk(i) { pass.push_constants(..); pass.draw(..); }
        })
    })
    .collect::<Result<_>>()?;

queue.submit(
    SubmitDescription::new()
        .wait(frame.ready().at(Stage::COLOR_ATTACHMENT_OUTPUT))
        .signal(frame.present_signal())
        .extend_parallel(ParallelLists::assemble(recorded)?)
)?;
```

**What Nevarea handles that a user would get wrong:** `VK_RENDERING_SUSPENDING_BIT` on every part but the
last, `RESUMING_BIT` on every part but the first, and `Load::Clear` applied to **part 0 only** — later parts
are silently promoted to `Load::Load`, otherwise the clear repeats and erases prior parts' work.

**Portability — why this shape and not raw suspend/resume:** suspend/resume is a Vulkan-specific *spelling*.
Metal has `MTLParallelRenderCommandEncoder` for precisely this; D3D12 records into multiple command lists
with no render-pass-instance concept at all. Modelling it keeps the public API backend-neutral; exposing the
flags would hard-code Vulkan into the surface and break §0's multi-API goal.

### Compute pass

```rust
pub struct ComputePass<'a> { /* .. */ }
impl Encoder for ComputePass<'_> { /* .. */ }

impl ComputePass<'_> {
    pub fn dispatch(&mut self, x: u32, y: u32, z: u32);
    pub fn dispatch_indirect(&mut self, args: &Buffer, offset: u64);
}
```

### Ray tracing pass

RT is the one domain that keeps pipelines — shader objects cannot express it.

```rust
pub struct RayTracingPipelineDescription<'a> {
    pub raygen:      ShaderCode<'a>,
    pub miss:        &'a [ShaderCode<'a>],
    pub hit_groups:  &'a [HitGroup<'a>],
    pub callable:    &'a [ShaderCode<'a>],
    pub max_recursion: u32,
    pub push_constants: u32,
}

pub struct RayTracingPass<'a> { /* .. */ }

impl Encoder for RayTracingPass<'_> { /* push_constants + keep_alive — F6, not re-declared */ }

impl RayTracingPass<'_> {
    pub fn bind_pipeline(&mut self, p: &RayTracingPipeline);
    pub fn trace_rays(&mut self, w: u32, h: u32, d: u32);
    pub fn trace_rays_indirect(&mut self, args: &Buffer, offset: u64);
}

// --- F8: query readback (the pool was write-only)
impl QueryPool {
    /// J4 — blocking behaviour is explicit. Every real profiler reads timestamps a frame
    /// or two later, so `NonBlocking` returning `None` is the common path, not an error.
    pub fn results(&self, range: Range<u32>, mode: ResultMode) -> Result<Option<Vec<u64>>>;
    /// Nanoseconds per timestamp tick, from Limits. Needed to interpret timestamps.
    pub fn timestamp_period(&self) -> f32;
}

#[derive(Copy, Clone, PartialEq, Debug)]
pub enum ResultMode { Block, NonBlocking }
```

---

## 8. Swapchain

```rust
pub struct SwapchainDescription {
    pub format:      Option<Format>,      // None = Nevarea picks
    pub color_space: ColorSpace,          // Srgb | Hdr10 | scRGB  (Feature::HdrOutput)
    pub present_mode: PresentMode,        // Fifo | Mailbox | Immediate
    pub image_count: Option<u32>,
}

/// OUT_OF_DATE / SUBOPTIMAL are API states, not errors to guess at (design §4.11).
pub enum Acquired {
    Frame(Frame),
    Suboptimal(Frame),
    OutOfDate,
}

/// Mirrors `Acquired` — present reports the same conditions.
pub enum PresentOutcome { Ok, Suboptimal, OutOfDate }

impl Swapchain {
    pub fn acquire(&mut self) -> Result<Acquired>;
    pub fn present(&mut self, frame: Frame) -> Result<PresentOutcome>;
    /// Stall-free: reuses the old swapchain internally.
    pub fn resize(&mut self, extent: [u32; 2]) -> Result<()>;
    pub fn format(&self) -> Format;
    pub fn extent(&self) -> [u32; 2];
}

impl Frame {
    pub fn view(&self) -> &ImageView;
    pub fn image(&self) -> &Image;
    pub fn extent(&self) -> [u32; 2];
    /// Pass as a submit wait. Internally the acquire binary semaphore — zero extra submits.
    pub fn ready(&self) -> Wait<'_>;
    /// Pass as a submit signal on the LAST submit that touches this frame.
    pub fn present_signal(&self) -> Signal<'_>;
}
```

---

## 9. Barriers (explicit, per-subresource — R2)

```rust
pub struct ImageBarrier<'a> {
    pub image:  &'a Image,
    pub range:  SubresourceRange,       // per-subresource, NOT whole-image
    pub from:   ImageState,
    pub to:     ImageState,
    /// G5 — queue-family ownership. Never constructed by hand; see `transfer()` below.
    pub(crate) transfer: Option<QueueTransfer>,
}

/// G5 RESOLVED, refined by I1 — a release/acquire pair, produced together so a mismatch
/// is unconstructible.
///
/// **I1 — the sharing-mode ruling is SPLIT, because the cost is not uniform:**
/// - **Buffers → `VK_SHARING_MODE_CONCURRENT`.** Buffers carry no DCC/colour compression,
///   so concurrent access is essentially free. No ownership transfer ever; `BufferBarrier`
///   has no queue fields. This also removes a gap nothing had caught: `readback_buffer`
///   runs on the transfer queue and would otherwise have needed a *release* from whichever
///   queue last wrote the buffer — impossible for Nevarea to record on the user's behalf.
/// - **Images → `VK_SHARING_MODE_EXCLUSIVE` + the paired transfer below.** Here CONCURRENT
///   really does disable DCC/delta colour compression on AMD and NVIDIA, which fails the
///   performance bar.
impl ImageBarrier<'_> {
    /// Returns (release, acquire). Record `release` on the source queue's list and
    /// `acquire` on the destination queue's. Neither half is constructible alone.
    ///
    /// H4 — KNOWN LIMITATION. The pair is constructed safely but not *consumed* safely:
    /// nothing forces both halves to actually be recorded, and dropping the acquire
    /// yields a hang or a validation error. Rust has no linear types, so this cannot be
    /// fully enforced. Mitigations: `#[must_use]` below, plus debug-build tracking that
    /// every release has a matching acquire recorded before submit. This is the one place
    /// the paired-barrier design does not fully deliver — stated rather than hidden.
    #[must_use = "BOTH halves must be recorded — dropping the acquire will hang or corrupt"]
    pub fn transfer<'a>(
        image: &'a Image, range: SubresourceRange,
        from: ImageState, to: ImageState,
        from_queue: QueueId, to_queue: QueueId,
    ) -> (ImageBarrier<'a>, ImageBarrier<'a>);
}
```

Needed whenever a resource is written on compute/transfer and read on graphics. Async compute and async
transfer are both Tier 1, so this is not optional.

**Portability:** D3D12 and Metal have no queue-ownership concept — both halves compile to no-ops there.
The abstraction degrades rather than leaking Vulkan into the surface.

```rust

/// State, not raw layout+stage+access. Nevarea derives the sync2 triple (STRATEGY).
#[derive(Copy, Clone, PartialEq, Debug)]
pub enum ImageState {
    Undefined, ColorAttachment, DepthAttachment, DepthReadOnly,
    ShaderRead, ShaderWrite, TransferSrc, TransferDst, Present, General,
}
```

Convenience constructors (`ImageBarrier::to_present(..)` etc.) are **sugar over the public struct** —
never the only path (design §7.3 finding 5).

---

## 10. Marker traits — this table IS the threading design

| Type | Bounds |
|---|---|
| `Instance`, `Adapter`, `Device`, `Queue` | `Send + Sync` |
| `Buffer`, `Image`, `Sampler`, `AccelStruct`, `ShaderSet`, `Timeline` | `Send + Sync` |
| **`CommandList`** | **`Send + !Sync`** — cross-thread recording is a compile error |
| `RenderPass`, `ComputePass`, `RayTracingPass` | `!Send + !Sync` (borrow the list) |
| `Swapchain`, `Frame` | `Send + !Sync` |

---

## 11. `nevarea-raw` (opt-in crate)

```rust
pub trait DeviceRawExt   { fn vk(&self) -> VulkanDevice<'_>; }
pub trait BufferRawExt   {
    fn vk_buffer(&self) -> vk::Buffer;
    fn into_raw(self) -> BufferHandle;
    unsafe fn from_raw(d: &Device, h: BufferHandle) -> Self;
}
pub trait CommandListRawExt { fn vk_command_buffer(&mut self) -> vk::CommandBuffer; }
pub trait ErrorRawExt    { fn vk_result(&self) -> Option<vk::Result>; }
```

---

## 12. Not yet specified — honest gaps

1. **Sparse / tiled resources** (tier 1) — needs its own binding API (`queue.bind_sparse`); not drafted.
2. **Device-generated commands** (tier 2) — indirect command layout types not drafted.
3. **Cooperative matrix / vector** (tier 2) — may be shader-side only, needing just a capability query.
4. **Micromaps** (tier 2) — additional `AccelBuildDescription` inputs, not drafted.
5. **Video queues** — `QueueKind::VideoDecode/Encode` exist; everything past that is `nevarea-raw` (by design).
6. **`Format` / `BufferUsage` / `ImageUsage` / `Limits`** — enumerations elided here for length.
7. **`MappedSlice` unmap semantics** and host-coherent flush rules — undecided.

## 13. Falsification pass — run 2026-08-11

Four sketches written against §1–§12 above. **Eight findings; three are serious.** Sketches are shown as
they *should* read once the findings are applied.

### 13.1 Triangle

```rust
let instance = Instance::new(&InstanceDescription {
    app_name: "triangle", validation: cfg!(debug_assertions),
    extensions: &[], layers: &[],
})?;

let surface = instance.create_surface(&window)?;             // BEFORE adapter pick — see F4

let adapter = instance.adapters().into_iter()
    .find(|a| a.info().kind == DeviceKind::Discrete && a.supports_present(&surface))
    .ok_or(Error::InitializationFailed)?;

let device = adapter.create_device(&DeviceDescription {
    queues: &[QueueRequest::graphics().with_present(&surface)],   // F4
    extensions: &[], feature_chain: None, frames_in_flight: 2,
})?;
let queue = device.queue(QueueKind::Graphics).unwrap();

let verts: [[f32; 4]; 3] = [ /* .. */ ];
let vbuf = device.create_buffer(&BufferDescription {
    size: size_of_val(&verts) as u64,
    usage: BufferUsage::STORAGE | BufferUsage::TRANSFER_DST,
    memory: MemoryKind::DeviceLocal, name: Some("triangle-verts"),
})?;
device.wait(device.upload_buffer(&vbuf, bytemuck::bytes_of(&verts))?)?;   // F7: returns SubmitValue

let shaders = device.create_shaders(&ShaderSetDescription {
    stages: &[(ShaderStage::Vertex,   ShaderCode { spirv: VS, entry: "main" }),
              (ShaderStage::Fragment, ShaderCode { spirv: FS, entry: "main" })],
    push_constants: size_of::<Push>() as u32,
})?;

let mut swapchain = device.create_swapchain(&surface, &SwapchainDescription::default())?;

loop {
    let frame = match swapchain.acquire()? {
        Acquired::Frame(f) | Acquired::Suboptimal(f) => f,
        Acquired::OutOfDate => { swapchain.resize(window.size())?; continue; }
    };

    let mut cmd = device.create_command_list(QueueKind::Graphics)?;
    cmd.barrier(&[], &[ImageBarrier {
        image: frame.image(), range: SubresourceRange::ALL,
        from: ImageState::Undefined, to: ImageState::ColorAttachment,
    }]);
    {
        let mut pass = cmd.begin_rendering(&RenderingDescription {
            color: &[ColorAttachment { view: frame.view(),
                     load: Load::Clear([0.0, 0.0, 0.0, 1.0]), store: Store::Store, resolve: None }],
            depth: None, area: Rect2D::from(frame.extent()), view_mask: 0,
        });
        pass.bind_shaders(&shaders);
        pass.set_render_state(&RenderState::default());
        pass.set_viewport(Viewport::full(frame.extent()));
        pass.push_constants(bytemuck::bytes_of(&Push { vertices: vbuf.device_address() }));
        pass.keep_alive(&vbuf);          // §4.9b — referenced only by address
        pass.draw(0..3, 0..1);
    }
    cmd.barrier(&[], &[ImageBarrier { image: frame.image(), range: SubresourceRange::ALL,
        from: ImageState::ColorAttachment, to: ImageState::Present }]);

    queue.submit(SubmitDescription {                       // F1: BY VALUE, consumes
        lists:   vec![cmd],
        waits:   vec![frame.ready().at(Stage::ColorAttachmentOutput)],   // F3: stage mask
        signals: vec![frame.present_signal()],
    })?;
    swapchain.present(frame)?;
}
```

### 13.2 Compute + 13.3 ray tracing

```rust
let mut cmd = device.create_command_list(QueueKind::Compute)?;
cmd.barrier(&[], &[ImageBarrier { image: &target, range: SubresourceRange::ALL,
    from: ImageState::Undefined, to: ImageState::ShaderWrite }]);
{
    let mut pass = cmd.begin_compute();
    pass.bind_shaders(&trace_shaders);
    pass.push_constants(bytemuck::bytes_of(&Push {
        target: target.bindless_index().expect("storage image"),   // F5: Option
        scene:  scene_buf.device_address(),
    }));
    pass.keep_alive(&scene_buf);
    pass.dispatch(1920 / 8, 1080 / 8, 1);
}
{
    let mut pass = cmd.begin_ray_tracing();
    pass.bind_pipeline(&rt_pipeline);
    pass.push_constants(bytemuck::bytes_of(&rt_push));   // F6: from Encoder, not duplicated
    pass.trace_rays(1920, 1080, 1);
}
let done = queue.submit(SubmitDescription { lists: vec![cmd], waits: vec![], signals: vec![] })?;
device.wait(done)?;
```

### 13.4 Multithreaded record + single submit

```rust
let lists: Vec<CommandList> = chunks.par_iter().map(|chunk| {
    let mut cmd = device.create_command_list(QueueKind::Graphics)?;   // thread-local pool
    let mut pass = cmd.begin_rendering(&desc);
    for obj in chunk { pass.push_constants(..); pass.draw(..); }
    drop(pass);
    Ok(cmd)                                   // CommandList: Send -> moves to submitting thread
}).collect::<Result<Vec<_>>>()?;

queue.submit(SubmitDescription { lists, waits: vec![frame.ready()], signals: vec![] })?;
```

---

### FINDINGS — round 2 (ALL EIGHT APPLIED to §1–§12 above)

**F1 — SERIOUS. `submit` must CONSUME its command lists.**
`lists: &'a [CommandList]` allows submitting the same list twice, and allows recording into it after
submission — undefined behaviour that Rust should be preventing here. Change to `lists: Vec<CommandList>`,
taken by value. Submit moves them into the retirement queue (§4.9), which *also* fixes when the command pool
may be reset: the lists are freed exactly when the timeline passes. One change, three problems solved.
`SubmitDescription` therefore becomes an owning struct (`Vec`, not `&[]`) and is passed by value.

**F2 — SERIOUS. The bindless descriptor set is never bound, and the global layout is unspecified.**
Nothing in the API binds the bindless set, yet every `bindless_index()` depends on it. Implied but never
stated: Nevarea uses **one global pipeline layout** for all shaders (bindless set + push-constant range),
bound once per command list. Must be written down — it constrains `push_constants` (needs
`stageFlags = ALL`) and means `ShaderSetDescription::push_constants` sizes a range in a *shared* layout,
so a mismatch across shader sets is a real error case.

**F3 — SERIOUS. `Wait`/`Signal` carry no pipeline stage mask.**
Vulkan needs `VkSemaphoreSubmitInfo::stageMask`. Without it the only safe default is `ALL_COMMANDS`, which
over-serializes — a real, permanent perf loss, unacceptable at the stated bar. Add
`Wait::timeline(t, v).at(Stage::ColorAttachmentOutput)`, defaulting to `ALL_COMMANDS` only when unspecified.

**F4 — Present-capable queue selection is impossible.**
`QueueRequest::graphics()` cannot express "must present to this surface"
(`vkGetPhysicalDeviceSurfaceSupportKHR`). Add `Adapter::supports_present(&Surface)` and
`QueueRequest::with_present(&Surface)`. Headless stays surface-free. Also: `QueueRequest` is referenced
throughout §3 but never defined — spec it.

**F5 — `Image::bindless_index()` must return `Option<u32>`.**
Swapchain images are not bindless-registered and cannot be. The inherent method returns `u32` while the
`Resource` trait returns `Option<u32>` — inconsistent, and the inherent one is a lie for every `Frame` image.

**F6 — `RayTracingPass` re-duplicates what `Encoder` exists to unify.**
It declares its own `push_constants` and `keep_alive`. Split the traits:
`trait Encoder { push_constants, keep_alive }` for all three passes, and `trait ShaderBind { bind_shaders }`
for render/compute only. RT binds a pipeline instead.

**F7 — `upload_buffer` blocks, with no way not to.**
Returning `Result<()>` forces a stall. Return `Result<SubmitValue>` so the caller chooses to wait or to
pipeline uploads against other work — matters for streaming.

**F8 — `QueryPool` is write-only.** `write_timestamp` / `begin_query` / `end_query` exist, but nothing reads
results back. Add `QueryPool::results(&self, range) -> Result<Vec<u64>>` plus the timestamp period from
`Limits` to convert to nanoseconds.

**Non-finding, noted:** `Encoder::keep_alive(&mut self, r: &impl Resource)` makes the trait non-object-safe.
Fine — nothing needs `dyn Encoder`. Flagged only so it is a choice rather than an accident.

---

## 14. Falsification — round 3 (against the revised surface)

Re-ran the same four sketches after applying F1–F8. **Five findings; two serious.** Two were caught while
applying round 2 and are already fixed above (G1, G3); three are new and NOT yet applied.

**G1 — SERIOUS. APPLIED. A device-wide `SubmitValue` is unsound across queues.**
Found while rewriting `submit`. If queue B signals 6 before queue A signals 5, `wait(6)` falsely implies 5
completed. `SubmitValue` is now `{ queue: QueueId, value: u64 }`.
**Knock-on that still needs writing into design §4.9:** the retirement queue assumed *one clock*. With
multiple queues it must track a per-queue value, and **a resource used on two queues must outlive both** —
retire only when every queue that touched it has passed its recorded value. The current §4.9 text is wrong
for multi-queue.

**G2 — SERIOUS. NOT APPLIED. Sketch 13.4 (parallel recording) is invalid Vulkan.**
N primary command buffers each calling `begin_rendering` on the same attachments creates N *separate*
render pass instances — the clear repeats, and splitting one pass across command buffers legally requires
`VK_RENDERING_SUSPENDING_BIT` / `VK_RENDERING_RESUMING_BIT`. This is *the* parallel-recording pattern for
dynamic rendering, and the API cannot express it at all. Since parallel recording is a headline goal, this
is load-bearing. Options:
- **(a) Expose it:** `RenderingDescription { suspend: bool, resume: bool }` — honest, raw, user sets it.
- **(b) Model it:** a `ParallelPass` handle that hands out per-thread `RenderPass` pieces and sets the flags
  itself. Safer, less raw, more Nevarea-shaped (bucket 1 SETUP).
- **(c) Secondary command buffers** — the older path; rejected earlier for order/inheritance complexity.

**G3 — APPLIED. `ShaderSetDescription::push_constants` was redundant** once F2's single global layout was
written down — the range is device-max and shared. Removed; a per-set size could only ever disagree with it.

**G4 — NOT APPLIED. `Image::view(&self, range) -> Result<ImageView>` allocates a new view per call.**
Called per-frame in a loop it leaks views or churns. Either cache internally keyed on the range (STRATEGY,
invisible) or make view creation explicit and user-held. Caching is the doctrine-consistent answer.

**G5 — NOT APPLIED. Multi-queue barriers cannot express queue-family ownership transfer.**
A resource written on the compute/transfer queue and read on graphics needs an ownership transfer under
`VK_SHARING_MODE_EXCLUSIVE` — `ImageBarrier`/`BufferBarrier` have no `src_queue`/`dst_queue` fields. Two ways
out, and it must be a deliberate ruling:
- **CONCURRENT sharing mode everywhere** — no transfers needed, costs measurable bandwidth on some hardware.
- **Expose transfer** — `ImageBarrier { from_queue: Option<QueueId>, to_queue: Option<QueueId> }`, correct
  and fast, but the user must get the release/acquire pair right on both queues.

Async compute and async transfer are both already Tier 1, so this is not deferrable.

**Also noted (minor):** `SubmitDescription` now allocates three `Vec`s per submit. Fine at frame granularity;
if it ever shows up, `SmallVec` is a drop-in. `PresentOutcome` — **APPLIED**, now defined.

**G2, G4, G5 — ALL APPLIED**, ruled on the stated criteria (safety first, user burden acceptable, fast, AAA,
multi-API). See §7 parallel rendering, §5 image views, §9 barriers.

---

## 15. Portability audit — does this surface actually survive D3D12 and Metal?

Multi-API support is a hard requirement, so every construct is checked, not assumed.

| Nevarea construct | Vulkan | D3D12 | Metal | Verdict |
|---|---|---|---|---|
| Buffer device address (`u64`) | BDA | GPU VA | `gpuAddress` | **clean** |
| Bindless `u32` index | descriptor indexing | descriptor heaps | argument buffers | **clean** |
| One global layout (§6.1) | pipeline layout | root signature | argument buffer layout | **clean** |
| Push constants, `ALL` stages | push constants | root constants | `setBytes` | **clean** |
| `Timeline` | timeline semaphore | `ID3D12Fence` | `MTLSharedEvent` | **clean** |
| Opaque `Wait`/`Signal` | binary *or* timeline | fence values | events | **clean — the opacity is what saves it** |
| `begin_rendering` | dynamic rendering | `OMSetRenderTargets` | render encoder | **clean** |
| `ParallelRendering` | suspend/resume | N command lists | `MTLParallelRenderCommandEncoder` | **clean — Metal has it natively** |
| `ImageState` barriers | sync2 | enhanced barriers | (implicit) | **clean** |
| `QueueTransfer` | ownership transfer | — | — | **degrades to no-op** |
| `SubmitValue { queue, value }` | per-queue timeline | per-queue fence | per-queue event | **clean** |
| Queries | query pool | query heap | counter sample buffer | **clean** |
| **`ShaderSet` (shader objects)** | `VK_EXT_shader_object` | **no equivalent — PSOs** | **no equivalent — pipeline states** | **needs backend PSO caching** |
| SPIR-V blob | native | DXIL | MSL | **user's problem by design (§6)** |

**The one real friction: `ShaderSet` on D3D12/Metal.** Neither has shader objects; both require a baked
pipeline state object. But this is *solvable and invisible*, precisely because the API made all render state
dynamic: those backends cache PSOs keyed on `(ShaderSet, RenderState, attachment formats)` and materialize
them lazily at draw time. The user-facing API does not change. This is the reverse of the usual porting
problem — a pipeline-shaped API cannot be made shader-object-shaped, but a shader-object-shaped API can
always be lowered to pipelines. **The shader-object-only ruling turns out to be the portable choice**, which
was not the reason it was picked.

**Conclusion: the surface is backend-neutral.** Nothing Vulkan-specific leaks except `QueueTransfer`, which
degrades to a no-op. The two constructs most at risk (parallel rendering, shader objects) both resolve
cleanly. No redesign needed for D3D12/Metal — only backend implementation work.

---

## 15b. Tooling & observability

Design rationale in `NEVAREA_RS_DESIGN.md` §4.9c. Four things in core, one optional crate, three cut.

### Device fault — breadcrumbs

The highest-value AAA tool and the one most often missing. When a shipped title TDRs, "device lost" is
useless; you need the draw.

```rust
pub struct DeviceFault {
    /// Last N GPU-side markers actually reached, newest last.
    /// Backed by VK_NV_device_diagnostic_checkpoints / VK_AMD_buffer_marker.
    pub breadcrumbs: Vec<Breadcrumb>,
    /// VK_EXT_device_fault, where available.
    pub vendor_info: Option<String>,
    pub address_faults: Vec<AddressFault>,
}

pub struct Breadcrumb { pub label: String, pub queue: QueueId, pub reached: bool }
```

Nevarea writes markers around passes and draws automatically (debug builds always; release opt-in, since
markers cost a write per draw).

### Memory

```rust
pub struct MemoryReport {
    pub budget: u64, pub usage: u64,        // VK_EXT_memory_budget
    pub allocations: Vec<AllocationInfo>,   // size, kind, debug name
    pub leaked: Vec<AllocationInfo>,        // live at shutdown
}
```

### Instrumentation — the hook Tracy plugs into

A user-side wrapper can only see calls it makes; it cannot see Nevarea's internal submits, staging uploads,
or retirement. This closes that blind spot without a dependency.

```rust
pub trait Instrument: Send + Sync {
    fn cpu_zone(&self, name: &'static str) -> ZoneGuard;
    fn gpu_zone_begin(&self, cmd: &mut CommandList, name: &'static str);
    fn gpu_zone_end(&self, cmd: &mut CommandList);
    fn frame_mark(&self);
    fn plot(&self, name: &'static str, value: f64);
}

pub struct Calibration { pub gpu: u64, pub cpu: u64, pub deviation: u64 }
```

`nevarea-tracy` implements it. Anyone can implement it for Optick, Superluminal, or in-house.

### Logging

No bespoke logging API. Nevarea emits `tracing` spans/events; the app picks a subscriber. Ecosystem standard,
zero API surface.

---

## 16. Falsification — round 4

Targeted at what round 3 changed: consuming `submit`, per-queue `SubmitValue`, `ParallelRendering`,
`QueueTransfer`. **Five findings; two serious.** None applied yet.

### Sketch: async compute → graphics, with ownership transfer

```rust
let (release, acquire) = ImageBarrier::transfer(
    &img, SubresourceRange::ALL,
    ImageState::ShaderWrite, ImageState::ShaderRead,
    compute_q.id(), graphics_q.id());

let mut ccmd = device.create_command_list(QueueKind::Compute)?;   // H2 — wrong owner
{ let mut p = ccmd.begin_compute(); p.bind_shaders(&cs); p.dispatch(240, 135, 1); }
ccmd.barrier(&[], &[release]);
compute_q.submit(SubmitDescription {
    lists: vec![ccmd], waits: vec![], signals: vec![Signal::timeline(&tl, 1)] })?;

let mut gcmd = device.create_command_list(QueueKind::Graphics)?;
gcmd.barrier(&[], &[acquire]);
/* .. sample img .. */
graphics_q.submit(SubmitDescription {
    lists: vec![gcmd],
    waits: vec![Wait::timeline(&tl, 1).at(Stage::FragmentShader)],
    signals: vec![] })?;
```

### FINDINGS

**H1 — SERIOUS. `ParallelRendering` allocates command lists on the WRONG THREAD.**
`device.begin_parallel_rendering(&desc, n)` is called on one thread and must produce `n` command lists — but
they are then *recorded* on `n` different threads. Command buffers must be recorded from the pool they were
allocated from, and pools are externally synchronized: two threads recording from one pool is UB. **This is
exactly the bug the `thread × frames-in-flight` pool rule exists to prevent, and the new API reintroduced
it.** Fix — each part must allocate lazily on its recording thread:
```rust
par.parts_mut().par_iter_mut().for_each(|part| {
    part.record(|pass| { pass.set_render_state(&st); pass.draw(0..3, 0..1); });  // allocates HERE
});
```
`RenderPart::record(&mut self, f: impl FnOnce(&mut RenderPass))` — the closure form is what guarantees
allocation and recording happen on the same thread.

**H2 — SERIOUS. Uploads silently need an acquire barrier that nothing tells the user about.**
`device.upload_buffer/upload_image` run on the **transfer queue**. Under the G5 ruling (resources stay
`EXCLUSIVE`), *every uploaded resource therefore requires a queue-ownership acquire before first use on
graphics/compute* — and the API neither performs nor mentions it. Silent corruption or validation errors on
the very first thing any user does. Three ways out:
- **(a)** Upload on the consuming queue — simple, no transfer, serializes against rendering.
- **(b)** Nevarea records the release internally and `upload_*` returns the matching `acquire` barrier the
  caller must record. Explicit, fast, consistent with G5.
- **(c)** Mark uploaded resources `CONCURRENT` — reintroduces the bandwidth cost G5 rejected.
Leaning **(b)**: it keeps the cost model honest and matches the paired-barrier pattern already chosen.

**H3 — `create_command_list` is on `Device`, so a family/queue mismatch is permitted.**
`device.create_command_list(QueueKind::Compute)` produces a list that can be handed to `graphics_q.submit()`.
That is UB and the type system currently allows it. Fix: move it to `Queue::create_command_list(&self)` so
the family is implied by construction, and `ParallelRendering` likewise becomes `Queue::begin_parallel_rendering`.
Eliminates the mismatch entirely rather than validating it.

**H4 — The transfer pair is constructed safely but not *consumed* safely.**
`ImageBarrier::transfer` guarantees both halves exist; nothing guarantees both are *recorded*. Dropping the
acquire yields a hang or a validation error. Rust has no linear types, so this cannot be fully enforced.
Mitigate: `#[must_use]` on the returned tuple, plus debug-build tracking that every release has a matching
acquire recorded before submit. **State the limitation explicitly** — it is the one place the paired design
does not fully deliver.

**H5 — `ParallelRendering::finish() -> Vec<CommandList>` loses a constraint.**
Those lists must be submitted **in order, to one queue, consecutively** — suspend/resume is only valid across
an unbroken run. Returning a plain `Vec` lets the user splice other lists between them, silently breaking the
render pass. Fix: return a newtype (`ParallelLists`) that `SubmitDescription` accepts whole, so the run
cannot be split.

**Round yield: 3 → 8 → 5 → 5.** Severity is no longer falling: H1 and H2 are both soundness bugs, and both
were *introduced by round 3's own fixes*. **ALL FIVE APPLIED** (H2 via option (b)).

---

## 17. Falsification — round 5

Targeted at round 4's fixes: `RenderPart::record`, `Queue::create_command_list`, the `Upload` types,
`ParallelLists`. **Five findings; two serious. The first one simplifies the design rather than complicating it.**

**I1 — SERIOUS. G5 over-applied: buffers should be CONCURRENT, only images need EXCLUSIVE.**
The bandwidth argument for `EXCLUSIVE` is **image compression** — `CONCURRENT` disables DCC/delta colour
compression on AMD and NVIDIA. **Buffers have no such compression, so `CONCURRENT` on buffers is
essentially free.** Splitting the ruling:

- **Buffers → `CONCURRENT`.** No ownership transfer, ever. `BufferUpload::acquire()` disappears entirely,
  and `BufferBarrier` needs no queue fields.
- **Images → `EXCLUSIVE` + paired transfer**, exactly as G5 specified.

This removes most of the H2/H4 burden at no measurable cost, and it fixes I2 below by more than half.
It also closes a gap nothing had caught: **`readback_buffer` runs on the transfer queue and would have
needed a *release* from whichever queue last wrote the buffer** — impossible for Nevarea to record on the
user's behalf. With buffers `CONCURRENT`, that problem ceases to exist.

**I2 — SERIOUS. Uploads do not batch.** Loading a scene means ~1000 `upload_image` calls, each returning a
`#[must_use]` value needing its own recorded acquire. Untenable ergonomically, and 1000 separate barriers is
bad practice besides. Needs a batch form that coalesces into **one** barrier call:
```rust
let mut batch = device.upload_batch();
for tex in textures { batch.image(&tex.image, &tex.data, region); }
let done = batch.finish()?;                       // one transfer submit
cmd.barrier(&[], &done.acquire_all(graphics_q.id(), ImageState::ShaderRead));
```
The single-shot `upload_image` stays for one-off use; the batch is the scene-loading path.

**I3 — `RenderPart::record` can be called twice.** `parts_mut() -> &mut [RenderPart]` hands out `&mut`, so a
second `record` would allocate a second list or silently reset the first. Make it consuming —
`into_parts() -> Vec<RenderPart>` with `fn record(self, f)` — so recording a part twice is unrepresentable.

**I4 — `SubmitDescription::lists` is a public field, which leaks the H5 invariant.** `extend_parallel` keeps a
parallel run contiguous, but nothing stops `submit.lists.insert(3, other)` from splitting it. Make the field
private with `push()` / `extend_parallel()` accessors — otherwise H5's newtype is decorative.

**I5 — `Stage` must be bitflags, not an enum.** A wait frequently applies at several stages
(`COLOR_ATTACHMENT_OUTPUT | FRAGMENT_SHADER`), and `VkPipelineStageFlags2` is a mask. As a plain enum, F3's
fix cannot express the common case and silently forces over-broad waits — reintroducing the very
over-serialization F3 existed to remove.

**Round yield: 3 → 8 → 5 → 5 → 5.** Notable that I1 *removes* API surface rather than adding it, and that
two findings (I4, I5) are cases where a previous fix was applied too shallowly to actually work.
**ALL FIVE APPLIED**, including knock-ons (`SubmitDescription::new()` builder, `ParallelLists::assemble`).

---

## 18. Falsification — round 6

**Five findings, ZERO soundness bugs — all completeness gaps.** First round with no UB, no lifetime hole,
and no unsound sync. The spine (object graph, sync model, threading, portability) is untouched for a
sixth round.

**J1 — SERIOUS (completeness). `Image::bindless_index()` is ambiguous.**
An image used as *both* sampled and storage occupies **two different descriptor arrays** at two different
binding points, with two different indices — this is exactly the layout the C++ Nevarea used (binding 0
sampled, binding 1 storage, binding 2 samplers). A single `Option<u32>` cannot express it, and returning the
wrong one silently reads the wrong descriptor array in the shader. Fix:
```rust
impl Image {
    pub fn sampled_index(&self) -> Option<u32>;
    pub fn storage_index(&self) -> Option<u32>;
}
```
`bindless_index()` should not exist at all — there is no single correct answer to it.

**J2 — Image-based VRS has no attachment point.**
`RenderState::shading_rate` covers per-draw rates only. Image-based VRS needs a shading-rate attachment on
the *pass*, and per-primitive VRS is a shader output. All three are separate mechanisms; the API currently
models one and silently implies the others are covered. Add `RenderingDescription::shading_rate_attachment`.

**J3 — `DepthAttachment` has no resolve, and stencil is entirely absent.**
`ColorAttachment` has `resolve`; depth does not, though depth-resolve is standard with MSAA. Stencil has no
representation anywhere — no `ImageState::StencilAttachment`, no stencil op state in `RenderState`.

**J4 — `QueryPool::results()` has unspecified blocking semantics.**
Does it block until available, or fail? Timestamps are read a frame or two later in every real profiler.
Needs an explicit form: `results(range, Wait::Block | Wait::NonBlocking) -> Result<Option<Vec<u64>>>`, or a
`SubmitValue` argument so the pool can check completion itself.

**J5 — `RenderState` is missing several real fields.** No line width, no primitive restart, no stencil ops,
no depth-clamp, no `logic_op`. Not architectural, but the struct claims to be the complete dynamic state and
currently is not.

### Assessment after six rounds

Yield: **3 → 8 → 5 → 5 → 5 → 5**, but the *character* has changed decisively:

| Round | Nature of findings |
|---|---|
| 1–3 | soundness — UB, unsound sync, lifetime holes |
| 4 | soundness bugs *introduced by round 3's fixes* |
| 5 | fixes applied too shallowly to work + one over-broad ruling |
| 6 | **completeness only — no soundness bugs at all** |

**Recommendation: stop paper-falsifying and start writing code.** J1 aside, rounds 4–6 have been dominated by
*propagation* errors — knock-ons of the previous round's fixes — and that is precisely the class `cargo check`
catches in seconds and for free. Paper review has done what it is good at (finding architectural and
semantic bugs a compiler cannot see, e.g. F1, G1, H1, H2, I1). It is now past the point of diminishing
returns against a borrow checker.

**Apply J1–J5, then build the skeleton crate with `todo!()` bodies and let the compiler take over.**

**J1–J5 APPLIED 2026-08-11. Paper falsification ends here.** Next reviewer is `cargo check`.
