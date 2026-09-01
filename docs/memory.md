# Memory

What the SoftSIM costs in RAM and flash on an nRF91, which knobs size it, and the
system-level budgets around it. Numbers marked *measured* come from a pristine build of
[`samples/softsim_external_profile`](../samples/softsim_external_profile) for
`nrf9151dk/nrf9151/ns` on NCS v3.4.0; re-measure on your own configuration before
trimming anything.

## The budget

The nRF9151 has 256 KB of SRAM, split three ways before the application sees any of it:

| Region | Size | Owner |
|---|---|---|
| TF-M secure RAM | 48 KB | Fixed in [`dts/softsim/nrf91_softsim_sram.dtsi`](../dts/softsim/nrf91_softsim_sram.dtsi) — already trimmed from the 88 KB NCS default for nRF91; `CONFIG_PM_PARTITION_SIZE_TFM_SRAM` no longer affects the split. TF-M itself uses ~40 KB (*measured*), and the reservation deliberately stays at 48 KB: the next SPU-granularity step down (8 KB) would leave ~1.2 KB of slack, and this TF-M instance also serves your application's own PSA keys, ITS data and crypto. Don't trim it. |
| Modem shared memory | 17.4 KB | `nrf_modem_lib` control (1,256 B) + TX (8,320 B) + RX (8,192 B) heaps, NCS defaults. RX has a hard floor of 2,616 B on this SoC; TX can go down to 1,024 B if your application's traffic allows. Trace is off. |
| Application | 190.6 KB | Everything else, including this module. |

The sample uses **~63.5 KB** of the application region at defaults (*measured*), of which
the SoftSIM module itself accounts for roughly 15 KB of static RAM plus its share of the
system heap (below). Flash is not a constraint: the sample image is ~148 KB of the 864 KB
application partition, of which the SIM core and glue are ~65 KB.

## Where SoftSIM RAM goes

Static:

| Item | Size | Knob |
|---|---|---|
| Work-queue thread stack | 6,000 B | `CONFIG_SOFTSIM_STACK_SIZE`. High-water mark *measured* at 2,200 B across provisioning, attach and 12 re-attach cycles with debug logs on; the default keeps ~2.7x that for OTA/SMS paths the measurement did not exercise. |
| Log-formatting buffers in the SIM core | 5,125 B | Compiled in unconditionally by the vendored SIM-core revision; once the submodule updates past its logging rework, `CONFIG_SOFTSIM_UICC_USE_LOGS=n` removes them along with ~25 KB of flash-resident log strings. |
| Work queue object, FIFO, filesystem bookkeeping | ~450 B | — |

Heap, drawn from the Zephyr system heap. SoftSIM contributes
`CONFIG_HEAP_MEM_POOL_ADD_SIZE_SOFTSIM` (default 24,000 B), so the kernel rounds the
effective pool up to that even when the application sets a smaller
`CONFIG_HEAP_MEM_POOL_SIZE`; a build assert enforces the floor on the effective size
because every SIM operation allocates from it:

| Item | Size | When |
|---|---|---|
| Filesystem directory table | ~6.3 KB | From `ss_init_fs()` until deinit: one entry + name per file (≈100 files in a provisioned profile). |
| SIM context | ~4.9 KB | Allocated on the modem's first `INIT` request, freed on `DEINIT` (kept while suspended). |
| File-content cache | ≤ ~2.5 KB | Up to 10 file buffers, recycled least-used-first. |
| Selected-path state, FCP/access decodes | 1.5–3 KB | Per selected file; freed on reselect. |
| APDU transactions | ~1.1 KB | Two live command/response pairs; three during a `6Cxx`/`61xx` retry. |
| OTA (remote file management) response | 4.1 KB | Peak, per OTA command. |

Steady state lands around 15–16 KB; the *measured* peak is 17,856 B across provisioning,
LTE attach and 12 re-attach cycles. The 24 KB default keeps that peak plus a worst-case
OTA response and margin for fragmentation.

## Sizing knobs

- `CONFIG_SOFTSIM_STACK_SIZE` — the dedicated thread that runs all SIM processing.
- `CONFIG_MAIN_STACK_SIZE` (default 5000) — raised via a Kconfig default, so an
  application `prj.conf` can override it. The demand comes from the provisioning path,
  which builds the decoded profile (~1 KB of locals) on the caller's stack.
- `CONFIG_HEAP_MEM_POOL_ADD_SIZE_SOFTSIM` (default 24000) — SoftSIM's system-heap
  contribution. Applications keep their own `CONFIG_HEAP_MEM_POOL_SIZE`; the effective
  pool is the larger of that and the summed contributions. Lower the contribution only
  with a measured heap profile.
- `CONFIG_SOFTSIM_NRF_DEBUG_LOGS` / `CONFIG_SOFTSIM_LIBS_DEBUG_LOGS` (default off) —
  each enables verbose logging and grows the deferred log buffer to 5,000 / 16,384 B
  plus ~1.5 KB of UART buffers. Never in production.
- The NVS partition is fixed at 32 KB and build-asserted: the provisioned filesystem
  occupies ~8 KB (2 of the 8 flash sectors); the rest is wear-leveling and
  garbage-collection headroom for a flash area that takes writes on every
  authentication. Do not shrink it.

To measure before trimming: build with `CONFIG_THREAD_ANALYZER=y` (+`CONFIG_INIT_STACKS=y`)
for per-thread stack high-water marks and `CONFIG_SYS_HEAP_RUNTIME_STATS=y` for
`sys_heap_runtime_stats_get()` on the system heap, then exercise boot → provisioning →
LTE attach → several authentications before reading the numbers.

## Around the module

Costs in the sample that are not SoftSIM's, worth knowing when you budget an application:
the AT host library (`CONFIG_AT_HOST_LIBRARY`) statically allocates a 4 KB command buffer
plus a 1 KB thread — it exists for interactive convenience and can be dropped. The system
heap also serves POSIX/socket plumbing (~800 B of configured add-sizes), and the modem
library keeps its own 1 KB heap for API calls.
