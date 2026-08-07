# Provisioning

A freshly flashed device carries the [filesystem template](configuration.md#the-filesystem-template)
— a complete, generic SIM with no identity. Provisioning personalizes it: one profile,
delivered once, gives the device its IMSI, ICCID and keys.

## The profile

A SoftSIM profile is a hex string of concatenated TLV fields (`TAG | LEN | DATA`, all
hex characters — `LEN` counts hex characters, not bytes):

| Tag | Field | Content |
|---|---|---|
| `01` | IMSI | Subscriber identity, encoded as in EF.IMSI |
| `02` | ICCID | Card identifier, encoded as in EF.ICCID |
| `03` | OPC | MILENAGE operator constant (pre-computed) |
| `04` | K/Ki | Subscriber authentication key |
| `05` | KIC | OTA ciphering key |
| `06` | KID | OTA integrity key |
| `07` | SMSP | SMS parameters (optional) |
| `08`–`0b` | PIN1, PIN2, ADM, PUK | PIN values (optional) |
| `0c` | SMSC | SMS service-center address (optional) |

The full tag list is in
[`ss_profile.h`](https://github.com/onomondo/onomondo-uicc/blob/master/include/onomondo/utils/ss_profile.h); the decoder
is `ss_profile_from_string()` in
[`utils/ss_profile.c`](https://github.com/onomondo/onomondo-uicc/blob/master/utils/ss_profile.c).

### Worked example: the GSMA TS.48 test profile

The sample's [`prj.conf`](../samples/softsim_external_profile/prj.conf) carries the
GSMA TS.48 standard USIM test profile in a comment. Decoded:

| Tag | Field | Raw hex | Decoded |
|---|---|---|---|
| `01` | IMSI | `080910101032547698` | `001010123456789` (MCC=001, MNC=01) |
| `02` | ICCID | `98001032547698103214` | `89000123456789012341` |
| `03` | OPC | `00000000000000000000000000000000` | all zeros (test value) |
| `04` | KI | `000102030405060708090A0B0C0D0E0F` | sequential bytes (test value) |
| `05` | KIC | `000102030405060708090A0B0C0D0E0F` | sequential bytes (test value) |
| `06` | KID | `000102030405060708090A0B0C0D0E0F` | sequential bytes (test value) |

So the IMSI field on the wire is `01` (tag) `12` (18 hex characters follow)
`080910101032547698`: an EF.IMSI length byte `08` (8 bytes of identity follow), then
`09` — high nibble `0` is the first digit, low nibble `9` flags an odd digit count and
identity type IMSI — then the remaining digits nibble-swapped in pairs
(`10 10 10 32 54 76 98` → `01 01 01 23 45 67 89`), giving `001010123456789`.

This profile lets the SIM initialize, but no public network will authenticate it; fetch
a real profile for connectivity.

## Getting profiles

Profiles are delivered encrypted through Onomondo's API. The
[softsim-cli](https://github.com/onomondo/onomondo-softsim-cli) wraps the flow:
`softsim fetch` downloads encrypted profiles, `softsim next` decrypts and prints the
next unused one — see the [README](../README.md) for the steps. That output string is
exactly what `nrf_softsim_provision()` expects.

## On-device flow

`nrf_softsim_provision(profile, len)` does three things
([`lib/nrf_softsim.c`](../lib/nrf_softsim.c)):

1. **Decode** the TLV string into an `ss_profile` (`ss_profile_from_string()`).
2. **Import the keys** — K/Ki, KIC, KID as PSA persistent keys with ids 10/11/12
   ([`lib/ss_crypto.h`](../lib/ss_crypto.h)). From this point they are non-extractable;
   all later crypto references them by id. The application should discard the plaintext
   profile string after the call.
3. **Write identity to the filesystem** (`port_provision()` in
   [`lib/ss_fs.c`](../lib/ss_fs.c)): IMSI, ICCID and the `A001`/`A004` records, plus
   EF.SMSP (`/3f00/7ff0/6f42`) whenever the profile carries tag `07` (SMSP) or `0c`
   (SMSC) — record 1 is read-modify-written, with the SMSC overlaid at byte 37.

`nrf_softsim_check_provisioned()` returns 1 only when both halves are in place: the KI
key exists *and* the stored IMSI differs from the template default. Applications branch
on it at boot — see
[`main.c`](../samples/softsim_external_profile/src/main.c).

Provisioning touches only NVS and the key store, so it can happen before or
independently of the modem. The sample reboots afterwards to release the UART back to
the AT host and start the modem against the fresh identity.

## Delivery transports

The API takes a buffer, not a transport. Two modes ship:

- **Serial** (the sample default): first boot waits for the profile on the UART,
  provisions, reboots — `echo "<profile>" > /dev/tty...`. See
  [`profile_serial.c`](../samples/softsim_external_profile/src/profile_serial.c).
- **Static** (`CONFIG_SOFTSIM_STATIC_PROFILE_ENABLE=y`, or
  `-DEXTRA_CONF_FILE=overlay-static.conf`): compiled into the firmware, provisioned at
  first `SYS_INIT`. Development only — every device flashed with that image gets the
  same identity.

Anything else works too: a factory fixture over RTT/SWD, BLE or NFC during
commissioning, or a fetch from your own backend over a bootstrap bearer. A base profile
is ~190 hex characters, up to ~410 with the optional SMSP/PIN/SMSC fields; the sample
allocates 512 bytes. See [Adapting](adapting.md).

## Factory reset on provision

A modem that previously ran another SIM keeps state in its NVM (registration data,
carrier settings, preferred networks) that can slow or confuse the first attach with a
new identity. `CONFIG_SOFTSIM_FACTORY_RESET_ON_PROVISION=y` compiles
`nrf_softsim_modem_factory_reset()` (`AT+CFUN=4`, then `AT%XFACTORYRESET=0`) and
`nrf_softsim_just_provisioned()` so the application can run the reset once, right after
provisioning, then reboot. It needs an initialized Modem library, so the sample defers
it until after `nrf_modem_lib_init()`. Initial-provisioning flows only.

## Re-provisioning and profile hygiene

- Provisioning again overwrites identity and keys — there is no multi-profile
  management (this is not an eUICC with ISD-P slots).
- A device is re-virginized by re-flashing the filesystem template; key slots are
  overwritten on the next provision. Note the
  [upgrade hazard](configuration.md#upgrading-provisioned-devices) — a partial template
  flash leaves an undefined filesystem, so erase the whole partition.
- Treat profile strings as secrets in your manufacturing pipeline: whoever holds the
  string holds the SIM (K/Ki included) until it is provisioned and destroyed.

<p align="center">
<img height="600" src="https://github.com/onomondo/nrf-softsim/assets/46489969/19083bea-2727-48d6-ad50-63f80384e4d8">
</p>
