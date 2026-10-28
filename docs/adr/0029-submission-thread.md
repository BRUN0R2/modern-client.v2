# ADR-0029 — The device submits and presents its frames on a thread of its own

- **Status:** Accepted
- **Date:** 2026-10-28

## Context

`next` records, submits and presents every frame on its main thread: `Device::beginFrame`, the
frame's commands, `Swapchain::acquire`, then `Device::submitFrame`, which calls `vkQueueSubmit2` and
`vkQueuePresentKHR` and returns once the driver has taken both.

Measured on 2026-10-28 (RTX 4060, NVIDIA 617.14, Windows 11; `next` windowed at 1280×720 on de_dust2
alone, `r_fidelity 1`, `fps_max 100000`; an xperf CPU profile, and timers around both calls in
`VulkanDevice::submit` and `VulkanSwapchain::present`): the main thread spends 385 to 420 µs a frame,
of which `vkQueuePresentKHR` takes about 176 µs and `vkQueueSubmit2` about 41 µs. Half of the frame
is the driver's and the kernel's work, on the thread that also reads the input, plays the game and
records the frame. Borderless fullscreen (submit 123 µs, present 134 µs) and MAILBOX (as IMMEDIATE)
do not reduce it. The GPU takes about 150 µs a frame (timestamp queries), the recording about
135 µs, the engine's own logic about 15 % of the frame. So about 2 500 frames a second uncapped, and
at `fps_max 1000` half of each frame's work is the driver's instead of the game's and the bots'.

[05 §7](../05-rendering.md#7-render-graph) already plans that "the render thread submits them in
order". Moving the two calls to another thread is bounded by:

- **Vulkan's external synchronization.** `vkQueueSubmit2`, `vkQueuePresentKHR` and
  `vkDeviceWaitIdle` (which takes every queue of the device, and which `next` calls on a map change,
  a resize and a weapon reload) may not use the queue at once. `vkAcquireNextImageKHR` and
  `vkQueuePresentKHR` both require the swapchain to be externally synchronized: an acquire on the
  main thread may not run while another thread presents.
- **The frame names its image as it is recorded.** The frame's last pass draws to the swapchain
  image's view (dynamic rendering), and in the legacy look the whole frame is that one pass
  ([05 §7.1](../05-rendering.md#71-frame-passes-legacy-mode)): the image is acquired before anything
  is recorded. Acquiring it on the main thread would wait, every frame, for the other thread to
  finish presenting the frame before: nothing would overlap.
- **Weak GPUs lack bandwidth** ([ADR-0025](0025-enhanced-rendering.md)): drawing the frame elsewhere
  and copying it to the window's image costs a full-screen read and write every frame, 16.6 MB at
  1920×1080, about 0.3 to 0.7 ms on an integrated GPU, where the frame rate is the GPU's: there the
  copy would cost more than the thread gives.
- **Frames in flight** (2 by default) bound how far the CPU runs ahead of the GPU: a frame's command
  buffers and resources are reused once the GPU has finished the frame that used them before.
- **Errors** found while submitting or presenting (a device lost, a surface lost, a swapchain out of
  date after a resize or a minimize) reach today's caller at once; the window's events, a resize
  among them, come to the main thread.
- **Latency.** The input is read after the limiter waits
  ([05 §12](../05-rendering.md#12-latency-and-frame-pacing)). A frame that waits behind another to be
  submitted adds a frame from input to present.
- **Threads.** Only `nx.platform` and `nx.jobs` create threads
  ([04 §7](../04-job-system.md#7-usage-rules)); a thread that blocks in OS calls is a dedicated
  thread, not a job ([04 §5](../04-job-system.md#5-dedicated-threads-outside-the-pool)).

## Decision

The Vulkan device submits and presents its frames on a submission thread of its own. The
render pass that draws to the window's image is recorded into a secondary command buffer, which that
thread runs inside its own render pass on the image it acquired: no copy. The RHI's functions keep
their signatures and the renderer does not change; what the functions promise changes as below.

### 1. A submission thread, the only user of the queue and the swapchain

- Each device starts one thread (`nx::platform::Thread`, the first of the OS threads `nx.platform`
  planned, named for the debuggers) that calls `vkQueueSubmit2`, `vkAcquireNextImageKHR` and
  `vkQueuePresentKHR`, frame after frame in the order the frames were handed to it.
- The render thread (the main thread today) uses the queue and the swapchain only once the
  submission thread has finished everything it was handed: `Device::waitIdle` drains it, then waits
  for the device as today; the swapchain is made, rebuilt and destroyed after such a drain. The
  hand-off's atomics order the two threads: no lock.
- [04 §5](../04-job-system.md#5-dedicated-threads-outside-the-pool) lists the thread among the
  dedicated ones.

### 2. The window's pass, recorded now and drawn once the image is acquired

- `Swapchain::acquire` gives the frame a texture that stands for the window's next image, of its
  size and format, before any image is acquired. A frame may draw to it in one render pass, its
  last, as the pass's only color attachment, with or without a depth attachment; it is not copied,
  sampled, read back nor drawn to in a second pass. Its transitions, from `Undefined` to
  `RenderTarget` and then to `Present`, are the submission thread's to record. Both looks draw their
  frame so today.
- The backend records that pass's commands into a secondary command buffer
  (`VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT`, its attachments' formats inherited), and the
  commands before it into the frame's primary command buffer.
- The submission thread takes the window's image it acquired and records, in a primary command
  buffer of its own, the image's transition to a color attachment, the render pass on the image (the
  frame's load and store operations, clear color, depth attachment and area), the secondary command
  buffer run inside it (`vkCmdExecuteCommands`), and the transition to presentation. It submits the
  frame in one `vkQueueSubmit2`, as two batches in order: the frame's own commands wait for nothing,
  so the GPU starts them before the image is free; the window's pass waits for the image, then
  signals the image's semaphore, which the presentation waits for, and the frame's timeline value.
  A frame that draws nothing to the window is one batch, as today.
- With no wait of the whole frame on the window's image, consecutive frames overlap on the GPU as
  far as their own barriers allow: a transition from `Undefined` waits for the texture's last use,
  which may be the frame before's (a depth target both draw to), instead of for nothing.
- The window's images stop being RHI textures: only the submission thread uses them.

### 3. One frame handed over at a time, the waits before the input

- `Device::submitFrame` puts the frame on a lock-free single-producer, single-consumer queue
  (`nx::jobs::SpscQueue`, as [04 §5](../04-job-system.md#5-dedicated-threads-outside-the-pool) asks
  of dedicated threads), wakes the thread and returns.
- `Device::beginFrame` waits until the submission thread has taken the frame before, so that no
  frame ever waits behind another, then, as today, until the GPU has finished the frame that last
  used the slot: frames in flight stay the back-pressure.
- `next` begins its frame before it reads the input, right after the limiter, so that both waits
  come before the input is read, as the limiter's own wait does
  ([05 §12](../05-rendering.md#12-latency-and-frame-pacing)).
- Every wait sleeps on `std::atomic::wait`; none spins.

### 4. The next image acquired ahead

- Once it has presented a frame, the submission thread acquires the window's next image before it
  takes the next frame. With FIFO (`gl_vsync 1`) it waits there for the display, before the render
  thread may begin the next frame, which then reads its input right after the vertical blank, as
  today; with IMMEDIATE and MAILBOX the acquire returns at once.
- One image is acquired at a time: the swapchain keeps one image more than the fewest the surface
  takes, as today.

### 5. Out of date, resized, minimized

- A swapchain the submission thread finds out of date or suboptimal (acquiring or presenting) is
  marked so, atomically; a frame with no image to draw to has its other commands submitted, and is
  not shown. The render thread rebuilds the swapchain at its next `acquire`, after a drain, as today
  it rebuilds it there. `Swapchain::resize`, from the window's events on the render thread, drains
  and rebuilds. A frame is always recorded for the swapchain the submission thread acquires from,
  since only a drained render thread rebuilds it.
- An image acquired ahead that a rebuild drops is never presented: an empty batch waits on its
  semaphore before the swapchain goes, so that no semaphore is destroyed or reused with a signal
  pending.
- Minimized, there is no swapchain and `acquire` gives no image, as today.

### 6. Errors a frame later

- A failure of the submission thread (`DeviceLost`, `SurfaceLost`, `OutOfHostMemory`,
  `OutOfDeviceMemory`, `DriverRefused`) is kept, the first one, and every later `beginFrame`,
  `submitFrame` and `acquire` returns it. The frames handed after it are taken and dropped, so the
  render thread never waits for a frame that cannot finish. The client ends on it, as it does today.

### 7. What it gives, as measured

Measured against `main` on 2026-10-28 (the setup of the context; Mirillis Action's layer off in
both; medians of 3-second windows over three alternating runs of each): the frame rate, the main
thread's work a frame (its waits left out), and the time from reading a frame's input to the return
of its `vkQueuePresentKHR`.

| Mode | Frames a second | Main thread's work | Input to present (mean, median) |
|------|-----------------|--------------------|---------------------------------|
| Uncapped, before | 2 119 | 437 µs | 467, 440 µs |
| Uncapped, after | 3 157 | 170 µs | 528, 492 µs |
| `fps_max 1000`, before | 998 | 444 µs | 466, 444 µs |
| `fps_max 1000`, after | 999 | 154 µs | 513, 496 µs |
| `gl_vsync 1` (240 Hz), before | 240 | 394 µs | 4 154, 4 156 µs |
| `gl_vsync 1` (240 Hz), after | 240 | 215 µs | 414, 401 µs |

- The main thread does 60 % less a frame. Uncapped, the submission thread bounds the frame rate: it
  is busy 288 µs a frame (the submit, the present, and the acquire of the next image, which the main
  thread did before), so the frame rate rises by half, short of the 4 000 frames a second a submit
  and present alone would allow. At `fps_max 1000` the main thread is idle for 85 % of each
  millisecond, which the game and the bots can use. The GPU does no more work than before.
- Latency grows by about 50 µs where the frame rate is capped or not, never a frame: a frame waits
  for the submission thread to wake, or to finish the frame before. With FIFO the input is now read
  after the display is ready rather than before the wait for it: 3.7 ms fresher.
- `nxview`'s screenshots stay byte-identical, and `next` runs with validation without an error.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| The frame drawn to a target of the swapchain's own, which the submission thread copies to the acquired image | Simpler in the backend, but a full-screen copy every frame: bandwidth that weak GPUs lack, about 0.3 to 0.7 ms at 1920×1080 on an integrated GPU, where it would cost more than the thread gives, and a target per frame in flight in memory |
| The render thread acquires the image as late as it can, under a lock shared with the present | The legacy look draws its whole frame in one pass to the image, so the acquire comes before all recording and waits for the present before, every frame: nothing overlaps |
| The submission thread acquires ahead for the render thread, which records straight to the window's image | The render thread needs its image while the frame before still holds one: two images acquired at once, so one image more, with which FIFO lets the program run a frame further ahead of the display (a frame of latency) unless the order changes by present mode; and the render thread waits for that acquire every frame |
| A render thread a frame behind the game (one thread plays, the other records) | A frame of latency, the client's state split between threads, and the driver's work still on a thread that records; the render graph's parallel recording comes first |
| Presenting from a second queue | Separates the queues only: the swapchain is still shared by the acquire and the present |
| Nothing, MAILBOX, or fullscreen | Measured: the driver's work stays (fullscreen moves it between the submit and the present) |

## Consequences

- ✅ The driver's submit and present, about half of today's main thread, leave it; the main thread
  never waits for the presentation engine.
- ✅ No GPU work and no memory are added: the window's pass draws straight to the window's image.
- ✅ The renderer, `nxview` and the tests record their frames as before; the window's images and their
  semaphores are the submission thread's alone. The passes before the window's pass start on the
  GPU without waiting for its image.
- ⚠️ The window's image serves one render pass a frame and nothing else: a screenshot of the window
  is drawn to a texture of its own, as `nxview`'s are; a later render graph ends with that pass.
- ⚠️ The backend splits a frame's recording in two at the window's pass, and a secondary command
  buffer inherits nothing bound before it: the command list starts it with no state.
- ⚠️ A failure to submit or present reaches the caller a frame later.
- ⚠️ Two threads share the device, so data races are the risk. Mitigation: the threads share only the
  hand-off's queue and atomics, and what the render thread changes only while the other is drained;
  the tests run with the validation layers and synchronization validation, and under
  ThreadSanitizer in CI (`tsan-linux-gcc`); a test presents to a hidden window where one can be
  made.
- ⚠️ `Device::waitIdle` may wait up to a vertical blank for an acquire ahead under FIFO.
