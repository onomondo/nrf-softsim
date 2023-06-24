
# nrf-softsim

Manifest repo for integrating Onomondo SoftSIM and NCS.

## New in v. 1.xx
LitleFS has been dropped for now in favor of smaller profiles and more efficent use of flash (denser storage). We now rely on NVS for profile storage. 

Network keys and SIM identity (IMSI/ICCID) is now secured by protected storage 

`mem.h`/`heap_port.c` can now optionally be used to implement custom allocators - i.e. `k_malloc()  ` to avoid conflict with stdc heap pools. We default to `k_malloc()/k_free()`

## planned changes
- Profile size can be decreased even further by tweaking the softsim core a bit. This unfortunately means that the current `template` has to be reworked as internal format changes. All softsim profile data is currently storred as hex and is internally parsed to uint8. By omitting this step we can introduce performance gains, reduction in code size and half the space required for the profile.

- All authentication can be done with the crypto engine as backend. This means that keys on provisioning should be written to the HUK directly. This also adrresses security concerns and will probably introduce some optimization in terms of execution speed and code size. 

### The template profile
Included is now a 'default' profile. This contains everything that is common to SoftSIMs. The personalization is done with call to `int nrf_sofsim_provision(uint8_t * profile, size_t len);`

`nrf_sofsim_provision` unwraps the profile and puts the content into protected storage at appropriate locations. 

The current status of the softsim can be queried with `nrf_sofsim_check_provisioned()`. 

Full sample is included. Snippet here: 

``` 
const char *profile_read_from_external_source = k_malloc(332);
  
read_from_prodline(profile_read_from_external_source);
  
// full profile
// "0829430500xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx30040434724";

printk("Provisioning SoftSIM profile.\n");
nrf_sofsim_provision((uint8_t *)profile_read_from_external_source,
                       strlen(profile_read_from_external_source));
k_free(...);
```

 ## Quick start
 Find a nice spot for the NCS to live and initialize with west

`west init -m git@github.com:onomondo/nrf-softsim.git`
`west update`
This will fetch `onomondo-softsim` as a module. 

You might need to follow some of these steps if it's the first time working with NCS: https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_installing.html


## bigger todos:
- [ ] (In progress) On `NRF_SOFTSIM_DEINIT` the internal buffers should be commit to flash in case device is about to power down. 
- [ ] (In progress) On `NRF_SOFTSIM_DEINIT` the DIR entry should potentially be updated. In rare occasions  files/dirs are added/removed which should result in a change in `DIR` content
- [x] Removing directories is a bit abstract. Probably just remove all files with matching file name as suggested in code
- [x] Dump flash from a "provisioned" device and use the NVS chunk as the template profile. 
- [ ] Protected storage is painfully slow when provisioning? No problem when device is provisioned already. No biggie. 

## config
```
# softsim uses (a) file system as storage backend. Currently this must be enabled by the user. 
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_MPU_ALLOW_FLASH_WRITE=y

# softsim is flashed seperately. location and size must be static and well defined
CONFIG_BUILD_WITH_TFM=y
CONFIG_TFM_PROFILE_TYPE_MEDIUM=y

CONFIG_NVS=y
CONFIG_PM_PARTITION_SIZE_NVS_STORAGE=0x8000 

# include softsim in build
CONFIG_SOFTSIM=y
CONFIG_SOFTSIM_AUTO_INIT=y
```





  


# Flashing your device:
### By command line:
flash sim profile
`nrfjprog -f NRF91 --sectorerase --log --program path/to/profile/template.he`
flash sample
`nrfjprog -f NRF91 --sectorerase --log --program /path/to/build/zephyr/merged.hex --reset`


### using nrf Connect programmer
Add the compiled sample (`merged.hex`). 

Drag the `template.hex` into the view. Flash both. SoftSIM sample is ready to provision

<img width="359" alt="image" src="https://github.com/onomondo/nrf-softsim-dev/assets/46489969/360e44af-4776-4f25-a146-48a20cb01505">


## samples
Yes. Should build out of the box. 

## SoftSIM integration in application code
Either call `nrf_softsim_init()` explicitly or let the kernel do it on boot with the config option. 

SoftSIM entrypoint starts its own workqueue and returns immidiately after. The handler installed with `nrf_modem_softsim_req_handler_set()` will enqueue request as they come and the workqueue will unblock and handle the request. The softsim context will be blocked most of the time. The main interaction happens on boot. 

![softsim_nrf_flow](https://github.com/onomondo/nrf-softsim/assets/46489969/7513bb06-99b3-4de4-95bb-34884a9726ed)

Please note that SoftSIM internally need access to a storage partition. This should be pre-populated with the `template.hex` provided in the samples. The adress in the `template.hex` is hardcoded but can freely be moved around as pleased with an appropriate tool. The location is derived from the devicetree at compile time (` FIXED_PARTITION_DEVICE(NVS_PARTITION)`)


## foobar

The main challenge here is that the NVS is a "key-value" store i.e. `UINT16 -> void *`. SoftSIM needs a `char * path -> void * data` map. 

To over come this the first entry (ID 1) in the NVS will store a mapping from paths to actual ID's. 

The ID is leveraged to encode additional information, i.e. if content is in protected storage instead. 

```
Dir entry:

[[ID_1, PATH_LEN, PATH], [ID_n, PATH_LEN, PATH], ...  ]

Where ID is the actual ID where DATA is storred. 

ID & 0xFF00 (upper bits) are used for flags, i.e. 

#define FS_READ_ONLY         (1UL << 8)
#define FS_COMMIT_ON_CLOSE   (1UL << 7)  // commit changes to NVS on close
#define FS_SENSITIVE_DATA    (1UL << 6)  // clear buffer on close
#define FS_PROTECTED_STORAGE (1UL << 5)  // clear buffer on close
```
The inital DIR is designed to prioritize most accessed files as well. Internally files are cached and in general kept in memory. 

`FS_COMMIT_ON_CLOSE` is used for SEQ numbers that should be committed to flash directly to reduce attack vectors.






