#!/usr/bin/env python3
import sys
import os
import re

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

tag = sys.argv[1] if len(sys.argv) > 1 else 'v1.0-beta4'
notes = ''
if os.path.exists('CHANGELOG.md'):
    with open('CHANGELOG.md', 'r', encoding='utf-8') as f:
        content = f.read()
    pattern = r'## \[' + re.escape(tag) + r'\][^\n]*\n(.*?)(?=\n## \[|\Z)'
    match = re.search(pattern, content, re.DOTALL)
    if match:
        notes = match.group(1).strip()

if not notes:
    notes = f'### Release {tag}\n\n- Standalone binaries for x64 and ARM64'

with open('RELEASE_NOTES.md', 'w', encoding='utf-8') as out:
    out.write(notes)

print(f'Extracted release notes for {tag} successfully.')
