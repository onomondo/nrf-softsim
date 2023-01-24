
# nrf-softsim

Manifest repo for integrating SoftSIM and nrf-sdk.

  

Plan here is to point people to this when using softsim. That way we can specify which nrf-sdk version should be used etc.

  
 ## quick start
 Find a nice spot for the NCS to live and initialize with west

`west init -m git@github.com:onomondo/nrf-softsim.git`
`west update`
This will fetch `onomondo-softsim` as a module. 

## manual stuff we still require
Current build (experimental `nrfxlib`) implements `nrf_modem_os_softsim_defer_req`. SoftSIM does that now. 

### How to get running:
-  `nrf/lib/nrf_modem_lib/nrf_modem_lib.c` -> remove all softsim related stuff. Including TEE
-  `nrf/lib/nrf_modem_lib/CMakeLists.txt` -> remove all softsim related stuff. Including TEE.

This will suffice.  
```
zephyr_library()

zephyr_library_sources(nrf_modem_lib.c)
zephyr_library_sources(nrf_modem_os.c)
zephyr_library_sources_ifdef(CONFIG_NET_SOCKETS nrf91_sockets.c)
zephyr_library_sources(errno_sanity.c)
zephyr_library_sources(shmem_sanity.c)
```
- `nrf/lib/nrf_modem_lib/Kconfig` Comment out `select BUILD_WITH_TFM` `select TFM_IPC`
-- SPM and flash access does not play well together in this old ish sdk and reads will throw a security exception. 

### In short. Most stuff has been moved to onomondo-softsim
... and we just need the static library that enables SoftSIM




## config
```
# softsim uses (a) file system as storage backend. Currently this must be enabled by the user. 
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_MPU_ALLOW_FLASH_WRITE=y
 
CONFIG_FILE_SYSTEM=y
# or use NVM. To be determined
CONFIG_FILE_SYSTEM_LITTLEFS=y

# softsim is flashed seperately. location and size must be static and well defined
CONFIG_PM_PARTITION_SIZE_LITTLEFS=0xF000

# include softsim in build
CONFIG_SOFTSIM=y
CONFIG_SOFTSIM_AUTO_INIT=y
```

## flash sim profile
`nrfjprog -f NRF91 --sectorerase --log --program path/to/profile/nrf_<sim_id>.hex --reset`

## flash sample 
`nrfjprog -f NRF91 --sectorerase --log --program /path/to/buildzephyr/merged.hex --reset`

## samples
Yes. Should build out of the box. 

