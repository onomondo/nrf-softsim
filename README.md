# Onomondo SoftSIM for Nordic nRF91 Series

The Onomondo SoftSIM is an [Open Source](https://github.com/onomondo/onomondo-uicc) C based UICC implementation, allowing new and innovative cellular device designs to see the light of day in the ever-growing landscape of IoT!

To integrate this awesome new SoftSIM UICC form factor, we have partnered with Nordic Semiconductor to develop and distribute a new SoftSIM modem interface that supports APDU exchange between the modem and the application processor. For more details and an in-depth explanation, refer to Nordic Semiconductor's [documentation](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrfxlib/nrf_modem/doc/softsim_interface.html).

This README is the quick path to a running sample. The full documentation — the design, every interface and configuration option, and how to adapt the SoftSIM to your own product or platform — lives in [`docs/`](docs/README.md).

## Quick start

1. **Initialize a workspace.**

   ```
   west init -m https://github.com/onomondo/nrf-softsim.git
   git -C modules/lib/onomondo-softsim submodule update --init
   west update
   ```

   The `git submodule` step is required: `west update` does not initialize the manifest
   repository's own submodules, and without it `lib/onomondo-uicc` stays empty and the
   CMake configure step fails. To add the module to an existing application instead,
   see [docs/integration.md](docs/integration.md).

2. **Build and flash the sample.**

   ```
   cd modules/lib/onomondo-softsim/samples/softsim_external_profile
   west build --sysbuild -b nrf9151dk/nrf9151/ns
   west flash
   ```

3. **Fetch a profile.** Profiles are delivered through Onomondo's API, wrapped by the
   [onomondo-softsim-cli](https://github.com/onomondo/onomondo-softsim-cli). Generate an API key
   at [app.onomondo.com/api-keys](https://app.onomondo.com/api-keys), then:

   ```
   ./softsim fetch --api-key <your_api_key> -n 1    # download 1 encrypted profile into ./profiles
   ./softsim next --key=<path to your private key>  # decrypt and print the next unused profile
   ```

   `softsim next` guarantees a fresh profile on every call.

4. **Provision the device.** The flashed sample prompts on the UART (115200 baud):
   `Transfer SoftSIM profile using serial COM port, terminate by newline character`.
   Paste the `softsim next` output and press return. The device provisions, reboots
   and attaches:

   ```
   <inf> softsim_sample: Profile received: <n> characters in total
   ...
   <inf> softsim_sample: LTE connected!
   ```

   Other delivery transports are covered in [docs/provisioning.md](docs/provisioning.md).

## License

The Onomondo nrf-softsim repository is provided under:

- SPDX-License-Identifier: GPL-3.0-only

This repository is licensed under the terms of the GNU General Public License version 3 only, as provided in:

- [LICENSE](LICENSE)
