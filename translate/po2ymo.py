#!/usr/bin/env python3
import sys
import os
import ast

FNV1_32_INIT = 0x811c9dc5
FNV_32_PRIME = 0x01000193

def fnv1a_32(data, hval=FNV1_32_INIT):
    for byte in data:
        hval ^= byte
        hval = (hval * FNV_32_PRIME) & 0xffffffff
    return hval

def unescape_po_string(s):
    s = s.strip()
    if s.startswith('"') and s.endswith('"'):
        try:
            return ast.literal_eval(s)
        except Exception:
            content = s[1:-1]
            return content.replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"').replace('\\\\', '\\')
    return s

def parse_po(file_path):
    with open(file_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    entries = []
    current_entry = {'msgctxt': None, 'msgid': None, 'msgstr': None, 'fuzzy': False}
    state = None

    def finish_entry():
        nonlocal current_entry
        if current_entry['msgid'] is not None and current_entry['msgstr'] is not None:
            if current_entry['msgid'] != '':
                entries.append(current_entry)
        current_entry = {'msgctxt': None, 'msgid': None, 'msgstr': None, 'fuzzy': False}

    for line in lines:
        line = line.strip()
        if not line:
            finish_entry()
            state = None
            continue
        if line.startswith('#'):
            if 'fuzzy' in line:
                current_entry['fuzzy'] = True
            continue

        if line.startswith('msgctxt '):
            finish_entry()
            current_entry['msgctxt'] = unescape_po_string(line[8:])
            state = 'msgctxt'
        elif line.startswith('msgid '):
            if state != 'msgctxt':
                finish_entry()
            current_entry['msgid'] = unescape_po_string(line[6:])
            state = 'msgid'
        elif line.startswith('msgstr '):
            current_entry['msgstr'] = unescape_po_string(line[7:])
            state = 'msgstr'
        elif line.startswith('"') and line.endswith('"'):
            val = unescape_po_string(line)
            if state == 'msgctxt':
                current_entry['msgctxt'] = (current_entry['msgctxt'] or '') + val
            elif state == 'msgid':
                current_entry['msgid'] = (current_entry['msgid'] or '') + val
            elif state == 'msgstr':
                current_entry['msgstr'] = (current_entry['msgstr'] or '') + val

    finish_entry()
    return entries

def po2ymo(infile_path, outfile_path, includefuzzy=False, encoding='utf-16le'):
    entries = parse_po(infile_path)
    units = {}
    for entry in entries:
        if entry['fuzzy'] and not includefuzzy:
            continue
        if not entry['msgstr']:
            continue
        source = entry['msgid']
        context = entry['msgctxt']
        if context:
            source = context + '\004' + source
        
        h = fnv1a_32(source.encode(encoding))
        target_bytes = entry['msgstr'].encode(encoding) + b'\x00\x00'
        units[h] = target_bytes

    byteorder = 'little'
    os.makedirs(os.path.dirname(os.path.abspath(outfile_path)), exist_ok=True)
    with open(outfile_path, 'wb') as outfile:
        outfile.write(len(units).to_bytes(2, byteorder))

        offset = 2 + len(units) * (4 + 2)
        for h, data in units.items():
            outfile.write(h.to_bytes(4, byteorder))
            outfile.write(offset.to_bytes(2, byteorder))
            offset += len(data)

        for data in units.values():
            outfile.write(data)

if __name__ == '__main__':
    if len(sys.argv) == 3:
        po2ymo(sys.argv[1], sys.argv[2])
    else:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        source_dir = os.path.join(script_dir, 'source')
        gen_dir = os.path.join(script_dir, 'generated')
        os.makedirs(gen_dir, exist_ok=True)
        
        zh_cn_po = os.path.join(source_dir, 'zh_CN.po')
        zh_cn_ymo = os.path.join(gen_dir, 'zh_CN.ymo')
        if os.path.exists(zh_cn_po):
            po2ymo(zh_cn_po, zh_cn_ymo)
            print(f"Generated {zh_cn_ymo}")
            
        zh_tw_po = os.path.join(source_dir, 'zh_TW.po')
        zh_tw_ymo = os.path.join(gen_dir, 'zh_TW.ymo')
        if os.path.exists(zh_tw_po):
            po2ymo(zh_tw_po, zh_tw_ymo)
            print(f"Generated {zh_tw_ymo}")
            
        rc_content = '''#include "../../targetver.h"
#include "windows.h"

LANGUAGE LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED
1 YMO "zh_CN.ymo"

LANGUAGE LANG_CHINESE, SUBLANG_CHINESE_TRADITIONAL
1 YMO "zh_TW.ymo"
'''
        rc_path = os.path.join(gen_dir, 'translate.rc')
        with open(rc_path, 'w', encoding='utf-16') as f:
            f.write(rc_content)
        print(f"Generated {rc_path}")
