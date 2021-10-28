import os 
import re
from os import path

TEST_DIR = "./assign2-tests"
OUTPUT_DIR = "./ground-truth-1"

file_list = os.listdir(TEST_DIR)
for file_name in file_list:
    f = open(path.join(TEST_DIR, file_name))
    outputs = []
    for line in f.readlines():
        if line.startswith("///") or line.startswith("//"):
            start_index = re.search(r'\d', line).start()
            print(start_index)
            line = line[start_index:]
            outputs.append(line)
    f.close()
    f = open(path.join(OUTPUT_DIR, file_name[0:-2]+".txt"), "w")
    f.writelines(outputs)