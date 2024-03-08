import os
import re

HEADER  = """
#include <stdint.h>
typedef struct {const char *name; const uint8_t *data; const uint32_t size;} file_t;
typedef struct {const char *name;} dir_t;
"""


def to_c_array(path='files', out_path='ss_static_files.c'):
    # walk the files directory and get a list of all files and subdirectories
    s = HEADER


    files, directories = get_dirs_and_files(path)    

    s += 'const dir_t dirs[] = {\n'  
    ds = []
    for d in directories:
        if d == '':
            continue    
        ds.append( f'{{.name = "{d}"}}')
    s += ', \n'.join(ds) + '\n};\n' 
      

    s += 'const file_t files[] = {\n'
    fs = []
    for f in files:
        with open(path + f, 'r') as file:
            data = file.read()
            data_len = len(data)/2
            # get it in chunks of 2
            data = re.findall('..', data)
            data = ','.join([f'0x{byte}' for byte in data])
            fs.append(f'{{.name = "{f}", .data = {{{data}}}, .size = {int(data_len)}}}')
    s += ', \n'.join(fs) + '\n};\n'
    
    s += f'const uint32_t num_files = {len(files)};\n'
    s += f'const uint32_t num_dirs = {len(directories)};\n'

    with open(out_path, 'w') as file: 
        file.write(s)

def get_dirs_and_files(path):
    files = []
    directories = []

    for root, d_names, f_names in os.walk(path):
        current_dir = root.split('files')[-1]
        directories.append(current_dir)
        for file in f_names:
            files.append(current_dir+'/'+file)
    return files, directories
to_c_array('../files')
    
