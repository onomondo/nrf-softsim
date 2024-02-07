First steps to enabling support for more flash backends.  

A general rewrite of the fs module is in progress and is expected to make the backend change easier in the future. 

## Generate a littlefs flash dump

```bash 
cd scripts
pip install littlefs-python 
python generate_littlefs_partition.py
```

There is a parameter to set the number of blocks. In general the block sixe is 4k on the nrf9160

The `files` directory contains the default content in the SoftSIM file system. For NVS backends this encoded in `template.bin`. 


