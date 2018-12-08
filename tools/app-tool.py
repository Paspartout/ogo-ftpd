#!/usr/bin/env python3

import io
import json
import os
from PIL import Image
import struct
import sys

"""
struct app_header_t {
    uint32_t magic;
    uint32_t header_version;
    uint32_t header_len;
    uint32_t json_len;
    uint32_t icon_len;
    uint32_t binary_len;
};
"""

def iconify(filename):
    data = b''
    im = Image.open(filename)
    im.thumbnail((48, 48), Image.ANTIALIAS)
    im = im.convert("RGB")
    for r8, g8, b8 in im.getdata():
        r5, g6, b5 = r8 >> 3, g8 >> 2, b8 >> 3
        pixel = r5 << 11 | g6 << 5 | b5
        data += struct.pack('<H', pixel)
    return data

HEADER_FORMAT = '<LLLLLL'
HEADER_MAGIC = 0x21505041
HEADER_VERSION = 1

app_filename = sys.argv[1]
json_filename = sys.argv[2]
icon_directory = sys.argv[4]
binary_filename = sys.argv[3]

with open(json_filename, 'r') as in_f:
    obj = json.loads(in_f.read())
    json_data = json.dumps(obj, separators=(',', ':')).encode('ascii')

icon_data = b''
i = 0
while True:
    filename = os.path.join(icon_directory, 'icon_%d.png' % (i,))
    if not os.path.exists(filename):
        break
    icon_data += iconify(filename)
    i += 1

with open(binary_filename, 'rb') as in_f:
    binary_data = in_f.read()

header_len = struct.calcsize(HEADER_FORMAT)
header = struct.pack(HEADER_FORMAT, HEADER_MAGIC, HEADER_VERSION, header_len,
                     len(json_data), len(icon_data), len(binary_data))

f = open(app_filename, 'wb')
f.write(header)
f.write(json_data)
f.write(icon_data)
f.write(binary_data)
f.close()
