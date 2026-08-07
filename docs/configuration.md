# Configuration

The drop-in [`overlay-softsim.conf`](../overlay-softsim.conf) at the repo root enables
the SoftSIM with its required dependencies and sane defaults; apply it to any NCS
application with `-DOVERLAY_CONFIG=<path>/overlay-softsim.conf`. It does not bring a
partition layout — see [Flash partitioning](#flash-partitioning).

## Core options

Defined in the top-level [`Kconfig`](../Kconfig).

| Option | Default | Purpose |
|---|---|---|
| `CONFIG_SOFTSIM` | — | Include the SoftSIM in the build. Depends on `NVS`, `FLASH`, `FLASH_MAP`, `FLASH_PAGE_LAYOUT`, `MPU_ALLOW_FLASH_WRITE`, `BUILD_WITH_TFM`. |
| `CONFIG_SOFTSIM_AUTO_INIT` | `y` | Initialize at boot via `SYS_INIT` **and** select the software SIM (`AT%CSUS=2`) when the Modem library comes up. Setting `n` compiles out both — the application must then call `nrf_softsim_init()` *and* send `AT%CSUS=2` itself (see [Boot flow](architecture.md#boot-flow)). |
| `CONFIG_SOFTSIM_STATIC_PROFILE_ENABLE` | `n` | Provision a compiled-in profile on first boot. **Development only.** |
| `CONFIG_SOFTSIM_STATIC_PROFILE` | — | The profile string used by the above. |
| `CONFIG_SOFTSIM_FACTORY_RESET_ON_PROVISION` | `n` | Compile `nrf_softsim_modem_factory_reset()` and `nrf_softsim_just_provisioned()`. Initial-provisioning flows only — see [Provisioning](provisioning.md#factory-reset-on-provision). |
| `CONFIG_SOFTSIM_FLASH_BUNDLED_HEX` | `n` | Make `west flash` program the merged hex that bundles the filesystem template. Set automatically by sysbuild — see [The filesystem template](#the-filesystem-template). |

## onomondo-uicc submodule options

These map 1:1 onto the submodule's CMake options
([`lib/CMakeLists.txt`](../lib/CMakeLists.txt)).

| Option | Default | Purpose |
|---|---|---|
| `CONFIG_SOFTSIM_UICC_EXTERNAL_CRYPTO_IMPL` | `y` | Use the platform crypto port (PSA) instead of the bundled software AES/3DES. |
| `CONFIG_SOFTSIM_UICC_EXTERNAL_KEY_LOAD` | `n` | Keep software crypto but fetch keys via an external loader. Mutually exclusive with the above. |
| `CONFIG_SOFTSIM_UICC_NO_DEFAULT_IMPL` | `y` | Exclude the library's host implementation of the `fs.h` shim (`fs.c`) from non-compact builds; no effect while compact storage is enabled. |
| `CONFIG_SOFTSIM_UICC_COMPACT_STORAGE` | `y` | Select the compact storage backend (`storage_compact.c`), which runs over the `fs.h` shim the nRF91 port implements — see [Interfaces](interfaces.md#storage). |
| `CONFIG_SOFTSIM_UICC_USE_SYSTEM_HEAP` | `n` | `malloc`/`free` instead of the `port_malloc` port. |
| `CONFIG_SOFTSIM_UICC_USE_LOGS` | `y` | Compile `SS_LOGP` logging into the SIM core. |
| `CONFIG_SOFTSIM_UICC_USE_EXPERIMENTAL_SUSPEND_COMMAND` | `y` | UICC SUSPEND support. |
| `CONFIG_SOFTSIM_UICC_USE_UTILS` | `n` | Build the submodule's `utils` target. |
| `CONFIG_SOFTSIM_UICC_ENABLE_SANITIZE` | `n` | Sanitizers in the submodule build. |
| `CONFIG_SOFTSIM_UICC_BUILD_LIB_ONLY` | `y` | Build only the libraries (no host demo executable). |

## Logging

Two independent log layers, each a Zephyr log module:

| Option | Effect |
|---|---|
| `CONFIG_SOFTSIM_NRF_DEBUG_LOGS` | nrf-softsim layer (SIM HAL, filesystem, crypto) at `DBG`; log buffer 5000 bytes. |
| `CONFIG_SOFTSIM_LIBS_DEBUG_LOGS` | onomondo-uicc `SS_LOGP` traces at `DBG`. High volume; log buffer 16384 bytes. |

Per-module levels (`CONFIG_SOFTSIM_NRF_LOG_LEVEL_*`, `CONFIG_SOFTSIM_LIBS_LOG_LEVEL_*`)
give finer control.

`CONFIG_SOFTSIM` also sets `LOG_DEFAULT_LEVEL=1` and `LOG_PROCESS_TRIGGER_THRESHOLD=5`
as Kconfig defaults; an explicit value in your `prj.conf` wins.

For a lossless library trace, enable synchronous logging (`CONFIG_LOG_MODE_IMMEDIATE=y`);
`CONFIG_SOFTSIM_LOG_IMMEDIATE_MODE` (default `y` in that case) then raises the console
to `CONFIG_SOFTSIM_LOG_IMMEDIATE_MODE_BAUD` (1000000) at boot so blocking log writes do
not stall SIM init past the modem's deadlines — reconnect your terminal at that rate.
SEGGER RTT is an alternative backend without the UART bottleneck. See the
`SOFTSIM_LOG_IMMEDIATE_MODE` Kconfig help. Debug only.

## Flash partitioning

The SoftSIM persists its filesystem in a dedicated **32 kB `nvs_storage` partition**
(the node label must be spelled exactly `nvs_storage`; a build assert in
[`lib/build_asserts.c`](../lib/build_asserts.c) enforces existence and size).

From NCS v3.4.0 partitioning comes from the **devicetree** (Nordic's Partition Manager
is deprecated). **NCS v3.4.0 is the minimum**: the layouts use the
`zephyr,mapped-partition` binding and the module resolves the partition through the
`PARTITION_*` flash map macros, neither of which exists in earlier releases.

Ready-made layouts ship in [`dts/softsim/`](../dts/softsim), on the devicetree include
path automatically:

| Include | Target |
|---|---|
| `softsim/nrf91_softsim_partitions.dtsi` | nRF91 DKs, no bootloader |
| `softsim/nrf91_softsim_mcuboot_partitions.dtsi` | nRF91 DKs with MCUboot |
| `softsim/thingy91_softsim_partitions.dtsi` | Thingy:91 (factory MCUboot layout) |
| `softsim/thingy91x_softsim_partitions.dtsi` | Thingy:91 X (factory B0 + MCUboot layout) |
| `softsim/nrf91_softsim_sram.dtsi` | Matching SRAM split (TF-M / application) |

The DK no-bootloader, Thingy:91 and Thingy:91 X layouts keep the addresses of the
earlier Partition Manager layouts, so firmware upgrades keep provisioned profiles
intact — *provided the upgrade is flashed as described [below](#upgrading-provisioned-devices)*.
The DK MCUboot layout is new (no supported MCUboot configuration existed before) but
keeps `nvs_storage` at the same address.

Apply a layout from a board overlay, as the sample does in
[`boards/`](../samples/softsim_external_profile/boards):

```c
#include <softsim/nrf91_softsim_partitions.dtsi>
#include <softsim/nrf91_softsim_sram.dtsi>
```

Custom layouts are fine as long as a 32 kB `nvs_storage` partition exists. On the DK
and Thingy:91 X layouts that node **also carries the `storage_partition` label**, which
is how TF-M configures the range as non-secure in the SPU; without it the first NVS
write faults. (The Thingy:91 layout is the exception: `storage_partition` there is a
separate settings partition, and the SPU region comes out of the factory layout.)

For DFU with MCUboot on the DKs, add the MCUboot layout on top — it matches the stock
boot/slot0/slot1 geometry, so the MCUboot image builds with the unmodified board
devicetree (see the sample's
[`mcuboot-partitions.overlay`](../samples/softsim_external_profile/mcuboot-partitions.overlay)):

```
west build --sysbuild -b nrf9151dk/nrf9151/ns -- \
  -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
  -DEXTRA_DTC_OVERLAY_FILE=mcuboot-partitions.overlay
```

On the Thingy:91 and Thingy:91 X the bootloader chain is enabled by the board defaults;
the module gives those images the matching partition view where the factory layout
differs from the stock board devicetree.

The deprecated Partition Manager flow still works during the transition window: build
with `-DSB_CONFIG_PARTITION_MANAGER=y` (static layouts in the `pm_static.yml` files,
kept in the sample and under [`boards/`](../boards)).

### Applications that already bring a layout

The SoftSIM layout **replaces** an application's own layout rather than stacking on it;
two complete layouts cannot both apply, and the second fails with
`undefined node label 'boot_partition'`. The NCS cellular samples hit this — they
`#include <samples/cellular/nrf91_no_bootloader_partitions.dtsi>` in
`boards/<board>.overlay`. Either edit that overlay, or override the application's
overlay list with `DTC_OVERLAY_FILE` (which replaces, unlike `EXTRA_DTC_OVERLAY_FILE`,
which appends):

```
west build --sysbuild -b nrf9151dk/nrf9151/ns nrf/samples/cellular/at_client -- \
  -DOVERLAY_CONFIG=$PATH_TO_ONOMONDO_SOFTSIM/overlay-softsim.conf \
  -DDTC_OVERLAY_FILE="$PATH_TO_ONOMONDO_SOFTSIM/dts/softsim/nrf91_softsim_partitions.dtsi;$PATH_TO_ONOMONDO_SOFTSIM/dts/softsim/nrf91_softsim_sram.dtsi"
```

## The filesystem template

The `nvs_storage` partition must be pre-populated with the SoftSIM's initial filesystem
(see [Architecture](architecture.md#the-filesystem)). The build relocates
[`lib/profile/template.bin`](../lib/profile/template.bin) to the partition address as
`template.hex`.

The recommended flow is automatic — set in `sysbuild.conf`:

```
SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX=y
```

Sysbuild ([`sysbuild/CMakeLists.txt`](../sysbuild/CMakeLists.txt)) merges firmware and
template into one image and points `west flash` at it (via
`CONFIG_SOFTSIM_FLASH_BUNDLED_HEX`). Manually:
`west flash --hex-file build/<app>/onomondo-softsim/template.hex`. Under the Partition
Manager the hex is generated in sysbuild scope instead — `west build -t onomondo_softsim_template`,
output at `build/onomondo-softsim/template.hex`.

If the partition table changes, the template address changes with it — rebuild and
re-flash. Check the resolved layout in `build/<app>/zephyr/zephyr.dts`, or with
`west build -t partition_manager_report` under the Partition Manager.

`template.bin` is a **prebuilt NVS image** checked into this repository; the build only
relocates it. Changes to the SIM core's file tree do not reach the device until a new
template is generated, and the generation tooling is not part of this repository —
contact Onomondo if your changes need a modified template.

### Upgrading provisioned devices

The default build bundles the template into `build/merged.hex` and points `west flash`
at it. That is the right artifact for a **fresh or fully erased** device — but on a
provisioned device `west flash` reprograms the template over the start of `nvs_storage`
and destroys the profile. Worse than a clean wipe: the template covers only the first
sectors, so the rest keep stale filesystem records and the mixed state is undefined (an
old profile may even resurrect). To deliberately reset provisioning, erase the whole
partition (`west flash --erase`, or `nrfutil device recover`).

To upgrade firmware while keeping the profile, either:

- build with `-DSB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX=n` — no template is merged and
  `west flash` programs the application-only hex, which never touches `nvs_storage`; or
- flash the application-only artifact explicitly
  (`build/<app>/zephyr/tfm_merged.hex`, or `build/<app>/zephyr/zephyr.signed.hex` under
  MCUboot) with an erase mode limited to the pages it touches, e.g.
  `nrfutil device program --firmware <hex> --options chip_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE`.

## Resource requirements

| Resource | Requirement |
|---|---|
| Heap | `CONFIG_HEAP_MEM_POOL_SIZE` ≥ 30000 (build assert). The SoftSIM allocates its filesystem cache and working buffers from the kernel heap; budget on top of your application's own use. |
| SoftSIM thread | 10 kB stack (`SOFTSIM_STACK_SIZE`). |
| Flash | 32 kB `nvs_storage` + the TF-M secure region, sized by the devicetree `slot0_s_partition` node: 0x18000 (96 kB) on both nRF91 DK layouts, 0x14000 (80 kB) on Thingy:91, 0x20000 (128 kB) on Thingy:91 X. (`CONFIG_PM_PARTITION_SIZE_TFM` in `overlay-softsim.conf` applies only to the deprecated Partition Manager flow.) Large applications may need features trimmed to fit — the linker reports overflow. The same applies to RAM. |
| TF-M | `CONFIG_BUILD_WITH_TFM=y` is a hard dependency (PSA crypto). The sample keeps TF-M small with `CONFIG_TFM_PARTITION_PROTECTED_STORAGE=n` and `CONFIG_PSA_CRYPTO_DRIVER_CC3XX=n`. |

## Coexistence constraints

- **Zephyr Settings**: `CONFIG_SETTINGS_NVS` is rejected by a build assert — the
  SoftSIM needs the `nvs_storage` partition. Switching to `CONFIG_SETTINGS_FCB=y` is
  not enough on its own: the FCB backend defaults to `storage_partition`, which on the
  DK and Thingy:91 X layouts *is* `nvs_storage`, and a second build assert rejects that
  too. Define a dedicated settings partition in your board overlay.
- **VFP linking**: some applications (e.g. `modem_shell`) fail to link with
  `... uses VFP register arguments`; add `CONFIG_FP_SOFTABI=y`. Application-specific
  SoftSIM options like this belong in an overlay of your own, layered on top of the
  repo's — `OVERLAY_CONFIG` takes a semicolon-separated list:

  ```
  west build -b nrf9151dk/nrf9151/ns -- \
    "-DOVERLAY_CONFIG=$PATH_TO_ONOMONDO_SOFTSIM/overlay-softsim.conf;overlay-softsim.conf"
  ```
- **PSA algorithms**: the crypto port needs `CONFIG_PSA_WANT_KEY_TYPE_AES`,
  `CONFIG_PSA_WANT_ALG_CBC_NO_PADDING`, `CONFIG_PSA_WANT_ALG_ECB_NO_PADDING` and
  `CONFIG_PSA_WANT_ALG_CMAC` (all set by `overlay-softsim.conf`).
