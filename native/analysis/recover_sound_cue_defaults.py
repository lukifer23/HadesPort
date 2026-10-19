#!/usr/bin/env python3
"""Recover semantic SoundCue defaults from exact WeaponData constant stores.

No original code execution. Unwritten empty-functor/padding bytes remain unknown.
This supplements, and does not overwrite, the scalar weapon-defaults report.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re

ROOT = Path(__file__).resolve().parents[2]
REPORTS = ROOT / '.local/reports'
EXPECTED = 'd98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=REPORTS / 'native-soundcue-compiled-defaults.json')
    args = parser.parse_args()
    dll = ROOT / '.local/game/base/x64/EngineWin64s.dll'
    assert sha(dll) == EXPECTED
    ctor_path = REPORTS / 'native-weapon-data-ctor.json'
    pdb_path = REPORTS / 'native-engine-pdb-weapon-defaults-next.json'
    ctor = json.loads(ctor_path.read_text())
    pdb = json.loads(pdb_path.read_text())
    assert ctor['dll_sha256'] == EXPECTED
    by = {value['index']: value for value in pdb['selected_types']}
    klass = next(value for value in by.values() if value.get('name') == 'sgg::WeaponDataDef' and 'fields: Some' in value['debug'])
    fields = int(re.search(r'fields: Some\(TypeIndex\((0x[\da-f]+)\)', klass['debug'])[1], 16)
    cue = next(value for value in by.values() if value.get('name') == 'sgg::SoundCue' and 'size: 40,' in value['debug'])
    cue_fields = int(re.search(r'fields: Some\(TypeIndex\((0x[\da-f]+)\)', cue['debug'])[1], 16)
    assert all(token in by[cue_fields]['debug'] for token in ['offset: 0, name: RawString("pOwner")', 'offset: 8, name: RawString("mName")', 'offset: 33, name: RawString("mValid")'])
    members = []
    for match in re.finditer(r'Member\(MemberType \{ attributes: FieldAttributes\(\d+\), field_type: TypeIndex\((0x[\da-f]+)\), offset: (\d+), name: RawString\("([^\"]+)"\)', by[fields]['debug']):
        if match[1] == '0x56a1':
            members.append({'member': match[3], 'name': match[3][1:], 'offset': int(match[2])})
    assert len(members) == 6
    values = [None] * 0x960
    origins = [None] * len(values)
    zero = {'%rax': None, '%r14': None, '%xmm0': None}
    canonical = {'%al': '%rax', '%eax': '%rax', '%r14b': '%r14', '%r14d': '%r14'}
    for line in ctor['assembly'].splitlines():
        match = re.match(r'^([\da-f]+):.*?\t([a-z][a-z0-9]*)\s*(.*?)\s*(?:#.*)?$', line)
        if not match:
            continue
        address, op, operands = int(match[1], 16), match[2], match[3].split('#')[0].strip()
        if not 0x1800f7598 <= address < 0x1800f7e76:
            continue
        parts = operands.split(',')
        if len(parts) == 2:
            source, destination = [part.strip() for part in parts]
            register = canonical.get(destination, destination)
            if register in zero:
                zero[register] = address if op in ('xorl', 'xorps') and source == destination else None
        store = re.match(r'(.+), (-?0x[\da-f]+)?\(%(rsp|rbp)\)$', operands)
        if store and op in ['movq', 'movl', 'movw', 'movb', 'movdqu']:
            width = {'movq': 8, 'movl': 4, 'movw': 2, 'movb': 1, 'movdqu': 16}[op]
            operand, displacement, base = store.groups()
            offset = int(displacement or '0', 0) + (0x100 if base == 'rbp' else 0) - 0x20
            provenance = {'store_va': hex(address), 'instruction': line}
            number = None
            if operand.startswith('$'):
                number = int(operand[1:], 0)
            elif zero.get(canonical.get(operand, operand)) is not None:
                number = 0
                provenance['constant_definition_va'] = hex(zero[canonical.get(operand, operand)])
            if number is not None and 0 <= offset <= len(values) - width:
                data = (number % (1 << (8 * width))).to_bytes(width, 'little')
                values[offset:offset + width] = data
                origins[offset:offset + width] = [provenance] * width
        if op == 'callq':
            zero['%rax'] = zero['%xmm0'] = None
    result = []
    for member in members:
        offset = member['offset']
        assert values[offset:offset + 8] == [0] * 8
        assert values[offset + 8:offset + 32] == [0] * 23 + [23]
        assert values[offset + 33] == 0
        selected = list(range(offset, offset + 32)) + [offset + 33]
        stores = {json.dumps(origins[index], sort_keys=True) for index in selected}
        result.append({**member, 'type': 'sound_cue', 'value': {'owner': None, 'name': '', 'valid': False},
                       'native_type': 'sgg::SoundCue', 'native_storage_size': 40,
                       'unproven_storage_byte_offsets': [index for index in range(40) if values[offset + index] is None],
                       'provenance': {'kind': 'compiled_native_default', 'constructor_rva': '0xf7530',
                                      'temporary_base': 'post-prologue RSP+0x20; RBP=RSP+0x100',
                                      'assignment_call_va': '0x1800f7e76',
                                      'stores': [json.loads(value) for value in sorted(stores)]}})
    report = {'schema': 'hades-soundcue-compiled-defaults-v1', 'dll_sha256': EXPECTED,
              'pdb_identity': pdb['identity'], 'source_reports': {str(path.relative_to(ROOT)): sha(path) for path in [ctor_path, pdb_path]},
              'fields': result,
              'limits': ['Compiled defaults before source deserialization/inheritance overrides.',
                         'Copy ctor0x43450 is not default initialization; actual inline WeaponData stores establish these values.',
                         'No 40-byte zero-filled cue is fabricated: mHash functor byte and padding remain unproven.',
                         'Named cue validity requires actual native SoundCueManager resolution; audio was not executed.']}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'report': str(args.output), 'semantic_sound_cue_defaults': len(result)}))


if __name__ == '__main__':
    main()
