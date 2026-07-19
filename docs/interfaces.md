# Interfaces

Four programmable boundaries, top down. Integrating into your own application touches
only the first; changing transport or platform touches the rest. See
[Adapting](adapting.md) for which one to cut at.

## Application API

Declared in [`lib/include/nrf_softsim.h`](../lib/include/nrf_softsim.h):

| Function | Purpose |
|---|---|
| `int nrf_softsim_init(void)` | Prime the filesystem, register the modem request handler, start the work queue. Runs from `SYS_INIT` at `APPLICATION` level with `CONFIG_SOFTSIM_AUTO_INIT=y`; call it yourself when that is `n` — and then also send `AT%CSUS=2` yourself, because the hook that normally does is compiled out with it. |
| `int nrf_softsim_provision(uint8_t *profile, size_t len)` | Decode a TLV-encoded profile string, import its keys, write identity data to the filesystem. See [Provisioning](provisioning.md). |
| `int nrf_softsim_check_provisioned(void)` | Returns 1 when a profile is fully provisioned — both the KI key and a non-default IMSI are present. Valid only after initialization. |
| `bool nrf_softsim_just_provisioned(void)` | Whether provisioning succeeded during *this* boot. Only with `CONFIG_SOFTSIM_FACTORY_RESET_ON_PROVISION=y`. |
| `void nrf_softsim_modem_factory_reset(void)` | Wipe modem NVM (`AT+CFUN=4`, then `AT%XFACTORYRESET=0`) so the modem restarts clean with a new SIM identity; reboot afterwards. Requires an initialized Modem library. Only with `CONFIG_SOFTSIM_FACTORY_RESET_ON_PROVISION=y`. |
| `int ss_init_fs(void)` / `int ss_deinit_fs(void)` | Prime the filesystem cache from NVS / flush dirty entries back and release it. Called internally on `INIT`/`DEINIT`. |

`port_provision()` and `port_check_provisioned()` are internal helpers; use the
`nrf_softsim_*` wrappers, which add profile parsing and key installation.

The sample shows the intended usage: check provisioning state
([`main.c`](../samples/softsim_external_profile/src/main.c)), obtain a profile over
UART ([`profile_serial.c`](../samples/softsim_external_profile/src/profile_serial.c)),
call `nrf_softsim_provision()`, reboot, connect.

## Modem contract

The nRF91 modem delegates SIM access through the Modem library's
[SoftSIM interface](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrfxlib/nrf_modem/doc/softsim_interface.html)
(`nrf_modem_softsim.h` in nrfxlib; the link matches the NCS version pinned in
[`west.yml`](../west.yml)). As implemented in
[`lib/nrf_softsim.c`](../lib/nrf_softsim.c):

- Register one callback with `nrf_modem_softsim_req_handler_set()`. It receives
  `(enum nrf_modem_softsim_cmd req, uint16_t req_id, void *data, uint16_t data_len)`
  **in an interrupt service routine** — it must not block; nrf-softsim defers to a work
  queue immediately.
- Requests are `NRF_MODEM_SOFTSIM_INIT`, `NRF_MODEM_SOFTSIM_APDU`,
  `NRF_MODEM_SOFTSIM_RESET`, `NRF_MODEM_SOFTSIM_DEINIT`. Each is answered with
  `nrf_modem_softsim_res(req, req_id, data, len)` — the ATR for `INIT`, the response
  APDU for `APDU`, empty for the rest.
- `RESET` is the modem's recovery path, issued when a request became unresponsive.
- On failure, answer `nrf_modem_softsim_err(req, req_id)`.
- Request payloads are freed with `nrf_modem_softsim_data_free(data)` after handling.
- The modem uses the software SIM only when selected with `AT%CSUS=2`. Selection is
  only accepted while the modem is deactivated, and is committed to modem NVM on
  `AT+CFUN=0`; `AT%CSUS=0` reverts to the physical SIM. nrf-softsim issues it on modem
  init when `CONFIG_SOFTSIM_AUTO_INIT=y`.

## SIM core API

Declared in
[`include/onomondo/softsim/softsim.h`](https://github.com/onomondo/onomondo-uicc/blob/master/include/onomondo/softsim/softsim.h).
This is what a transport glue drives:

| Function | Purpose |
|---|---|
| `struct ss_context *ss_new_ctx(void)` / `void ss_free_ctx(ctx)` | Create / destroy a SIM instance. All state hangs off the opaque context. |
| `void ss_reset(ctx)` | Warm-reset the card state. |
| `size_t ss_atr(ctx, buf, len)` | Produce the Answer To Reset. |
| `size_t ss_application_apdu_transact(ctx, rsp, rsp_len, req, &req_len)` | Execute one command APDU, produce the response APDU including status words. The workhorse. |
| `size_t ss_transact(ctx, rsp, rsp_len, req, &req_len)` | Lower-level variant for callers speaking raw TPDUs (e.g. the host/vpcd build). |
| `void ss_poll(ctx)` | Give the card CPU time for proactive-SIM work outside a transaction. |
| `uint8_t ss_is_suspended(ctx)` | Whether the card is in UICC SUSPEND. |

## Port interfaces

onomondo-uicc reaches the platform through four interfaces under
[`include/onomondo/softsim/`](https://github.com/onomondo/onomondo-uicc/tree/master/include/onomondo/softsim). The
nRF91 implementations live in [`lib/`](../lib):

| Port | Header | Contract | nRF91 implementation |
|---|---|---|---|
| Storage | `storage.h` (+ `fs.h` shim) | Path-addressed file operations | [`ss_fs.c`](../lib/ss_fs.c) + [`ss_cache.c`](../lib/ss_cache.c): Zephyr NVS + RAM cache |
| Crypto | `crypto.h` | AES/3DES/CMAC used by MILENAGE and OTA | [`ss_crypto.c`](../lib/ss_crypto.c): PSA Crypto, keys by reference (AES/AES-CMAC only — the 3DES entry points are stubs) |
| Memory | `mem.h` | `port_malloc()` / `port_free()` | [`ss_heap.c`](../lib/ss_heap.c): `k_malloc()` / `k_free()` |
| Logging | `log.h` | `SS_LOGP(subsys, level, fmt, ...)` | [`ss_logp_zephyr.c`](../lib/ss_logp_zephyr.c): Zephyr log module, see [Configuration](configuration.md#logging) |

### Storage

The core addresses files by path list (`/3f00/7ff0/6f07`) and expects these operations
(`storage.h`):

```c
int        ss_storage_get_file_def(struct ss_list *path);
struct ss_buf *ss_storage_read_file(const struct ss_list *path, size_t offset, size_t len);
size_t     ss_storage_get_file_len(const struct ss_list *path);
int        ss_storage_write_file(const struct ss_list *path, const uint8_t *data, size_t offset, size_t len);
int        ss_storage_create_file(const struct ss_list *path, size_t file_len);
int        ss_storage_create_dir(const struct ss_list *path);
int        ss_storage_delete(const struct ss_list *path);
int        ss_storage_update_def(const struct ss_list *path);
int        ss_storage_set_path(const char *path);   /* host builds; storage root */
```

Two backends ship in the submodule: `storage.c` (host: one file per EF on disk) and
`storage_compact.c` (embedded, `CONFIG_SOFTSIM_UICC_COMPACT_STORAGE=y` — the nRF91
default). The compact backend does not talk to hardware itself: it performs all file
I/O through a stdio-like shim declared in `fs.h` (`ss_fopen`, `ss_fread`, `ss_fwrite`,
`ss_fclose`, `ss_fseek`, `ss_file_size`, …). **That shim is what the nRF91 port
implements.** The chain:

```
SIM core ── storage.h (ss_storage_*) ── storage_compact.c ── fs.h (ss_f*) ── ss_fs.c/ss_cache.c ── NVS
```

Either cut point works for a port — see
[Adapting](adapting.md#port-to-another-platform).
`CONFIG_SOFTSIM_UICC_NO_DEFAULT_IMPL=y` only excludes the library's host implementation
of the shim (`fs.c`), and only when compact storage is disabled.

### Crypto

With `CONFIG_SOFTSIM_UICC_EXTERNAL_CRYPTO_IMPL=y` (the nRF91 default) the library's
software AES/3DES is replaced by the platform's implementation of `crypto.h`:

```c
void ss_utils_aes_encrypt(uint8_t *buf, size_t len, const uint8_t *key, size_t key_len);
void ss_utils_aes_decrypt(uint8_t *buf, size_t len, const uint8_t *key, size_t key_len);
void ss_utils_3des_encrypt(uint8_t *buf, size_t len, const uint8_t *key);
void ss_utils_3des_decrypt(uint8_t *buf, size_t len, const uint8_t *key);
int  ss_utils_ota_calc_cc(uint8_t *cc, size_t cc_len, uint8_t *key, size_t key_len,
                          enum enc_algorithm alg, uint8_t *d1, size_t d1_len,
                          uint8_t *d2, size_t d2_len);
```

On the nRF91, "key" is a reference: provisioning imports K/Ki, KIC and KID as PSA
persistent keys with ids 10/11/12 (`KEY_ID_KI`, `KEY_ID_KIC`, `KEY_ID_KID` in
[`lib/ss_crypto.h`](../lib/ss_crypto.h)) and the port resolves key material inside the
secure domain, so plaintext keys never transit application RAM after provisioning. The
port also provides the key-management helpers used by provisioning:
`ss_utils_setup_key()` and `ss_utils_check_key_existence()`.

For platforms that keep the software crypto but load keys from a secure element,
`CONFIG_SOFTSIM_UICC_EXTERNAL_KEY_LOAD` (mutually exclusive with the external crypto
implementation) enables the `ss_load_key_external()` hook in
[`ss_crypto_extension.h`](https://github.com/onomondo/onomondo-uicc/blob/master/include/onomondo/utils/ss_crypto_extension.h):
the core calls it to resolve a key id into key material, uses it in a local buffer, and
zeroizes it afterwards.

### Memory

The core allocates through `port_malloc()`/`port_free()` (`mem.h`), which nrf-softsim
maps to the Zephyr kernel heap — hence the `CONFIG_HEAP_MEM_POOL_SIZE` requirement in
[Configuration](configuration.md#resource-requirements).
`CONFIG_SOFTSIM_UICC_USE_SYSTEM_HEAP=y` uses plain `malloc`/`free` instead (the host
build does this).

### Logging

Core diagnostics funnel through `SS_LOGP()` (`log.h`) with per-subsystem tags (FS,
STORAGE, APDU, AUTH, …). The Zephyr port forwards them to a dedicated log module — see
[Configuration](configuration.md#logging). `SS_LOGP` compiles away only when `NDEBUG`
is defined *and* `CONFIG_SOFTSIM_UICC_USE_LOGS=n`; a Zephyr build defines neither, so
filtering happens at the Zephyr log level.
