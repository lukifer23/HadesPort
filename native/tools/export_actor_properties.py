#!/usr/bin/env python3
"""Export hash-bound native primary flags, retaining omitted-source defaults."""
import argparse, hashlib, json, math, struct
from pathlib import Path

DLL_SHA = 'd98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
ROOT = Path(__file__).resolve().parents[2]

def identity(path):
    return {'path': str(path), 'bytes': path.stat().st_size,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}

def export(bank, output, version=1):
    if output.exists():
        raise ValueError('Fresh output directory required')
    reports = ROOT / '.local/reports'
    ctor = json.loads((reports / 'native-active-animation-data-ctor-static.json').read_text())
    fill = json.loads((reports / 'native-primary-slide-fill-default-static.json').read_text())
    assign = json.loads((reports / 'native-primary-slide-assign-static.json').read_text())
    if any(x['dll_sha256'] != DLL_SHA for x in [ctor, fill, assign]):
        raise ValueError('Mismatched native evidence')
    # AnimationData::AnimationData clears a 0x2e0-byte template, copies it into
    # mDef at +0x58, then applies explicit defaults. Neither +0x8e nor +0xb0
    # receives a later override. SlideData's zero template +0x30 is copied by
    # SlideDataDef::operator=, independently of the named copy constructor.
    if not all(s in ctor['assembly'] for s in ['180093d85:', '1800940b5:']):
        raise ValueError('Missing primary default initialization evidence')
    if not all(s in fill['assembly'] for s in ['18009e492:', '18009e501:']):
        raise ValueError('Missing SlideData zero template evidence')
    if not all(s in assign['assembly'] for s in ['1800e930a:', '1800e930e:']):
        raise ValueError('Missing SlideData Airborne assignment evidence')
    fields = (reports / 'native-fields-AnimationDataDef.txt').read_text()
    if not all(s in fields for s in ['offset: 54, name: RawString("mOwnerImmobile")',
                                    'offset: 88, name: RawString("mBlockNewOwnerAnims")',
                                    'offset: 592, name: RawString("mAirborneZ")']):
        raise ValueError('Native primary property layout mismatch')
    if version>=3 and not all(s in fields for s in ['offset: 50, name: RawString("mOwnerInvulnerable")','offset: 51, name: RawString("mOwnerNoArmorCoverage")','offset: 52, name: RawString("mOwnerNoInvulnerableCoverage")']):
        raise ValueError('Native primary combat-property layout mismatch')
    if version>=4 and not all(t in fields for t in ['offset: 46, name: RawString("mDisableOwnerInteract")','offset: 47, name: RawString("mDisableOwnerManualInteract")']):
        raise ValueError('Native primary interaction-property layout mismatch')
    if version>=4:
        read=json.loads((reports/'native-action-animation-deserialize-static.json').read_text())
        if read['dll_sha256']!=DLL_SHA or not all(t in read['assembly'] for t in ['180092f96:','0x86(%r14)','180092fbf:','0x87(%r14)']):
            raise ValueError('Native primary interaction deserialize mismatch')
    if version>=5 and not all(t in ctor['assembly'] for t in ['1800941ff:', '0x101, 0x5d(%rdi)']):
        raise ValueError('Missing native UseAttachedFlasher true initializer')
    manifest = json.loads((bank / 'manifest.json').read_text())
    packet = bank / 'actor-animations.bin'
    digest = hashlib.sha256(packet.read_bytes()).digest()
    data = bytearray(('HAP'+str(version)).encode() + digest + struct.pack('<I', len(manifest['clips'])))
    records = []
    for clip in manifest['clips']:
        definition = clip['definition']
        props = {}
        values = 0
        for bit, key in ([(1, 'OwnerImmobile'), (2, 'BlockNewOwnerAnims')]+([(8,'OwnerInvulnerable'),(16,'OwnerNoArmorCoverage'),(32,'OwnerNoInvulnerableCoverage')]if version>=3 else [])+([(64,'DisableOwnerInteract'),(128,'DisableOwnerManualInteract')]if version>=4 else [])+([(256,'UseAttachedFlasher')]if version>=5 else [])+([(512,'UseAttachedShake')]if version>=6 else [])):
            value = definition.get(key, key in ('UseAttachedFlasher','UseAttachedShake'))
            if type(value) is not bool:
                raise ValueError(f'Untyped {key}')
            values |= bit if value else 0
            props[key] = {'value': value, 'provenance': 'resolved-source' if key in definition else 'native-compiled-default'}
        book = bool(clip['flags'] & 512)
        sequence = clip['source_sequence']
        data += struct.pack('<III', 1023 if version>=6 else(511 if version>=5 else(255 if version>=4 else(63 if version>=3 else(7 if version==2 else 3)))), values, len(sequence))
        if version>=2:
            # Later explicit constructor override after the zero-template copy.
            if '180094273:' not in ctor['assembly'] or '0x42c80000, 0x2a8(%rdi)' not in ctor['assembly']:
                raise ValueError('Missing explicit native AirborneZ100 initializer')
            value=definition.get('AirborneZ',100.)
            if type(value) not in (int,float) or not math.isfinite(value) or abs(value)>100000:
                raise ValueError('Untyped/out-of-bounds AirborneZ')
            data += struct.pack('<f',value)
            props['AirborneZ']={'value':value,'provenance':'resolved-source' if 'AirborneZ' in definition else 'native-compiled-default','def_offset':592,'full_data_offset':680}
        if version>=6:
            # Native fullData+0x5f true at180094205; LocationFromOwner+0xc2
            # explicitly0 at1800941aa; ScaleVectorFromOwner+0xbf zeroed by
            # wordstore1800940fa. Preserve actual owner enums, not aliases.
            if not all(t in ctor['assembly'] for t in ['180094205:', '1800941aa:', '1800940fa:']):
                raise ValueError('Missing native owner shake/location/scale vector defaults')
            for key in ['LocationFromOwner','ScaleVectorFromOwner']:
                value=definition.get(key,'Maintain')
                value={'Maintain':0,'Take':1,'Ignore':2}.get(value,value)
                if type(value) is not int or value not in (0,1,2):
                    raise ValueError('Invalid native owner enum: '+key)
                data += struct.pack('<I',value)
                props[key]={'value':value,'provenance':'resolved-source' if key in definition else 'native-compiled-default'}
        slides = []
        for step in sequence:
            frame = step['original_frame']
            if book:
                value, provenance = False, 'Unit::IsAirborne-non-Slide-branch'
            else:
                source_slides = definition['Slides']
                source = source_slides[frame - 1]
                value = source.get('Airborne', False)
                if type(value) is not bool:
                    raise ValueError('Untyped SlideData.Airborne')
                provenance = 'resolved-source-Slides' if 'Airborne' in source else 'native-SlideData-compiled-default'
            data += struct.pack('<II', frame, 2 if value else 1)
            slides.append({'original_frame': frame, 'airborne': value, 'provenance': provenance})
        records.append({'name': clip['name'], 'properties': props, 'slides': slides,
                        'source_type': definition.get('Type'), 'source_raw': clip['source_raw']})
    output.mkdir(parents=True)
    target = output / 'actor-properties.bin'
    target.write_bytes(data)
    report = {'schema': 'hades-primary-animation-properties-v'+str(version), 'packet': identity(target),
              'bank': identity(packet), 'source_manifest': identity(bank / 'manifest.json'),
              'dll_sha256': DLL_SHA, 'records': records,
              'format': 'HAP'+str(version)+' + raw32 HAA SHA + u32 clipCount; per clip u32 knownMask/valueMask/slideCount; '+('float AirborneZ;' if version>=2 else '')+' per slide u32 originalFrame1/u32 airborne(0 unknown,1 false,2 true). Property mask OwnerImmobile=1,BlockNewOwnerAnims=2'+(',OwnerInvulnerable=8,OwnerNoArmorCoverage=16,OwnerNoInvulnerableCoverage=32'if version>=3 else '')+(',DisableOwnerInteract=64,DisableOwnerManualInteract=128'if version>=4 else '')+(',UseAttachedFlasher=256' if version>=5 else '')+(',UseAttachedShake=512; HAP6 stores u32 LocationFromOwner then u32 ScaleVectorFromOwner after AirborneZ' if version>=6 else '')+(',AirborneZ-known=4.' if version>=2 else '.'),
              'evidence': [identity(reports / n) for n in [
                  'native-active-animation-data-ctor-static.json', 'native-fields-AnimationDataDef.txt',
                  'native-primary-slide-fields.json', 'native-primary-slide-fill-default-static.json',
                  'native-primary-slide-assign-static.json', 'native-active-unit-airborne-static.json','native-action-animation-deserialize-static.json']],
              'native_defaults': {'flasher':'Def+6 / fullData+0x5e initialized true by word0x0101 atfull+5d VA1800941ff. Native GetDrawFlasher293fab..293fd6 gates owner component inheritance on this field.', 'interact':'Def+0x2e/2f(full+0x86/87) zero template copied1800940b5; postcopy writes inspected no overlap. Native Read DisableOwnerInteract comparison180092f83 -> bool destination86 at180092f96; ManualInteract comparison180092fac -> destination87 at180092fbf.', 'combat': 'Def+0x32/33/34 (fullData+0x8a/8b/8c) zero-template copied at1800940b5; all post-copy writes inspected, none overlap. Exact native deserialize keys OwnerInvulnerable/OwnerNoArmorCoverage/OwnerNoInvulnerableCoverage retained.', 'primary': 'AnimationData ctor 180093d7a..180093d85 zeros template rsp+0x38 for 0x2e0 bytes; 180093eae selects template, 180093eba selects mDef+0x58; operator= call 1800940b5. No subsequent writes to full offsets 0x8e/0xb0.',
                                 'slide': 'SlideData fill 18009e3b0 zeros eax, 18009e492 zeros temporary rsp+0x50 (template+0x30); 18009e501 copies default via SlideDataDef::operator=. Assignment 1800e930a/e copies byte+0x30. PDB mAirborne offset48, SlideData.mDef offset0; stride0x110.'},
              'limits': ['Native compiled defaults apply to this exact build only.', 'No effect execution or movement decisions are performed by the packet.', 'Book airborne false is the source native type gate, not an inference from actor height.']}
    (output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['packet']))

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--version',type=int,choices=[1,2,3,4,5,6],default=1)
    a = p.parse_args()
    export(a.bank, a.output,a.version)
