# nrf-softsim


## TL;DR 
Find a nice spot for the NCS to live and initialize with west

`west init -m git@github.com:onomondo/nrf-softsim.git`

`west update`
This will fetch `onomondo-softsim` as a module. 

You might need to follow some of these steps if it's the first time working with NCS: https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/gs_installing.html


`cd modules/lib/onomondo-softsim/samples/softsim && west build` to get the sample up and running. 

SoftSIM relies on some default config - basically a template profile that needs to be customized in order to attach. With default parameters it has a static address. To flash this seperately do:

`nrfjprog -f NRF91 --sectorerase --log --program path/to/profile/template.hex`

Using the `nRF Connect for Desktop` you can write both things in one go as well. Just add the template and you application and press `erease and write`.

Or merge it into your `merged.hex` and flash everything in one go.



## Understanding the SIM - why SoftSIM is possible.

To be as brief as possible - a SIM is nothing more than a fancy filesystem with the ability to calculate an authentication response to a given authentication challenge. More about that later. 


### Filesystem operations
For details - refer to https://www.etsi.org/deliver/etsi_ts/102200_102299/102221/18.00.00_60/ts_102221v180000p.pdf

The majority of commands that the SIM understands are related to the underlying filesystem. These include, but not limitied to,  `SELECT`, `READ BINARY`, `UPDATE BINARY`, `READ RECORD`, `UPDATE RECORD`, `SEARCH RECORD`. You get the idea. Something about selecting files and either reading them or updating them. 

Not all files are free to update. For instance the `IMSI` can only be changed by the operator with the correct PINs - so a SIM also manages access rights. Some rights are unlocked with the PIN - for that `VERIFY PIN` command is issued. 

#### The nrf9160 SIM init sequence 
What happens when you 'activate' the SIM on your device (`AT+CFUN=41`)? The first many commands follows the same pattern - `00a4....` which is the `SELECT` command followed by `00b0....` which is the `READ BINARY` command.

- `80f20000000168` - `STATUS`
- `00a408040000022fe20168` - `SELECT` '2fe2' (EF_ICCID)
- `00b000000a` - `READ BINARY` - Get the actual content of selected fine
- `00a40804000002` 2f000168 - `SELECT` '2f00' (EF_DIR)
- `00b2010426` - `READ RECORD`
- `00a408040000022f050168` - `SELECT` '2f05' (EF_PL) which encodes the Preferred Language
- `00b000000a` - - `READ BINARY` 
- `00a408040000022f080168` - `SELECT` '2f08' (EF_UMPC) (UICC Maximum Power Consumption)  

And the list goes on... 

 


### The template SIM profile

The main point here is that EF_DIR, EF_PL, EF_UMPC etc are the same for all SIMs. Only the ICCID and IMSI is different across SIMs when they are fresh out of the factory.

To accomodate that we've created a bootstrapping filesystem that should be flashed together with the application. <img width="338" src="https://github.com/onomondo/nrf-softsim/assets/46489969/4de502a0-a2c9-4759-b2ed-74ae0adaef25">


The list of files is fairly involved - but in the end only a subset of files are ever accessed. 

Internally this is a NVS partition which is a `key-value` store type. It is pretty compact and _good enough_ for what we need it fore. So the previous request of reading the `ICCID` internally translates to something like:

`00a408040000022fe20168` -> `open('/3f00/2fe2)` -> `nvs_read(id=14)`

We've made a caching layer as well to avoid i) slow reads ii) excessive writes to flash. So actually the SoftSIM profile data looks something like this
 <img width="358" src="https://github.com/onomondo/nrf-softsim/assets/46489969/e778a7ea-4d5e-4ed0-aff0-f162f9894dbe">


The first entry is used to translate between paths (`'3f00/2fe2`) to an actual `NVS key`. It contains an ordered list of files sorted by frequency of access - i.e. the 'master file, 3f00' is in the top since it is most frequenctly accessed. 
<img width="280" alt="image-3" src="https://github.com/onomondo/nrf-softsim/assets/46489969/89515114-c3e7-4d2f-ae4f-6797c6411a1f">


List is read and parsed to a linked list - and this makes the base for all cached operations. The order makes the lookup very fast in most cases.

 <img height="338" src="https://github.com/onomondo/nrf-softsim/assets/46489969/b1fef4e6-623e-4df5-9fa5-db645a23dd8c">



## Provisioning
And that leads us to provisioning of SIMs. 

The `IMSI/ICCID` should't be a surprise at this point:

```c
#define IMSI_PATH "/3f00/7ff0/6f07"
#define ICCID_PATH "/3f00/2fe2"

int write_profile(...){

  ...

  // find the NVS key based on the cache
  struct cache_entry *entry = (struct cache_entry * f_cache_find_by_name(IMSI_PATH, &fs_cache);

  // commit directly to NVS
  nvs_write(&fs, entry->key, profile->IMSI, IMSI_LEN)

  ... // repeat for ICCID and support files

}
```

Alright, that was the easy part. In reality something else happens _just_ before:

```c
struct ss_profile profile = {0};
decode_profile(len, profile_r, &profile);

// import to psa_crypto
ss_utils_setup_key(KMU_KEY_SIZE, profile.K, KEY_ID_KI);
ss_utils_setup_key(KMU_KEY_SIZE, profile.KIC, KEY_ID_KIC);
ss_utils_setup_key(KMU_KEY_SIZE, profile.KID, KEY_ID_KID);
```
3 keys are written to the KMU. These are related to the authentication and remote SIM OTA commands. 

When a device finds a network it want to attach to something like this happens:

<img height="600" src="https://github.com/onomondo/nrf-softsim/assets/46489969/54703e7d-6cca-402f-a747-cc2b3a2455b7">




That is greatly simplified. Step 5 is a SIM command as well `AUTHENTICATE EVEN` and it is running the `milenage algorithm` that also creates session keys etc (and checks that the network in fact isn't an imposter). 

The milenage algorithm is `AES` based and we utilize the KMU through the `psa_crypto` API to implement this. This means that the keys are _very_ protected and once written they can't ever be extracted. Instead they are used by refernce in the crypto engine. 


#### Provisioning SoftSIMs 

Is done through a pretty simple interface -
`nrf_sofsim_provision(uint8_t * data, size_t len)` decodes and write the profile to the appropriate places. 
The profile is fetched from Onomondo's API. The sample uses UART to transfer it. 

At any time the provisioning status can be queried with `nrf_sofsim_check_provisioned()`. Handy when boothing up as the device can enter a provision mode based on this. 


#### Quick recap
```c
struct profile 
{
  u8[] Ki,
  u8[] KIC,
  u8[] KID,
  u8[] IMSI,
  u8[] ICCID,
  u8[] SMSP // SMS related
}
```

The profile encoding is a `TLV` like structure (Tag Length Value) to make it a bit more flexible. Each field in the profile has a tag assigned e.g. `IMSI_TAG=(0x01)`. 

The profile is contructed by encoding `TAG|LEN|DATA[LEN]` for each field and concatinating multple TLV fields:

Encoding the IMSI is done by: `TAG: 1, LEN: 0x12`:

IMSI_TLV: `0112082943051220434955`

Where the first 4 bytes are recognized as `01(TAG) 12(LEN)` and indeed 18 bytes follow.


## kConfig options

Isn't completely finalized yet. The following fields should either be `y` selected by softsim or the application developer:
- Flash access
- TFM for `psa_crypto`
- NVS for profile

`CONFIG_SOFTSIM` includes softsim in the build system
`CONFIG_SOFTSIM_AUTO_INIT` starts the softsim task automatically. This can be omitted and done expicitly in the user application. 

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
1 - flash sim profile

`nrfjprog -f NRF91 --sectorerase --log --program path/to/profile/template.he`

2 - flash application

`nrfjprog -f NRF91 --sectorerase --log --program /path/to/build/zephyr/merged.hex --reset`


### using nrf Connect programmer
Add the compiled sample (`merged.hex`). 

Drag the `template.hex` into the view. `Erase and write`. SoftSIM sample is ready to provision






# Some more details:
#### Heap
`mem.h`/`heap_port.c` can now optionally be used to implement custom allocators - i.e. `k_malloc()` to avoid conflict with stdc heap pools. We default to `k_malloc()/k_free()`
#### More on NVS
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
```
The inital DIR is designed to prioritize most accessed files as well. Internally files are cached and in general kept in memory. 

`FS_COMMIT_ON_CLOSE` is used for SEQ numbers that should be committed to flash directly to reduce attack vectors.


#### SoftSIM integration in application code
Either call `nrf_softsim_init()` explicitly or let the kernel do it on boot with the config option. 

SoftSIM entrypoint starts its own workqueue and returns immidiately after. The handler installed with `nrf_modem_softsim_req_handler_set()` will enqueue request as they come and the workqueue will unblock and handle the request. The softsim context will be blocked most of the time. The main interaction happens on boot. 

![softsim_nrf_flow](https://github.com/onomondo/nrf-softsim/assets/46489969/7513bb06-99b3-4de4-95bb-34884a9726ed)

Please note that SoftSIM internally need access to a storage partition. This should be pre-populated with the `template.hex` provided in the samples. The adress in the `template.hex` is hardcoded but can freely be moved around as pleased with an appropriate tool. The location is derived from the devicetree at compile time (` FIXED_PARTITION_DEVICE(NVS_PARTITION)`)
