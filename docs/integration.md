# Integration

Putting the SoftSIM into your own product: build setup, partitioning, shipping the
filesystem template, and adapting to your own board.

## Add it to your build

No changes to this repository are needed — consume it as a west module.

1. Add it to your application's `west.yml`. **`submodules: true` is required**: `west
   update` does not initialize submodules of a project it imports, so without it
   `lib/onomondo-uicc` stays empty and the CMake configure step fails.

   ```yaml
   projects:
     - name: sdk-nrf
       remote: nrfconnect
       revision: <tag>
       import: true
     - name: onomondo-softsim
       url: https://github.com/onomondo/nrf-softsim.git
       path: modules/lib/onomondo-softsim
       revision: <tag>
       submodules: true
   ```

   Match `revision` to a release validated against your NCS version — each release pins
   its NCS in [`west.yml`](../west.yml) and lists it in the [changelog](../CHANGELOG.md).

   (To point an existing NCS workspace at *this* repository's manifest instead, clone it
   to `modules/lib/onomondo-softsim`, `west config manifest.path` at it, and `west
   update` — with the same `git submodule update --init`, since `west update` does not
   cover the manifest-self repository's submodules either.)

2. Apply [`overlay-softsim.conf`](../overlay-softsim.conf) — the drop-in that enables the
   SoftSIM with its required dependencies and sane defaults:

   ```
   -DOVERLAY_CONFIG=$PATH_TO_ONOMONDO_SOFTSIM/overlay-softsim.conf
   ```

   `OVERLAY_CONFIG` takes a semicolon-separated list, so layer your own options on top
   rather than editing the repo's. The overlay deliberately does **not** bring a
   partition layout — see [Flash partitioning](#flash-partitioning).

3. Include a [partition layout](#flash-partitioning) from a board overlay and set
   `SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX=y` in `sysbuild.conf` so
   [the filesystem template](#the-filesystem-template) travels with the firmware.

4. Decide who initializes: keep `CONFIG_SOFTSIM_AUTO_INIT=y` for zero-touch startup, or
   set it to `n` and own both `nrf_softsim_init()` and `AT%CSUS=2` — see
   [Boot flow](architecture.md#boot-flow).

The sample is your integration test: when in doubt whether an issue is yours or the
module's, reproduce against
[`samples/softsim_external_profile`](../samples/softsim_external_profile). The
[`tests/`](../tests) Twister suites run on `native_sim` and are the fastest way to check
a change to the port implementations: `west twister -T tests/`.

## Options worth commenting on

Every symbol is documented in [`Kconfig`](../Kconfig); the ones under "Options for the
onomondo-uicc submodule" map 1:1 onto that submodule's CMake options. Only these four
behave in ways the help text can't fully convey:

- **`SOFTSIM_AUTO_INIT`** (default `y`) — setting it to `n` compiles out *two* things:
  the `SYS_INIT` **and** the hook that sends `AT%CSUS=2`. Your application must then do
  both. That is the configuration for runtime SIM selection; see
  [Boot flow](architecture.md#boot-flow).
- **`SOFTSIM_STATIC_PROFILE_ENABLE`** — compiles a profile into the firmware. Development
  only: every device flashed with that image gets the same SIM identity.
- **`SOFTSIM_FLASH_BUNDLED_HEX`** — points `west flash` at the merged hex. You normally
  don't set this; sysbuild does, from `SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX`. Read
  [Upgrading provisioned devices](#upgrading-provisioned-devices) before shipping with it.
- **`SOFTSIM_LOG_IMMEDIATE_MODE`** (default `y` under `CONFIG_LOG_MODE_IMMEDIATE`) —
  synchronous logging is the only way to get a lossless library trace, but it blocks on
  every write, and at 115200 baud that stalls SIM init past the modem's deadlines and
  prevents registration. So this raises the console to
  `SOFTSIM_LOG_IMMEDIATE_MODE_BAUD` (1000000) at boot — **reconnect your terminal at
  that rate**. SEGGER RTT avoids the UART bottleneck entirely. Debug only.

## Flash partitioning

The SoftSIM persists its filesystem in a dedicated **32 kB `nvs_storage` partition** (the
node label must be spelled exactly `nvs_storage`; a build assert in
[`lib/build_asserts.c`](../lib/build_asserts.c) enforces existence and size).

From NCS v3.4.0 partitioning comes from the **devicetree** (Nordic's Partition Manager is
deprecated). **NCS v3.4.0 is the minimum**: the layouts use the `zephyr,mapped-partition`
binding and the module resolves the partition through the `PARTITION_*` flash map macros,
neither of which exists in earlier releases.

Ready-made layouts ship in [`dts/softsim/`](../dts/softsim), on the devicetree include
path automatically:

| Include | Target |
|---|---|
| `softsim/nrf91_softsim_partitions.dtsi` | nRF91 DKs, no bootloader |
| `softsim/nrf91_softsim_mcuboot_partitions.dtsi` | nRF91 DKs with MCUboot |
| `softsim/thingy91_softsim_partitions.dtsi` | Thingy:91 (factory MCUboot layout) |
| `softsim/thingy91x_softsim_partitions.dtsi` | Thingy:91 X (factory B0 + MCUboot layout) |
| `softsim/nrf91_softsim_sram.dtsi` | Matching SRAM split (TF-M / application) |

Apply one from a board overlay, as the sample does in
[`boards/`](../samples/softsim_external_profile/boards):

```c
#include <softsim/nrf91_softsim_partitions.dtsi>
#include <softsim/nrf91_softsim_sram.dtsi>
```

The DK no-bootloader, Thingy:91 and Thingy:91 X layouts keep the addresses of the earlier
Partition Manager layouts, so firmware upgrades keep provisioned profiles intact —
*provided the upgrade is flashed as described [below](#upgrading-provisioned-devices)*.
The DK MCUboot layout is new (no supported MCUboot configuration existed before) but keeps
`nvs_storage` at the same address.

Custom layouts are fine as long as a 32 kB `nvs_storage` partition exists. On the DK and
Thingy:91 X layouts that node **also carries the `storage_partition` label**, which is how
TF-M configures the range as non-secure in the SPU; without it the first NVS write faults.
(The Thingy:91 layout is the exception: `storage_partition` there is a separate settings
partition, and the SPU region comes out of the factory layout.)

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
the module gives those images the matching partition view where the factory layout differs
from the stock board devicetree. On the Thingy:91 X this matters: the stock view would let
a DFU swap write the image trailer into the SoftSIM storage area.

The deprecated Partition Manager flow still works during the transition window: build with
`-DSB_CONFIG_PARTITION_MANAGER=y` (static layouts in the `pm_static.yml` files, kept in
the sample and under [`boards/`](../boards)).

### Applications that already bring a layout

The SoftSIM layout **replaces** an application's own layout rather than stacking on it;
two complete layouts cannot both apply, and the second fails with
`undefined node label 'boot_partition'`. The NCS cellular samples hit this — they
`#include <samples/cellular/nrf91_no_bootloader_partitions.dtsi>` in
`boards/<board>.overlay`. Either edit that overlay, or override the application's overlay
list with `DTC_OVERLAY_FILE` (which replaces, unlike `EXTRA_DTC_OVERLAY_FILE`, which
appends):

```
west build --sysbuild -b nrf9151dk/nrf9151/ns nrf/samples/cellular/at_client -- \
  -DOVERLAY_CONFIG=$PATH_TO_ONOMONDO_SOFTSIM/overlay-softsim.conf \
  -DDTC_OVERLAY_FILE="$PATH_TO_ONOMONDO_SOFTSIM/dts/softsim/nrf91_softsim_partitions.dtsi;$PATH_TO_ONOMONDO_SOFTSIM/dts/softsim/nrf91_softsim_sram.dtsi"
```

## The filesystem template

The `nvs_storage` partition must be pre-populated with the SoftSIM's initial filesystem
(see [the filesystem](architecture.md#the-filesystem)). The build relocates
[`lib/profile/template.bin`](../lib/profile/template.bin) to the partition address as
`template.hex`.

The recommended flow is automatic — set in `sysbuild.conf`:

```
SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX=y
```

Sysbuild ([`sysbuild/CMakeLists.txt`](../sysbuild/CMakeLists.txt)) merges firmware and
template into one image and points `west flash` at it. Manually:
`west flash --hex-file build/<app>/onomondo-softsim/template.hex`. Under the Partition
Manager the hex is generated in sysbuild scope instead —
`west build -t onomondo_softsim_template`, output at
`build/onomondo-softsim/template.hex`.

If the partition table changes, the template address changes with it — rebuild and
re-flash. Check the resolved layout in `build/<app>/zephyr/zephyr.dts`, or with
`west build -t partition_manager_report` under the Partition Manager.

`template.bin` is a **prebuilt NVS image** checked into this repository; the build only
relocates it. The on-device file tree therefore comes from that image, not from the
submodule source — changes to file definitions in onomondo-uicc do not reach the device
until a new template is generated, and the generation tooling is not part of this
repository. Contact Onomondo if your changes need a modified template.

### Upgrading provisioned devices

The default build bundles the template into `build/merged.hex` and points `west flash` at
it. That is the right artifact for a **fresh or fully erased** device — but on a
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

## Resources and coexistence

| Resource | Requirement |
|---|---|
| Heap | `CONFIG_HEAP_MEM_POOL_SIZE` ≥ 30000 (build assert). The filesystem cache and working buffers come from the kernel heap; budget on top of your application's own use. |
| SoftSIM thread | 10 kB stack (`SOFTSIM_STACK_SIZE`). |
| Flash | 32 kB `nvs_storage` + the TF-M secure region, sized by the devicetree `slot0_s_partition` node: 0x18000 (96 kB) on both nRF91 DK layouts, 0x14000 (80 kB) on Thingy:91, 0x20000 (128 kB) on Thingy:91 X. Large applications may need features trimmed to fit — the linker reports overflow. The same applies to RAM. |
| TF-M | `CONFIG_BUILD_WITH_TFM=y` is a hard dependency (PSA crypto). The sample shows how to keep it small (`CONFIG_TFM_PARTITION_PROTECTED_STORAGE=n`, `CONFIG_PSA_CRYPTO_DRIVER_CC3XX=n`). |

The build asserts in [`lib/build_asserts.c`](../lib/build_asserts.c) catch the heap floor,
the partition size and the Settings clash with an explanatory message, so you will be told
rather than debug it. Two of them need context:

- **Zephyr Settings.** `CONFIG_SETTINGS_NVS` is rejected — the SoftSIM needs the
  `nvs_storage` partition. Switching to `CONFIG_SETTINGS_FCB=y` (or ZMS) is not enough on
  its own: those backends default to `storage_partition`, which on the DK and Thingy:91 X
  layouts *is* `nvs_storage`. Define a dedicated settings partition in your board overlay.
- **MCUboot on the DKs.** Enabling it without switching to the MCUboot partition layout
  leaves the devicetree with no `boot_partition`; add the overlay
  [above](#flash-partitioning).

Two more that no assert catches: some applications (e.g. `modem_shell`) fail to link with
`... uses VFP register arguments` — add `CONFIG_FP_SOFTABI=y`. And the crypto port needs
`CONFIG_PSA_WANT_KEY_TYPE_AES`, `CONFIG_PSA_WANT_ALG_CBC_NO_PADDING`,
`CONFIG_PSA_WANT_ALG_ECB_NO_PADDING` and `CONFIG_PSA_WANT_ALG_CMAC`, all already set by
`overlay-softsim.conf`.

## Custom boards and ports

- Any flash layout works if it contains a 32 kB partition labeled `nvs_storage`, plus the
  `storage_partition` label on the same node for TF-M's SPU configuration on DK-style
  layouts. Start from the closest [`dts/softsim/*.dtsi`](../dts/softsim).
- Moving `nvs_storage` is safe on unprovisioned devices; on provisioned fleets it orphans
  the profile. Plan the address to be stable across your product's DFU history — the
  shipped layouts are address-compatible with older Partition Manager releases for exactly
  this reason.
- **Memory**: `port_malloc`/`port_free` map to `k_malloc`/`k_free`. Point them at a
  dedicated `k_heap` to isolate the SoftSIM's ~30 kB from your application's heap
  accounting.
- **Storage**: for a different medium there are two cut points — reimplement the
  `ss_storage_*` operations of `storage.h` wholesale, or keep the submodule's
  `storage_compact.c` and implement the `fs.h` shim underneath it, as this module does
  (see [the filesystem](architecture.md#the-filesystem)). The cache layer is reusable if
  you keep the path→id model.
- **Crypto**: stays on PSA under TF-M on the nRF91. On other platforms, map it to whatever
  secure key store exists.

Three things that bite when porting to another modem or MCU:

- **Don't reject large Le values.** Modems legitimately request Le = 256; size response
  buffers for payload + status words (this module uses 260) and let the SIM core
  arbitrate, or attach fails in ways that look like network trouble.
- **Keys belong in the secure domain.** The external-crypto port exists so K/Ki never sits
  in application RAM after provisioning. `CONFIG_SOFTSIM_UICC_EXTERNAL_KEY_LOAD` (software
  crypto with an external key loader) is the fallback, not the goal.
- **Respect commit-on-close.** Files tagged `FS_COMMIT_ON_CLOSE` are written on every
  close; everything else is flushed at deinit. The MILENAGE sequence-number files are
  tagged, so a SEQ update survives power loss — a port that lazily batches all writes
  weakens AKA replay protection.
