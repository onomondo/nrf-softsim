from littlefs import LittleFS
import os

def generate_img(in_path='', out_path='', blocks = 15):

    if not os.path.exists(in_path):
        raise Exception('input path does not exist')

    if not os.path.exists(os.path.dirname(out_path)):
        raise Exception('output path does not exist')

    # Initialize the File System according to your specifications
    fs = LittleFS(block_size=4096, block_count=blocks)

    for root, d_names, f_names in os.walk(in_path):
        current_dir = root.split('files')[-1]

        for directory in d_names:
            fs.mkdir('/'+current_dir+'/'+directory)

        for file in f_names:
            in_file = open(in_path + current_dir + '/' + file, "r")

            data_hex = in_file.read()  
            data_bytes = bytearray.fromhex(data_hex)
            in_file.close()
            with fs.open(current_dir+'/'+file, 'wb') as fh:
                fh.write(data_bytes)


    # Dump the filesystem content to a file
    with open(out_path, 'wb') as fh:
        fh.write(fs.context.buffer)


generate_img(in_path='../files', out_path='./FlashMemory.bin', blocks=15)
    # srec_cat FlashMemory.bin -binary -offset  0xe8000 -output sim_profile_s.hex -Intel

