# Architecture

How the SoftSIM is put together on the nRF91: who calls whom in which context, and how
SIM data is persisted and protected.

The SIM itself — specification behaviour, no platform assumptions — is
[onomondo-uicc](https://github.com/onomondo/onomondo-uicc), vendored as a submodule.
This repository is the nRF91 port: the modem transport plus implementations of the four
port interfaces (storage, crypto, memory, logging) on Zephyr and TF-M. Replace that
bottom layer and the SIM core runs elsewhere — which is what the submodule's host/PC
build does.

## Request lifecycle

The modem drives everything. When it needs its SIM it issues requests through the Modem
library's SoftSIM interface, and the glue in
[`lib/nrf_softsim.c`](../lib/nrf_softsim.c) answers them:

1. The Modem library invokes the handler registered with
   `nrf_modem_softsim_req_handler_set()`. **This callback runs in an interrupt service
   routine**, so it must not block.
2. The handler allocates a request node, puts it on a FIFO, and submits work to a
   **dedicated work queue** (own thread, 10 kB stack, `SOFTSIM_STACK_SIZE`).
3. The worker drains the FIFO and dispatches on the request type. Every case answers
   with `nrf_modem_softsim_res()`:

   | Request | What the worker does |
   |---|---|
   | `NRF_MODEM_SOFTSIM_INIT` | Create the SIM context (`ss_new_ctx()`) if needed; unless suspended, reset it and prime the filesystem (`ss_init_fs()`). Answers with the ATR from `ss_atr()`. |
   | `NRF_MODEM_SOFTSIM_APDU` | Feed the command APDU to `ss_application_apdu_transact()`. Answers with the response APDU + status words. |
   | `NRF_MODEM_SOFTSIM_RESET` | `ss_reset()` — warm reset of the SIM state. The modem issues this when a request became unresponsive, so answer promptly. |
   | `NRF_MODEM_SOFTSIM_DEINIT` | Unless suspended: free the context and `ss_deinit_fs()`, committing cached writes to flash. |

4. Request payloads handed over by the Modem library are released with
   `nrf_modem_softsim_data_free()`.

APDU buffers are sized `SIM_HAL_MAX_LE` (260) bytes: the modem may request the full
256-byte short-APDU payload, plus room for the status words. A port that rejects large
Le values will break LTE attach.

The modem uses the software SIM only when selected with `AT%CSUS=2`. Selection is
accepted only while the modem is deactivated, and is committed to modem NVM on
`AT+CFUN=0`; `AT%CSUS=0` reverts to the physical SIM. The rest of the contract is in the
[SoftSIM interface documentation](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrfxlib/nrf_modem/doc/softsim_interface.html)
(`nrf_modem_softsim.h` in nrfxlib; the link matches the NCS version pinned in
[`west.yml`](../west.yml)).

### Threading

APDU and context processing runs on the work queue only, so the SIM core needs no
locking of its own. The *filesystem* is not confined to that thread:
`nrf_softsim_init()` calls `ss_init_fs()` and `ss_new_ctx()` in its caller's context,
and `nrf_softsim_provision()` runs on the application thread and mutates the same cache
(`port_provision()` in [`lib/ss_fs.c`](../lib/ss_fs.c)). **Provision before the modem is
activated, not concurrently with it.**

### Error handling

`nrf_modem_softsim_err()` has one call site: the ISR, when the request node cannot be
allocated. Inside the worker the SIM-core calls are not error-checked at all — return
values from `ss_init_fs()`/`ss_deinit_fs()` are discarded, `ss_reset()` is `void`, and
the lengths from `ss_atr()` and `ss_application_apdu_transact()` go straight into the
response — so a SIM-core failure is still answered, just with a possibly empty or bogus
payload. Only `nrf_modem_softsim_res()` itself is checked; when *it* fails the error is
logged and the request goes unanswered, and the modem falls back on its own timeout and
`RESET`.

### Suspend

The modem can suspend the SIM (UICC SUSPEND, enabled by
`CONFIG_SOFTSIM_UICC_USE_EXPERIMENTAL_SUSPEND_COMMAND`). While suspended, `DEINIT` keeps
the SIM context and filesystem alive so the follow-up `INIT` resumes instead of
cold-booting.

## Boot flow

With `CONFIG_SOFTSIM_AUTO_INIT=y` (the default) everything is wired up before `main()`:

1. `SYS_INIT(nrf_softsim_init, APPLICATION, 0)` primes the filesystem, provisions the
   static profile if one is configured (and the device is not already provisioned),
   registers the modem request handler, and starts the work queue.
2. An `NRF_MODEM_LIB_ON_INIT` hook issues `AT%CSUS=2` as soon as the Modem library
   initializes, selecting the software SIM.
3. When the application activates the modem (`AT+CFUN=1` / `lte_lc_connect()`), the
   modem sends `INIT` and the APDU exchange begins.

With `CONFIG_SOFTSIM_AUTO_INIT=n` **both** the `SYS_INIT` and the `AT%CSUS=2` hook are
compiled out — they live in the same `#ifdef`. The application then owns both halves:

1. Call `nrf_softsim_init()` before any other SoftSIM API;
   `nrf_softsim_provision()` and `nrf_softsim_check_provisioned()` need the filesystem
   it initializes.
2. Send `AT%CSUS=2` itself after `nrf_modem_lib_init()`.

That is what runtime SIM selection needs: bring SoftSIM up and select it only when a
profile is actually provisioned, so an unprovisioned device falls back to a physical SIM
without a separate firmware build. The sample builds this way with
`-DEXTRA_CONF_FILE=overlay-manual-init.conf`.

![SoftSIM request flow](https://github.com/onomondo/nrf-softsim/assets/46489969/7513bb06-99b3-4de4-95bb-34884a9726ed)

## What the modem actually asks for

Most of what a SIM does is filesystem access with access control. Activating the SIM
(`AT+CFUN=41`) starts a long run of `SELECT` followed by `READ BINARY` or `READ RECORD`
— `00a408040000022fe20168` selects EF.ICCID, `00b000000a` reads it, and so on for tens of
files. The exception is `AUTHENTICATE`, which runs MILENAGE, derives session keys and
validates that the network is not an imposter. See [Provisioning](provisioning.md).

## The filesystem

The SIM core addresses files by hierarchical path (`/3f00/7ff0/6f07` — MF, then
ADF.USIM, then EF.IMSI). Reaching flash from there runs through four pieces:

```
SIM core ── storage.h ── storage_compact.c ── fs.h ── ss_fs.c / ss_cache.c ── NVS
            (port)       (submodule)          (shim)  (this repo)
```

`storage_compact.c` is the submodule's embedded backend
(`CONFIG_SOFTSIM_UICC_COMPACT_STORAGE=y`); it does no hardware I/O itself, but calls a
stdio-like shim declared in `fs.h` (`ss_fopen`, `ss_fread`, `ss_fwrite`, `ss_fclose`, …).
**That shim is what this repository implements**, on top of Zephyr's
[NVS](https://docs.zephyrproject.org/latest/services/storage/nvs/nvs.html) in a dedicated
32 kB `nvs_storage` flash partition. Either boundary is a valid cut point for a port.

NVS is a `uint16 id → blob` store, so a translation layer is needed:

- **Directory entry.** NVS record 1 holds the map from paths to NVS ids as consecutive
  `[path_len (1 byte), id (2 bytes big-endian), path]` records, ordered roughly by access
  frequency so lookups terminate early. At boot it is parsed into a linked list
  (`generate_dir_table_from_blob()` in [`ss_cache.c`](../lib/ss_cache.c)).

  ```
  00a408040000022fe20168  →  open("/3f00/2fe2")  →  nvs_read(id=14)
  ```

- **Id flags.** The upper byte of each id is the flags field
  (`_flags = (id & 0xFF00) >> 8`). `FS_COMMIT_ON_CLOSE` (`1 << 7`) is the one in use.
- **Cache.** File content is cached in RAM once read. Writes normally stay in the cache
  and are flushed on `DEINIT`/`ss_deinit_fs()`; files flagged `FS_COMMIT_ON_CLOSE` (the
  MILENAGE sequence-number files) are committed to flash on every close, trading wear for
  integrity.

<p align="center">
 <img width="338" src="https://github.com/onomondo/nrf-softsim/assets/46489969/e77404ed-f8fd-46c8-98d8-054258727b8b">
 <img width="358" src="https://github.com/onomondo/nrf-softsim/assets/46489969/c03113b3-f41b-41c7-b681-0e2b09f7ee7b">
 <img height="338" src="https://github.com/onomondo/nrf-softsim/assets/46489969/815529a8-caf4-485f-a752-1a6242bec082">
</p>

Every fresh SIM shares the same file tree — only identity and keys differ — so the module
ships that tree as a prebuilt **template**
([`lib/profile/template.bin`](../lib/profile/template.bin)), flashed to `nvs_storage`
alongside the firmware. Provisioning personalizes a handful of records. See
[Provisioning](provisioning.md) and
[the filesystem template](integration.md#the-filesystem-template).

## Security model

- **Authentication keys.** Provisioning imports K/Ki, KIC and KID through the PSA Crypto
  API as persistent keys with ids 10/11/12 ([`lib/ss_crypto.h`](../lib/ss_crypto.h)),
  inside TF-M — hence `CONFIG_BUILD_WITH_TFM` is a hard dependency. They are imported
  without `PSA_KEY_USAGE_EXPORT`, so the application can use them by id but never read
  them back; MILENAGE and OTA integrity/confidentiality run by key reference
  ([`lib/ss_crypto.c`](../lib/ss_crypto.c)). The key file in the filesystem (`A001`)
  carries only a one-byte key tag in place of each key — but the MILENAGE operator
  constant OPc stays in `A001` on flash, so the filesystem's confidentiality still
  matters.
- **Replay protection.** The MILENAGE sequence-number files are flagged
  `FS_COMMIT_ON_CLOSE`, so a power loss cannot roll them back; OTA counters follow the
  normal cache lifecycle and are committed on `DEINIT`.
- **Storage partition isolation.** TF-M configures the `nvs_storage` range as non-secure
  via the SPU, driven by the devicetree `storage_partition` label (see
  [Flash partitioning](integration.md#flash-partitioning)), so the application can reach
  its own SIM filesystem while the keys stay in the secure domain.

## Ports

onomondo-uicc reaches the platform through four interfaces, declared in the submodule's
[`include/onomondo/softsim/`](https://github.com/onomondo/onomondo-uicc/tree/master/include/onomondo/softsim):

| Port | Header | nRF91 implementation |
|---|---|---|
| Storage | `storage.h` (+ the `fs.h` shim) | [`ss_fs.c`](../lib/ss_fs.c) + [`ss_cache.c`](../lib/ss_cache.c): Zephyr NVS + RAM cache |
| Crypto | `crypto.h` | [`ss_crypto.c`](../lib/ss_crypto.c): PSA Crypto, keys by reference (AES/AES-CMAC only — the 3DES entry points are stubs) |
| Memory | `mem.h` | [`ss_heap.c`](../lib/ss_heap.c): `k_malloc()` / `k_free()` |
| Logging | `log.h` | [`ss_logp_zephyr.c`](../lib/ss_logp_zephyr.c): Zephyr log module |

`CONFIG_SOFTSIM_UICC_EXTERNAL_CRYPTO_IMPL=y` (the nRF91 default) is what swaps the
submodule's software AES/3DES for the PSA port; `CONFIG_SOFTSIM_UICC_EXTERNAL_KEY_LOAD`
is the alternative for platforms keeping software crypto with keys from a secure element.

## The rest of the tree

Beyond the port implementations above and
[`lib/nrf_softsim.c`](../lib/nrf_softsim.c) (modem glue, work queue, init, provisioning):

| Path | Contents |
|---|---|
| [`lib/include/nrf_softsim.h`](../lib/include/nrf_softsim.h) | The application API, documented at the source |
| [`lib/build_asserts.c`](../lib/build_asserts.c) | Compile-time layout checks (32 kB `nvs_storage`, heap floor, settings-partition clash, A001/A004 layout) |
| [`dts/softsim/`](../dts/softsim) | Devicetree partition layouts |
| [`sysbuild/`](../sysbuild) | Template-hex generation and merging |
| [`samples/softsim_external_profile/`](../samples/softsim_external_profile) | The reference application |
| [`tests/`](../tests) | Twister suites run on `native_sim` — `west twister -T tests/` |
