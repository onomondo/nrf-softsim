
# nrf-softsim

Manifest repo for integrating SoftSIM and nrf-sdk.

  

Plan here is to point people to this when using softsim. That way we can specify which nrf-sdk version should be used etc.

  
 ## quick start
 Find a nice spot for the NCS to live and initialize with west

`west init -m git@github.com:onomondo/nrf-softsim.git`
`west update`
This will fetch `onomondo-softsim` as a module. 

You might need to follow some of these steps if it's the first time working with NCS: https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_installing.html

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
CONFIG_PM_PARTITION_SIZE_LITTLEFS=0x13000 

# include softsim in build
CONFIG_SOFTSIM=y
CONFIG_SOFTSIM_AUTO_INIT=y

CONFIG_BUILD_WITH_TFM=y

```
# Flashing your device:
### By command line:
flash sim profile
`nrfjprog -f NRF91 --sectorerase --log --program path/to/profile/nrf_<sim_id>.hex`
flash sample
`nrfjprog -f NRF91 --sectorerase --log --program /path/to/build/zephyr/merged.hex --reset`


### using nrf Connect programmer
Add the compiled sample (`merged.hex`). Additionally find the SIM profile and add that. 

The profile should follow the pattern: `addr_<flash_base_addr>_nrf_<sim_id>.hex`. In general, the profile will reside in the upper end of the flash. 

![image](https://user-images.githubusercontent.com/46489969/217817885-8b993ab7-f41f-4082-b06e-fdaab3cbae54.png)



## samples
Yes. Should build out of the box. 


## trouble shooting... 
- `littlefs` is throwing a lot of errors. 
-- Compare the address for the storage partitioning i.e. `ninja partition_manager_report` with the base address in the sim profile 




