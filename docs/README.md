# Onomondo SoftSIM documentation

These pages document **nrf-softsim**, the nRF91 integration. They cover what the code
cannot tell you on its own: ordering constraints, hazards, and the reasons behind the
design. For option-by-option detail, read [`Kconfig`](../Kconfig) and
[`lib/include/nrf_softsim.h`](../lib/include/nrf_softsim.h) — both are documented at the
source.

Two codebases make up the SoftSIM:

| Repository | Role |
|---|---|
| [onomondo-uicc](https://github.com/onomondo/onomondo-uicc) | The SIM. A portable, dependency-free C implementation of a SIM/UICC/USIM — APDU parser, smart-card filesystem, USIM authentication (MILENAGE), OTA remote file management. It knows nothing about Zephyr or Nordic hardware; it reaches the outside world through four port interfaces. Feature boundaries, specification coverage and host testing are documented in that repository. GPL-3.0-only. |
| [nrf-softsim](https://github.com/onomondo/nrf-softsim) (this repository) | The nRF91 integration. A Zephyr module for the [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nrf-connect-sdk) that binds onomondo-uicc to the nRF91 modem, implements the port interfaces on Zephyr and TF-M, and ships a reference sample. |

The nRF91 modem firmware supports a software SIM: instead of driving the physical SIM
interface it forwards SIM requests (reset, APDUs) through the
[Modem library's SoftSIM interface](https://nrfconnectdocs.nordicsemi.com/ncs/3.4.0/nrfxlib/nrf_modem/doc/softsim_interface.html)
to the application core, where this module answers them using onomondo-uicc. That
interface was developed in partnership with Nordic Semiconductor.

```
┌─────────────────────────────────────────────┐
│ Your application            (Zephyr, NCS)   │
├─────────────────────────────────────────────┤
│ nrf-softsim        Zephyr module (this repo)│
│  · modem glue, work queue                   │
│  · ports: NVS fs + cache, PSA crypto,       │
│    heap, logging                            │
├─────────────────────────────────────────────┤
│ onomondo-uicc      portable SIM core        │
│  · APDU/TLV codecs, UICC filesystem,        │
│    USIM auth (MILENAGE), OTA (RFM)          │
├─────────────────────────────────────────────┤
│ nrf_modem SoftSIM interface     (nrfxlib)   │
├─────────────────────────────────────────────┤
│ nRF91 modem core       (cellular protocol)  │
└─────────────────────────────────────────────┘
```

nrf-softsim is consumed as a regular [west](https://docs.zephyrproject.org/latest/develop/west/index.html)
module ([`zephyr/module.yml`](../zephyr/module.yml)); its [`west.yml`](../west.yml) pins
the nRF Connect SDK version the release is validated against. onomondo-uicc is vendored
as a git submodule at [`lib/onomondo-uicc`](../lib/onomondo-uicc) and built from source.

## Pages

| Page | What it covers |
|---|---|
| [Architecture](architecture.md) | Request lifecycle, threading, boot flow, the NVS-backed filesystem, the security model, the port and source maps. |
| [Integration](integration.md) | Adding the module to your application, the options worth commenting on, flash partitioning, the filesystem template, resources and coexistence, adapting to your own board. |
| [Provisioning](provisioning.md) | Profile format, delivery, the on-device flow, key storage, re-provisioning. |

Just want it running? The [top-level README](../README.md) is the quick path.

## Supported hardware

nRF91 series (nRF9151, nRF9160, nRF9161) on the non-secure (`*/ns`) build targets —
TF-M is required for key storage. Validated board overlays ship for the nRF9151 DK,
nRF9160 DK, nRF9161 DK, Thingy:91 and Thingy:91 X. NCS v3.4.0 or later.
