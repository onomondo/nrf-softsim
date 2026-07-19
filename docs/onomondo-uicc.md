# onomondo-uicc — the SIM core

[onomondo-uicc](https://github.com/onomondo/onomondo-uicc) is a pure C, dependency-free
implementation of a SIM/UICC/USIM. nrf-softsim vendors it as a git submodule at
[`lib/onomondo-uicc`](../lib/onomondo-uicc) and builds it from source, so every layer
is steppable in a debugger.

## Specification coverage

| Area | Specification |
|---|---|
| UICC platform: filesystem, commands, PINs, ATR | ETSI TS 102 221 |
| Administrative commands (CREATE FILE, DELETE FILE, ACTIVATE FILE) | ETSI TS 102 222 |
| USIM application: authentication, USIM files | 3GPP TS 31.102 |
| BER-TLV encoding used throughout | ETSI TS 101 220 §7 |
| OTA secured packets (SMS-delivered) | ETSI TS 102 225 |
| OTA remote APDUs / Remote File Management | ETSI TS 102 226 |
| Authentication and Key Agreement | MILENAGE (3GPP) |

### Supported

- APDU command parser / encoder
- BER-TLV encoder/decoder
- Smart-card filesystem: MF, DFs, EFs (transparent and linear fixed), ADFs (e.g.
  ADF.USIM), access control via Access Rule Referencing (EF.ARR)
- File commands: SELECT, STATUS, READ/UPDATE BINARY, READ/UPDATE RECORD, SEARCH RECORD,
  CREATE FILE, DELETE FILE
- PIN management: VERIFY, CHANGE, ENABLE, DISABLE, UNBLOCK
- USIM AKA with MILENAGE, including sequence-number management
- CAT / proactive SIM (TERMINAL PROFILE, ENVELOPE, FETCH, REFRESH) to the extent OTA
  needs it
- OTA Remote File Management over secured SMS packets (shared filesystem and ADF.USIM
  RFM), AES and 3DES packet security
- UICC SUSPEND (optional, `CONFIG_SOFTSIM_UICC_USE_EXPERIMENTAL_SUSPEND_COMMAND`)

### Not supported

- CAT/STK beyond OTA RFM + REFRESH
- Applets of any kind (no JavaCard), GlobalPlatform, SCP
- Cyclic and BER-TLV files
- File DEACTIVATE and deactivated-state handling (ACTIVATE FILE *is* implemented, as
  the personalization step that finalizes a file or the card)
- Logical channels beyond the default channel and OTA
- Secure Messaging (ISO/IEC 7816-4)
- ISIM application

This list is maintained here because it corrects the upstream
[submodule README](https://github.com/onomondo/onomondo-uicc/blob/master/README.md) in a few places (ACTIVATE FILE,
ISO 7816-4); check both before "fixing" a difference.

## Persistence

The filesystem is the single abstraction for persistence. Data no terminal ever reads
lives in files too, in a reserved id range with access rules that never match:

| File | Contents |
|---|---|
| `A001` | SIM authentication keys. On the nRF91 the key field holds only a one-byte key tag (the key itself is in the secure key store), but the file is still required — it stores the OPc and is read on every AUTHENTICATE |
| `A1xx` | MILENAGE sequence numbers |
| `A003` | PINs and their state |
| `A004` | OTA TARs and keys for remote commands |

While the MF is in the creation/initialization state, access control is suspended so
the file tree can be built; ACTIVATE on the MF finalizes the card. That is how the
[template filesystem](configuration.md#the-filesystem-template) is produced.

Platform specifics sit behind four port interfaces (storage, crypto, memory, logging) —
see [Interfaces](interfaces.md). Reused code comes under permissive licenses: the
MILENAGE and AES reference implementations originate from wpa_supplicant (BSD).

## Source layout

```
lib/onomondo-uicc/
├── include/onomondo/softsim/   public headers (core API + port interfaces)
├── src/softsim/
│   ├── uicc/          APDU/TLV codecs, filesystem, commands, PINs,
│   │                  authentication, proactive SIM, OTA, suspend
│   ├── milenage/      MILENAGE algorithm (BSD, from wpa_supplicant)
│   ├── crypto/        software AES/3DES fallback (BSD; replaced by PSA on
│   │                  the nRF91 via CONFIG_SOFTSIM_UICC_EXTERNAL_CRYPTO_IMPL)
│   ├── storage.c      host storage backend (one file per EF, on disk)
│   ├── storage_compact.c  compact storage backend (used on the nRF91)
│   └── main.c         host demo: runs the SIM against vpcd (PC/SC)
├── utils/             profile decoding, provisioning helpers
├── files/             a card filesystem definition for host runs
├── gscriptor/         APDU-level test scripts
└── tests/             unit and integration tests
```

The build produces four static libraries — `uicc`, `milenage`, `crypto`, `storage` —
which [`lib/CMakeLists.txt`](../lib/CMakeLists.txt) configures via CMake cache options
and links into the Zephyr application. Each option is surfaced as a Kconfig symbol; see
[Configuration](configuration.md#onomondo-uicc-submodule-options).

## Running it without hardware

The host build connects to the
[vsmartcard](https://frankmorgner.github.io/vsmartcard/virtualsmartcard/README.html)
virtual reader (`vpcd`), where it shows up as a regular PC/SC card.
[pySim](https://osmocom.org/projects/pysim/wiki) can then browse the filesystem
interactively, and the `gscriptor` scripts replay APDU test sequences. Build
instructions: [submodule README](https://github.com/onomondo/onomondo-uicc/blob/master/README.md).

This is the fastest way to reproduce filesystem issues and validate changes to the core
before touching hardware.

## Licensing

onomondo-uicc is **GPL-3.0-only**, with the imported MILENAGE/AES components under
**BSD-3-Clause** ([`LICENSE`](https://github.com/onomondo/onomondo-uicc/blob/master/LICENSE) and
[`BSD-3-Clause`](https://github.com/onomondo/onomondo-uicc/blob/master/BSD-3-Clause) in the submodule). nrf-softsim is
likewise GPL-3.0-only ([`LICENSE`](../LICENSE)). If GPL obligations are a concern for
your product, contact Onomondo about licensing options before integrating.
