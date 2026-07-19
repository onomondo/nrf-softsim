# Adapting the SoftSIM to your project

The sample is one validated way to run the SoftSIM, not the required one. This page
lists the extension points.

## Use it in your own NCS application

No changes to this repository are needed — consume it as a west module.

1. Add it to your application's `west.yml`. `submodules: true` is required: `west
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

   To point an existing NCS workspace at this repository's manifest instead:

   ```
   cd <ncs_base>
   git clone https://github.com/onomondo/nrf-softsim.git modules/lib/onomondo-softsim
   git -C modules/lib/onomondo-softsim submodule update --init
   west config manifest.path modules/lib/onomondo-softsim/
   west update
   ```

   The `submodule update` line is needed here too — `west update` does not cover the
   manifest-self repository's own submodules.

   Match `revision` to a release validated against your NCS version — each release pins
   its NCS in [`west.yml`](../west.yml) and lists it in the [changelog](../CHANGELOG.md).

2. Merge [`overlay-softsim.conf`](../overlay-softsim.conf) into your build, or copy the
   relevant options into your `prj.conf` (see [Configuration](configuration.md)).

3. Include a [partition layout](configuration.md#flash-partitioning) from a board
   overlay — note the
   [conflict](configuration.md#applications-that-already-bring-a-layout) if your
   application already brings one — and set `SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX=y`
   in `sysbuild.conf` so the filesystem template travels with the firmware.

4. Decide who initializes: keep `CONFIG_SOFTSIM_AUTO_INIT=y` for zero-touch startup, or
   set it to `n` and own both `nrf_softsim_init()` and `AT%CSUS=2`
   ([Boot flow](architecture.md#boot-flow)).
   `nrf_softsim_check_provisioned()` reads the SoftSIM filesystem and is only valid
   after initialization.

The sample is your integration test: when in doubt whether an issue is yours or the
module's, reproduce against
[`samples/softsim_external_profile`](../samples/softsim_external_profile). The
[`tests/`](../tests) Twister suites (`apdu`, `cache`, `crypto`, `fs`, `handler`,
`sample_serial`) run on `native_sim` and are the fastest way to check a change to the
port implementations: `west twister -T tests/`.

## Custom boards and flash layouts

- Any layout works if it contains a **32 kB partition labeled `nvs_storage`** — plus
  the `storage_partition` label on the same node for TF-M's SPU configuration on
  DK-style layouts (see [Configuration](configuration.md#flash-partitioning)). Start
  from the closest [`dts/softsim/*.dtsi`](../dts/softsim).
- Moving `nvs_storage` is safe on unprovisioned devices; on provisioned fleets it
  orphans the profile. Plan the address to be stable across your product's DFU history
  — the shipped layouts are address-compatible with older Partition Manager releases
  for exactly this reason.
- For bootloader setups, mirror the sample's
  [`mcuboot-partitions.overlay`](../samples/softsim_external_profile/mcuboot-partitions.overlay).
  On the Thingy:91 X the module injects the factory partition view into the MCUboot
  images automatically, because the stock view would otherwise let a DFU swap write the
  image trailer into the SoftSIM storage area.

## Tune or replace the port implementations

See [Interfaces](interfaces.md#port-interfaces) for the contracts.

- **Memory**: `port_malloc`/`port_free` map to `k_malloc`/`k_free`. Point them at a
  dedicated `k_heap` to isolate the SoftSIM's ~30 kB from your application's heap
  accounting.
- **Storage**: the NVS + cache design targets flash wear and read latency on the
  nRF91's internal flash. For a different medium there are two cut points —
  reimplement the `ss_storage_*` operations of `storage.h` wholesale, or keep the
  submodule's `storage_compact.c` and implement the `fs.h` shim underneath it, as
  nrf-softsim does. The cache layer is reusable if you keep the path→id model.
- **Logging**: already a thin shim; redirect `SS_LOGP` output anywhere.
- **Crypto**: stays on PSA under TF-M on the nRF91. On other platforms, map it to
  whatever secure key store exists.

## Port to another platform

A port to another modem/MCU combination consists of:

1. **A transport**: whatever your modem offers for remote SIM access — feed command
   APDUs to `ss_application_apdu_transact()` and return the responses, `ss_atr()` on
   reset. The [request lifecycle](architecture.md#request-lifecycle) describes the
   threading pattern that has proven out (single worker, FIFO in front).
2. **The four ports**: storage, crypto, memory, logging. The host build (`storage.c`,
   software crypto, system heap) is the minimal reference; [`lib/`](../lib) is the
   hardened one.
3. **A provisioning path**: template filesystem + profile injection
   ([Provisioning](provisioning.md)).

Things that bite:

- **Don't reject large Le values.** Modems legitimately request Le = 256 and use
  extended addressing; size response buffers for payload + status words (nrf-softsim
  uses 260) and let the SIM core arbitrate, or attach fails in ways that look like
  network trouble.
- **Keys belong in the secure domain.** The external-crypto port exists so K/Ki never
  sits in application RAM after provisioning. `CONFIG_SOFTSIM_UICC_EXTERNAL_KEY_LOAD`
  (software crypto with an external key loader) is the fallback, not the goal.
- **Respect commit-on-close.** Files tagged `FS_COMMIT_ON_CLOSE` are written on every
  `ss_fclose()`; everything else is flushed at deinit. The MILENAGE sequence-number
  files are tagged, so a SEQ update survives power loss — a port that lazily batches
  all writes weakens AKA replay protection.
- **Validate against a real card's behavior.** The submodule's
  [`gscriptor/`](https://github.com/onomondo/onomondo-uicc/tree/master/gscriptor) APDU scripts and a
  [host/vpcd run](onomondo-uicc.md#running-it-without-hardware) catch filesystem and
  command regressions before hardware enters the picture.

## Extending the SIM itself

Filesystem contents, new EFs, access rules and OTA behavior are onomondo-uicc territory,
developed upstream at
[onomondo/onomondo-uicc](https://github.com/onomondo/onomondo-uicc). The submodule is
consumed as source so you can experiment in-tree and upstream what proves useful.
Feature boundaries are listed in [onomondo-uicc](onomondo-uicc.md#not-supported).

One caveat on the nRF91: the on-device file tree comes from the prebuilt template image,
not from the source tree, so code changes to file definitions do not reach the device
until the template is regenerated (see
[the template](configuration.md#the-filesystem-template)).
