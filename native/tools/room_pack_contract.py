"""Room-pack admission shared by builders, revisions and APK staging.

The native transition consumer requires every room-scoped file before retiring
the current map. Read the same declaration rather than maintaining another list.
This validates pack completeness/identity, not native payload semantics.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGET = 'd98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'


def required_room_files():
    names = set()
    for line in (ROOT / 'native/app/room_asset_names.inc').read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        match = re.fullmatch(r'HADES_ROOM_ASSET\("([A-Za-z0-9_.-]+)"\)', line)
        if not match or match[1] in names:
            raise ValueError('Invalid shared room asset declaration: ' + line)
        names.add(match[1])
    if not names:
        raise ValueError('Empty shared room asset declaration')
    return frozenset(names)


REQUIRED_ROOM_FILES = required_room_files()


def validate_room_name(room):
    # Matches validMapName in room_assets.hpp, including all-underscore names.
    if not isinstance(room, str) or not re.fullmatch(r'[A-Za-z0-9_]{1,64}', room):
        raise ValueError('Invalid original room name')
    return room


def validate_room_manifest(manifest):
    if manifest.get('schema') != 'hades-room-pack-v1' or manifest.get('target_sha256') != TARGET:
        raise ValueError('Room pack identity mismatch')
    room = validate_room_name(manifest.get('room'))
    # RoomOpening is served at the root, never through rooms/RoomOpening/.
    if room == 'RoomOpening':
        raise ValueError('Boot map must be staged at the asset root')
    files = manifest.get('files')
    if not isinstance(files, dict):
        raise ValueError('Room pack files must be a mapping')
    missing = REQUIRED_ROOM_FILES - files.keys()
    unexpected = files.keys() - REQUIRED_ROOM_FILES
    if missing or unexpected:
        raise ValueError(f'Room pack file closure mismatch: missing={sorted(missing)} unexpected={sorted(unexpected)}')
    for name, row in files.items():
        if not isinstance(row, dict) or not isinstance(row.get('path'), str) or not row['path']:
            raise ValueError('Room asset source path missing: ' + name)
        size = row.get('bytes')
        if type(size) is not int or size <= 0 or not isinstance(row.get('sha256'), str) or not re.fullmatch(r'[0-9a-f]{64}', row['sha256']):
            raise ValueError('Room asset identity malformed: ' + name)
    return room
