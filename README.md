# Onomondo SoftSIM for Nordic nRF91 Series

The Onomondo SoftSIM is an [Open Source](https://github.com/onomondo/onomondo-uicc) C based UICC implementation, allowing new and innovative cellular device designs to see the light of day in the ever-growing landscape of IoT!

To integrate this awesome new SoftSIM UICC form factor, we have partnered with Nordic Semiconductor to develop and distribute a new SoftSIM modem interface that supports APDU exchange between the modem and the application processor. For more details and an in-depth explanation, refer to Nordic Semiconductor's [documentation](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrfxlib/nrf_modem/doc/softsim_interface.html).

This README is the quick path to a running sample. The full documentation — the design, every interface and configuration option, and how to adapt the SoftSIM to your own product or platform — lives in [`docs/`](docs/README.md).


## Quick Setup Guide

The Onomondo SoftSIM samples for nRF91 Series SiP's can be imported as a Zephyr module within the [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nrf-connect-sdk).

A new SDK can be initiated with the following two commands if you are already a user of west and nrf:

```
west init -m https://github.com/onomondo/nrf-softsim.git
git -C modules/lib/onomondo-softsim submodule update --init
west update
```

Getting started with the external profile sample:
```
cd modules/lib/onomondo-softsim/samples/softsim_external_profile
west build --sysbuild -b nrf9151dk/nrf9151/ns
west flash
```

## Prerequisites

### Initialize the onomondo-uicc git submodule

`onomondo-uicc` is bundled as a git submodule of this repository (under `lib/onomondo-uicc`). `west update` does **not** automatically initialize submodules of the manifest self repo, so you must run:

```
git -C modules/lib/onomondo-softsim submodule update --init
```

Skipping this step leaves `lib/onomondo-uicc` empty and the CMake configure step will fail.

### Get access to your free Onomondo SoftSIM profile
SoftSIM profiles are delivered through our API. As this can be a bit cumbersome, we've developed a small tool to make this process easier. The tool is available at [softsim-cli](https://github.com/onomondo/onomondo-softsim-cli). Additional instructions can be found in the CLI repository.

1. Generate an API key on [app.onomondo.com/api-keys](https://app.onomondo.com/api-keys). Follow the instructions on the app.
2. Download the `softsim` cli tool for your platform.
3. Fetch your profile: `./softsim fetch --api-key <your_api_key> -n 1`. This will create a `profiles` directory for you with `1` encrypted profile.

Every time you require a new profile, simply use the `./softsim next --key=<path to your private key>`. It will look in the `./profiles` folder and decrypt and format a profile. _This command guarantees that a new profile is given each time._


## License

The Onomondo nrf-softsim repository is provided under:

- SPDX-License-Identifier: GPL-3.0-only

This repository is licensed under the terms of the GNU General Public License version 3 only, as provided in:

- [LICENSE](LICENSE)
