# nrf-softsim
#### Table of Contents  
##### Quickstart
1. [Configure NCS to include SoftSIM libraries in your build system](#setup)
2. [Set-up your API key to get access to SoftSIM profiles through our API](#get-access-to-your-free-softsim-profiles)
3. [Configure you project to build with SoftSIM](#configure-and-build)
4. [Configuring SoftSIM in NCS samples](#general-usage)

##### General
- [Building and running](#building-flashing-and-running)
- [SoftSIM and physical SIM selection](#software-sim-selection)
- [Understanding the SIM - why SoftSIM is possible](#understanding-the-sim---why-softsim-is-possible)
- [Details on provisioning](#provisioning)
- [Details on kConfig options](#kconfig-options)

### Setup
For existing toolchains and build systems it is sufficient to update the manifest to point to `west.yml` inside this repository:

```
cd <ncs_base>
git clone https://github.com/onomondo/nrf-softsim.git modules/lib/onomondo-softsim
west config manifest.path modles/lib/onomondo-softsim/
west update
```

First time setting it up? We recommend using the [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop) to get the build system correctly set up. Once done, configure the manifest as described above.


Bonus tip: The Toolchain Manager allows you to easily generate the correct environment variables. Click the small arrow and select `Generate environment script`. The output file contains everything you need to set up the new toolchain. 

Your folder structure should look something like:
```
ncs
 |___ .west
 |___ modules
 |___ nrf
 |___ nrfxlib
 |___ modules
     |___lib
         |___onomondo-softsim
         |___ ...
     |___ ...
 |___ zephyr
 |___ ...
```

Alternatively, a new SDK can be initiated with `west init -m https://github.com/onomondo/nrf-softsim.git`. 


### Get access to your free SoftSIM profiles
SoftSIM profiles are delivered through our API. As this can be a bit cumbersome, we've developed a small tool to make this process easier. The tool is available at [sofsim-cli](https://github.com/onomondo/onomondo-softsim-cli). Additional instructions can be found in the CLI repository. 

1. Generate an API key on [app.onomondo.com/api-keys](https://app.onomondo.com/api-keys). Follow the instructions on the app. 
2. Download the `softsim` cli tool for your platform.
3. Fetch your profiles: `./softsim fetch --api-key = <your_api_key> -n 5`. This will create a `profiles` directory for you with `5` encrypted profiles.

Every time you require a new profile, simply use the `./softsim next --key=<path to your private key>`. It will look in the `./profiles` folder and decrypt and format a profile. _This command guarantees that a new profile is given each time._


### Configure and build

There are currently two samples included to showcase how a SoftSIM can be provisioned. It is recommended to start with the *static profile* sample.

##### static profile
`samples/softsim_static_profile` will provision a profile during the first system initialization. The profile is configured in the `prj.conf` 

In the `prj.conf` you'll find the following options related to the SoftSIM statically provisioned profile. This setup is useful for development, as the profile doesn't have to be re-provisioned every time the device is flashed. 

```
CONFIG_SOFTSIM_STATIC_PROFILE_ENABLE=y
CONFIG_SOFTSIM_STATIC_PROFILE="011208091..."
```


Run `./softsim next --key=~/myPrivateKey` (with path to your private key) and grab the output. By default it formats the profile to be accepted by any nRF91 series devices. The profile will look similar to `01120...`. Replace the `CONFIG_SOFTSIM_STATIC_PROFILE` value with your SoftSIM profile.


Build and flash the sample and the device will attach and send data right away.

```
west build
west flash
```

__This is only to be used during development.__

#### externally provisioned
`samples/softsim_external_profile` will wait for a profile supplied via UART. After receiving it will be provisioned and the device will reboot to free up the UART port for AT commands. 

``` 
echo "<my_profile>" > /dev/tty.usbmodem<id>
```

Which results in:
```
*** Booting Zephyr OS build v3.2.99-ncs1 ***
[00:00:00.610,198] <inf> softsim_sample: SoftSIM sample started.
[00:00:00.610,656] <inf> softsim_sample: Waiting for profile... 0/190
[00:00:20.610,717] <inf> softsim_sample: Waiting for profile... 190/190
*** Booting Zephyr OS build v3.2.99-ncs1 ***
[00:00:00.555,664] <inf> softsim_sample: SoftSIM sample started.
[00:00:00.615,875] <inf> softsim_sample: Waiting for LTE connect event.
[00:00:00.744,140] <inf> softsim_sample: LTE cell changed: Cell ID: -1, Tracking area: -1
[00:00:01.185,760] <inf> softsim_sample: LTE cell changed: Cell ID: 13358642, Tracking area: 2000
[00:00:01.308,349] <inf> softsim_sample: RRC mode: Connected```
+CEREG: 5,"07D0","00CBD632",7,,,"00100011","11100000"
[00:00:07.096,221] <inf> softsim_sample: Network registration status: Connected - roaming
[00:00:07.096,405] <inf> softsim_sample: LTE connected!
```
### General usage

For most samples and applications, it's sufficient to build by executing the following command:
```
west build -b nrf9160dk_nrf9160_ns -- "-DOVERLAY_CONFIG=$PATH_TO_ONOMONDO_SOFTSIM/overlay-softsim.conf"
```
Where `PATH_TO_ONOMONDO_SOFTSIM` is the path of the downloaded Onomondo SoftSIM repository, for example `$HOME/ncs/nrf-softsim-dev`.


#### Note
SoftSIM is relying on some default data in the storage partition. This section of the flash can be generated and flashed manually (see steps below) or, as we recommend, automatically included by `CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX=y` 

__Manually generating SoftSIM profile template data__
1. After building the application, generate the application-specific template profile. `west build -b nrf9160dk_nrf9160_ns -t onomondo_softsim_template`
2. Flash the application-specific template profile. `west flash --hex-file build/onomondo-softsim/template.hex`

If the partition table of the application changes, for example due to another partition changing size, the template profile must be rebuilt and flashed again.
The partition table can be checked at any time with:
```
west build -t partition_manager_report
```

#### Note


Some applications will fail to link with error `zephyr/zephyr_pre0.elf uses VFP register arguments` (for example `modem_shell`). In this case it is required to also enable `CONFIG_FP_SOFTABI=y`. It is suggested to create an additional Kconfig overlay for application specific SoftSIM configurations and add them to `overlay-softsim.conf` inside
the application directory.
The application can then be built like this:
```
west build -b nrf9160dk_nrf9160_ns -- "-DOVERLAY_CONFIG=$PATH_TO_ONOMONDO_SOFTSIM/overlay-softsim.conf;overlay-softsim.conf"
```

#### Note

For very large applications, it is required to disable features in order to reduce the size of the application binary and leave space on Flash for the SIM profile. 
During the build step, a Flash overflow error will be reported if this requirement is not satisfied.
The same principle applies for RAM requirements.

#### Note

Onomondo SoftSIM uses the heap memory pool. It is expected that `CONFIG_HEAP_MEM_POOL_SIZE` is at least `30000`, so if the target application also uses the heap, please
consider adjusting this Kconfig accordingly.

#### Note

Onomondo SoftSIM cannot coexist with `CONFIG_SETTINGS` with NVS backend `CONFIG_SETTINGS_NVS`. Please consider switching instead to FCB backend by enabling `CONFIG_SETTINGS_FCB`.

### Building, flashing and running

To build sample, follow `Configure and build`. For static provisioning, inside the target application `overlay-softsim.conf` add:
```
CONFIG_SOFTSIM_STATIC_PROFILE_ENABLE=y
CONFIG_SOFTSIM_STATIC_PROFILE="123abc..."
```
This will enable provisioning at system initialization if the device has not been provisioned yet. `CONFIG_SOFTSIM_STATIC_PROFILE` is the string representing
the unique SIM profile to be provisioned.



### Software SIM selection

The Modem is runtime-configurable to use regular SIM and/or SoftSIM (iSIM). The configuration is done by the AT command `AT%CSUS=2` for Software SIM selection. The configuration can be done only when the Modem is deactivated. When reverting to physical SIM, configure with the AT command `AT%CSUS=0`. SIM selection is committed to NVM after a `AT+CFUN=0`.

When enabling SoftSIM, the Software SIM will be selected automatically upon initialization.


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

To accomodate that we've created a bootstrapping filesystem that should be flashed together with the application.

<p align="center">
 <img width="338" src="https://github.com/onomondo/nrf-softsim/assets/46489969/e77404ed-f8fd-46c8-98d8-054258727b8b">
</p>


The list of files is fairly involved - but in the end only a subset of files are ever accessed. 

Internally this is a NVS partition which is a `key-value` store type. It is pretty compact and generally sufficient. The previous request of reading the `ICCID` internally translates to something similar to:

`00a408040000022fe20168` -> `open('/3f00/2fe2)` -> `nvs_read(id=14)`

We've made a caching layer as well to avoid i) slow reads and ii) excessive writes to flash. So, the SoftSIM profile data looks something like:

<p align="center">
 <img width="358" src="https://github.com/onomondo/nrf-softsim/assets/46489969/c03113b3-f41b-41c7-b681-0e2b09f7ee7b">
</p>


The first entry is used to translate between paths (`'3f00/2fe2`) to an actual `NVS key`. It contains an ordered list of files sorted by frequency of access - i.e. the 'master file, 3f00' is in the top, since it is most frequently accessed. 


The list is read and parsed to a linked list - and this makes the base for all cached operations. The order makes the lookup very fast.
<p align="center">
 <img height="338" src="https://github.com/onomondo/nrf-softsim/assets/46489969/815529a8-caf4-485f-a752-1a6242bec082">
</p>



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

When a device finds a network it wants to attach to, something like this happens (simplified version):

<p align="center">
<img height="600" src="https://github.com/onomondo/nrf-softsim/assets/46489969/19083bea-2727-48d6-ad50-63f80384e4d8">
</p>


Step 5 is a SIM command as well `AUTHENTICATE EVEN` and it is running the `milenage algorithm` that also creates session keys, checks that the network in fact isn't an imposter, etc.  

The milenage algorithm is `AES` based and we utilize the KMU through the `psa_crypto` API to implement this. This means that the keys are _very_ protected and once written they can't ever be extracted. Instead they are used by reference in the crypto engine. 


#### Provisioning SoftSIMs 

Is done through a pretty simple interface -
`nrf_softsim_provision(uint8_t * data, size_t len)` decodes and write the profile to the appropriate places. 
The profile is fetched from Onomondo's API. The sample uses UART to transfer it. 

At any time the provisioning status can be queried with `nrf_softsim_check_provisioned()`. Handy when boothing up as the device can enter a provision mode based on this. 


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

# Enable nordic security backend and PSA APIs
# probably already the default? 
CONFIG_NRF_SECURITY=y
CONFIG_PSA_CRYPTO_DRIVER_CC3XX=y
CONFIG_PSA_WANT_ALG_CBC_NO_PADDING=y
CONFIG_PSA_WANT_ALG_CMAC=y
CONFIG_PSA_WANT_KEY_TYPE_AES=y

CONFIG_NVS=y
CONFIG_PM_PARTITION_SIZE_NVS_STORAGE=0x8000 

# include softsim in build
CONFIG_SOFTSIM=y
CONFIG_SOFTSIM_AUTO_INIT=y
CONFIG_SOFTSIM_BUNDLE_TEMPLATE_HEX=y

# Currently needed for persistent key storage
CONFIG_TFM_PROFILE_TYPE_NOT_SET=y
```



# Flashing your device:
### By command line:

`nrfjprog -f NRF91 --sectorerase --log --program /path/to/build/zephyr/merged.hex --reset`


# Some more details:
#### Heap
`mem.h`/`heap_port.c` can now be optionally used to implement custom allocators - i.e. `k_malloc()` to avoid conflict with stdc heap pools. We default to `k_malloc()/k_free()`
#### More on NVS
The main challenge here is that the NVS is a "key-value" store i.e. `UINT16 -> void *`. SoftSIM needs a `char * path -> void * data` map. 

To overcome this, the first entry (ID 1) in the NVS will store a mapping from paths to actual ID's. 

The ID is leveraged to encode additional information, i.e. if content is in protected storage instead. 

```
Dir entry:

[[ID_1, PATH_LEN, PATH], [ID_n, PATH_LEN, PATH], ...  ]

Where ID is the actual ID where DATA is stored. 

ID & 0xFF00 (upper bits) are used for flags, i.e. 

#define FS_READ_ONLY         (1UL << 8)
#define FS_COMMIT_ON_CLOSE   (1UL << 7)  // commit changes to NVS on close
```
The inital DIR is designed to prioritize most accessed files as well. Internally, files are cached and in general kept in memory. 

`FS_COMMIT_ON_CLOSE` is used for SEQ numbers that should be committed to flash directly to reduce attack vectors.


#### SoftSIM integration in application code
Either call `nrf_softsim_init()` explicitly or let the kernel do it on boot with the config option. 

SoftSIM entrypoint starts its own work queue and returns immediately after. The handler installed with `nrf_modem_softsim_req_handler_set()` will enqueue requests, as they come and the work queue will unblock and handle the request. The SoftSIM context will be blocked most of the time. The main interaction happens on boot. 


![softsim_nrf_flow](https://github.com/onomondo/nrf-softsim/assets/46489969/7513bb06-99b3-4de4-95bb-34884a9726ed)

Please note that SoftSIM internally need access to a storage partition. This should be pre-populated with the `template.hex` provided in the samples. The adress in the `template.hex` is hardcoded but can be moved around freely as pleased with an appropriate tool. The location is derived from the devicetree at compile time (` FIXED_PARTITION_DEVICE(NVS_PARTITION)`)
