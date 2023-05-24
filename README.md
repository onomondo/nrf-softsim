
# nrf-softsim

Manifest repo for integrating SoftSIM and nrf-sdk.

  

Plan here is to point people to this when using softsim. That way we can specify which nrf-sdk version should be used etc.
## while it isn't beautiful. 
The sample will now 'listen' for a profile if it isn't provisioned yet
![image](https://github.com/onomondo/nrf-softsim-dev/assets/46489969/5e4ac617-42fb-4a67-9a04-bf50d13a1d5b)


## changes from publib repo
LitleFS has been dropped for now in favor of smaller profiles and more efficent (hopefully) use of flash (denser storage). 

Instead we rely on NVS for profile storage. 

`mem.h`/`heap_port.c` can now optionally be used to implement custom allocators - i.e. `k_malloc()  ` to avoid conflict with stdc heap pools. 



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
#define FS_PROTECTED_STORAGE (1UL << 5)  // store encrypted
```
The inital DIR is designed to prioritize most accessed files as well. Internally files are cached and in general kept in memory. 

`FS_COMMIT_ON_CLOSE` is used for SEQ numbers that should be committed to flash directly to reduce attack vectors.

### Template profiles and provisioning

If `BOOTSTRAP_TEST` is defined SoftSIM will write all files needed for a basic template profile (no IMSI etc). It increases code-size as the whole profile is included in the application, i.e. 

In `provision.c`:
```
const struct bootstrap_entry this_is_flashed_in_the_future[] = {
    {.id = 2,
     .name = "/3f00/a003",
     .content =
         "0003000a000131323334ffffffff31323334353637380003000a008131323334fffff"
         "fff31323334353637380103000a000a31323334ffffffff3132333435363738"},
    {.id = 3,
```

Upon init SoftSIM checks if `MF` exist. If not the whole struct is written to NVS. 

Essentially this isn't needed for production as it can be flashed directly. It is on the TODO-list. But, it is veeeery handy for rapid prototyping. By default it isn't included in the build. When the build-system integration is more done it will be removed entirely. 

It also allows us to generate this "template profile" on a device and read out the flash content later. 

### Provisioning
`int nrf_sofsim_provision(uint8_t * profile, size_t len);` is now exposed to be used in the main application code. 

It decodes the profile and put it in the relevant spots. 

Protected storage is painfully slow when provisioning? No problem when device is provisioned already. No biggie. 

``` 
  const char *profile_read_from_external_source = k_malloc(332);
  
  read_from_prodline(profile_read_from_external_source);
  
  // full profile
      "0829430500xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx30040434724";

  printk("Provisioning SoftSIM profile.\n");
  nrf_sofsim_provision((uint8_t *)profile_read_from_external_source,
                       strlen(profile_read_from_external_source));
  k_free(...);
```

## bigger todos:
- On `NRF_SOFTSIM_DEINIT` the internal buffers should be commit to flash in case device is about to power down. 
-  On `NRF_SOFTSIM_DEINIT` the DIR entry should potentially be updated. In rare occasions  files/dirs are added/removed which should result in a change in `DIR` content
- Removing directories is a bit abstract. Probably just remove all files with matching file name as suggested in code
- Dump flash from a "provisioned" device and use the NVS chunk as the template profile. 



  
 ## quick start
 Find a nice spot for the NCS to live and initialize with west

`west init -m git@github.com:onomondo/nrf-softsim-dev.git`
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

# softsim is flashed seperately. location and size must be static and well defined
CONFIG_BUILD_WITH_TFM=y
CONFIG_TFM_PROFILE_TYPE_MEDIUM=y

CONFIG_NVS=y
CONFIG_NVS_LOG_LEVEL_DBG=y
CONFIG_PM_PARTITION_SIZE_NVS_STORAGE=0x8000 

# include softsim in build
CONFIG_SOFTSIM=y
CONFIG_SOFTSIM_AUTO_INIT=y


```
# Flashing your device:
### By command line:
flash sim profile
`nrfjprog -f NRF91 --sectorerase --log --program path/to/profile/nrf_<sim_id>.hex`
flash sample
`nrfjprog -f NRF91 --sectorerase --log --program /path/to/build/zephyr/merged.hex --reset`


### using nrf Connect programmer
Add the compiled sample (`merged.hex`). Additionally find the SIM profile and add that. 

`template.hex` resides in the upper end of the flash. Nothing stops it from being moved around though :)

I.e. `using srec_cat` or `srecord` the offset can be changed: 

`srec_cat template.hex -offset <offset here. can be negative> -output <offset_template.hex> -Intel`

![image](https://github.com/onomondo/nrf-softsim-dev/assets/46489969/8651d4e4-6a67-4bbe-983b-80d066326f55)


## samples
Yes. Should build out of the box. 







