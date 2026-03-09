  # Instructions
  This sample is based on the `softsim_static_profile` with the static profile from `prj.conf` removed and `SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX` disabled.

  Before using this sample on a device, that device must be first provisioned using one of the other samples.
  The sample will fault if the device has not already been provisioned.

  Building with `SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX`enabled adds a section which initialises the flash partition `nonsecure_storage/nvs_storage`. However, for already provisioned devices, this re-initialisation will erase any existing provisioning data.

  Building without enabling `SB_CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX` will preserve the existing provisioning data, providing the flash partition `nonsecure_storage/nvs_storage` is at the same location.
