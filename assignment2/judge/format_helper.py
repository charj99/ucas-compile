import os
import sys


origin_str = sys.argv[1]
origin_str.strip(' ')
lines = origin_str.splitlines()
output = ""
for line in lines:
    start = line.find(':')
    if start == -1:
        continue
    line_num = line[:start+1]
    words = line[start+1:].split(',')
    words = [word.strip() for word in words]
    words.sort()
    line = line_num + ",".join(words)
    # line = line[:-1]
    output += line + "\n"
print(output,end='')