#!/usr/bin/env python3.13
"""Export original action/world animation definitions and lossless atlas frames.

The root set is not a hand list: it is the `action_roots` of a static coverage
report (native/tools/animation_coverage.py), i.e. every animation the original
scripts/data reach in the current native scope whose shape this bank serves
(Slide/Book/Constant/ConstantNoUpdate atlas sprites, single- or multi-angle
with angle-major source image names, and Random choices). Records come from every Game/Animations/*.sjson file, InheritFrom
resolved with the native Read rules. A covered name whose resource cannot be
exported is listed under Rejected with the exact reason (never dropped), and
the runtime reports that reason if the original game requests it.

This is resource admission, not simulated FX or successful native execution.
"""
import argparse, copy, hashlib, json, struct, zlib
from collections import defaultdict
from pathlib import Path
from export_room_creation import RangedSjson
from export_effect_initialization import lua
from export_actor_animations import string
from hades_assets import Package, xnb_image
from build_room_scene import identity
TARGET = 'd98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
TYPES = ('Slide', 'Book', 'Constant', 'ConstantNoUpdate')
DEP_FIELDS = ['ChildAnimation', 'CreateAnimation', 'VisualFx', 'Light', 'ChainTo']
MAX_CANVAS = 2048  # ActorAnimationBank::load canvas bound
MAX_FRAMES = 1000
MAX_ANGLES = 64  # ActorAnimationBank::load clip angle bound


def load_records(content):
    records, origins, sources = {}, {}, []
    for source in sorted((content / 'Game/Animations').glob('*.sjson')):
        raw = source.read_bytes(); parser = RangedSjson(raw.decode('utf-8-sig')); sources.append(source)
        for d in parser.parse().get('Animations', []):
            if 'Name' in d:
                if d['Name'] in records: raise ValueError('Ambiguous original animation ' + d['Name'])
                records[d['Name']] = d; origins[d['Name']] = (source, raw, parser.ranges[id(d)])
    return records, origins, sources


def export(a):
    if a.output.exists(): raise ValueError('Fresh output required')
    coverage = json.loads(a.coverage.read_text())
    if coverage.get('schema') != 'hades-animation-coverage-v1' or coverage.get('target_sha256') != TARGET:
        raise ValueError('Animation coverage report schema/target mismatch')
    roots = coverage['action_roots']; packages_in_scope = coverage['packages']
    records, origins, sources = load_records(a.content)
    resolved, provenance = {}, {}

    def resolve(name, stack=()):
        if name in resolved: return resolved[name]
        if name in stack: raise ValueError('Original animation inheritance cycle ' + name)
        own = records[name]; parent = own.get('InheritFrom')
        d = copy.deepcopy(resolve(parent, stack + (name,))) if parent and parent != 'null' else {}
        # Native Clear* acts during this source record's Read, not a persistent
        # animation field. CreateAnimations is additive; first Slides clears.
        if own.get('ClearSlides'): d['Slides'] = []
        if own.get('ClearCreateAnimations'): d['CreateAnimations'] = []
        old_creates = d.get('CreateAnimations', [])
        d.update(copy.deepcopy(own))
        if 'CreateAnimations' in own: d['CreateAnimations'] = old_creates + copy.deepcopy(own['CreateAnimations'])
        source, raw, span = origins[name]
        provenance[name] = {'path': str(source), 'file_sha256': hashlib.sha256(raw).hexdigest(), 'range': span,
                            'range_sha256': hashlib.sha256(raw[span[0]:sum(span)]).hexdigest(), 'inherit_from': parent}
        resolved[name] = d; return d

    missing = [n for n in roots if n not in records]
    if missing: raise ValueError('Coverage roots absent from original animations ' + repr(missing[:8]))
    definitions = {n: resolve(n) for n in sorted(roots)}
    edges = []
    for name, d in definitions.items():
        deps = [(k, d.get(k)) for k in DEP_FIELDS]
        deps += [('Random', c.get('Name')) for c in d.get('Random', [])]
        deps += [('CreateAnimations', c.get('Name')) for c in d.get('CreateAnimations', [])]
        deps += [('Slides.VisualFx', s.get('VisualFx')) for s in d.get('Slides', [])]
        for field, dep in deps:
            if dep and dep != 'null': edges.append({'parent': name, 'field': field, 'child': dep, 'exported': dep in definitions})
    # Shape review: anything this bank cannot represent is rejected by name.
    rejected = {}
    for bank, names in coverage['banks'].items():
        if bank != 'action':
            for n in names: rejected[n] = 'coverage bank ' + bank
    clip_names = {}; extended = {}  # name -> declared NumFrames for EndFrame-extended Books
    for name, d in definitions.items():
        if d.get('Type') == 'Random': continue
        n = d.get('NumFrames', 1); angles = d.get('NumAngles', 1)
        if d.get('Type') not in TYPES or not isinstance(angles, int) or not 0 < angles <= MAX_ANGLES or not isinstance(n, int) or not 0 < n <= MAX_FRAMES:
            rejected[name] = f"shape Type={d.get('Type')} NumAngles={angles} NumFrames={n}"; continue
        # BookAnimation::PrepDraw 0x1802af9a6: texture = GetAngleNum x NumFrames
        # + frame (angle-major), 1-based 4-digit names when more than one image.
        # Single-angle Books index their image list directly with frame-1
        # (StartFrame..EndFrame); a Book that inherits a longer sequence and
        # plays a later window (e.g. SwordSwipeBDark NumFrames 13, frames 8-20)
        # needs images through EndFrame. APPROXIMATION (labelled): the native
        # Book image-list size rule is not recovered; max(NumFrames, EndFrame).
        if d['Type'] == 'Book' and angles == 1 and isinstance(d.get('EndFrame'), int) and d['EndFrame'] > n: extended[name] = n; n = d['EndFrame']
        rows = [d['FilePath'] + f'{i:04}' for i in range(1, n * angles + 1)] if n * angles > 1 else [d['FilePath']]
        if d['Type'] == 'Slide':
            slides = d.get('Slides', [])
            if len(slides) != n: rejected[name] = f'Slide count {len(slides)} differs from frame count {n}'; continue
            if any(not isinstance(s.get('DurationFrames'), int) or not 0 <= s['DurationFrames'] <= 10000 for s in slides):
                rejected[name] = 'absent/unbounded Slide DurationFrames'; continue
        clip_names[name] = rows
    # Sprite index across the in-scope resident packages (closest-to-unit ratio wins).
    wanted = {s for rows in clip_names.values() for s in rows}; matches = {}; manifests = []
    packages = a.content / 'Win/Packages'
    for package in packages_in_scope:
        path = packages / (package + '.pkg_manifest')
        if not path.exists(): continue
        manifests.append(identity(path))
        for e in Package(path).entries():
            for s in e.get('sprites', []):
                if s['name'] not in wanted: continue
                old = matches.get(s['name']); record = {'package': package, 'atlas': e['name'], 'sprite': {k: v for k, v in s.items() if k != 'hull'}}
                if old is None or sum(abs(v - 1) for v in s['scale_ratio']) < sum(abs(v - 1) for v in old['sprite']['scale_ratio']): matches[s['name']] = record
    # Extended Books keep only images that exist in the original packages
    # (some declare an EndFrame past their last image, e.g. Backstab); never
    # fewer than the declared NumFrames.
    for name, base in extended.items():
        rows = clip_names.get(name)
        while rows and len(rows) > base and rows[-1] not in matches: rows.pop()
    for name, rows in list(clip_names.items()):
        bad = [r for r in rows if r not in matches]
        if not bad:
            bad = [r for r in rows if matches[r]['sprite']['flags'] & 3]
            reason = 'multipart/variant original graphic' if bad else None
            if not bad:
                bad = [r for r in rows if max(matches[r]['sprite']['rect'][2:]) > MAX_CANVAS or min(matches[r]['sprite']['rect'][2:]) <= 0 or min(matches[r]['sprite']['scale_ratio']) <= 0 or max(matches[r]['sprite']['scale_ratio']) > 16]
                reason = 'atlas frame outside native canvas/ratio bound' if bad else None
        else:
            reason = 'original image absent from in-scope packages'
        if bad:
            rejected[name] = f'{reason}: {bad[0]}'; del clip_names[name]
    # Packet frames are (original image, source angle) pairs: the bank binds
    # each slide angle to a frame of that angle. An original image reused
    # under another angle by a different definition gets its own record.
    used = {(r, k // (len(rows) // definitions[cname].get('NumAngles', 1))) for cname, rows in clip_names.items() for k, r in enumerate(rows)}
    # Stream frames atlas by atlas: decode one original atlas at a time.
    by_atlas = defaultdict(list)
    for key in sorted(used): by_atlas[(matches[key[0]]['package'], matches[key[0]]['atlas'])].append(key)
    a.output.mkdir(parents=True); packet = a.output / 'action-effect-frames.bin'
    ids, frames, payloads, raw_total, clips = {}, [], [], 0, []
    with packet.open('xb') as out:
        out.write(b'HAA1' + struct.pack('<II', len(used), len(clip_names)))
        for package in sorted({p for p, _ in by_atlas}):
            need = {atlas for p, atlas in by_atlas if p == package}
            for e in Package(packages / (package + '.pkg')).entries():
                if e.get('name') not in need: continue
                im, info = xnb_image(e['data'])
                payloads.append({'package': package, 'atlas': e['name'], 'location': e['location'], 'xnb_sha256': hashlib.sha256(e['data']).hexdigest(), 'decoded': info})
                for name, angle in by_atlas[(package, e['name'])]:
                    s = matches[name]['sprite']; x, y, w, h = s['rect']; rx, ry = s['scale_ratio']
                    if min(x, y) < 0 or x + w > im.width or y + h > im.height: raise ValueError('Original action atlas bounds ' + name)
                    pixels = im.crop((x, y, x + w, y + h)).tobytes(); packed = zlib.compress(pixels, 6)
                    origin = [s['original'][0] / 2 / rx - s['trim'][0], s['original'][1] / 2 / ry - s['trim'][1]]
                    index = len(frames); ids[(name, angle)] = index; raw_total += len(pixels)
                    out.write(struct.pack('<8I2f', w, h, 0, 0, w, h, index, angle, *origin)); out.write(struct.pack('<III', len(pixels), len(packed), zlib.crc32(pixels))); out.write(packed)
                    frames.append({'name': name, **matches[name], 'origin': origin, 'original_angle': angle, 'rgba_sha256': hashlib.sha256(pixels).hexdigest(), 'raw_bytes': len(pixels), 'packed_bytes': len(packed)})
                del im
        if len(frames) != len(used): raise ValueError('Original action atlas payload missing')
        for name, rows in clip_names.items():
            d = definitions[name]; slides = []
            if d['Type'] == 'Slide':
                for i, s in enumerate(d['Slides'], 1):
                    if s['DurationFrames']: slides.append((i, s['DurationFrames'], {k: v for k, v in s.items() if k != 'DurationFrames'}))
            else: slides = [(i, 1, {}) for i in range(1, len(rows) // d.get('NumAngles', 1) + 1)]
            if not slides: raise ValueError('Original Slide animation without positive duration ' + name)
            angles = d.get('NumAngles', 1); f = len(rows) // angles
            for text in [name, '', '']: string(out, text)
            out.write(struct.pack('<4I4f', angles, angles, 512 if d['Type'] == 'Book' else 0, len(slides), 1., 0., 0., 1 / 60)); out.write(struct.pack('<' + 'I' * angles, *range(angles)))
            for i, ticks, events in slides: out.write(struct.pack('<II', i, ticks) + struct.pack('<' + 'I' * angles, *[ids[(rows[a * f + i - 1], a)] for a in range(angles)])); string(out, json.dumps(events, separators=(',', ':')))
            # Indexed angle-major like the source names: [angle x NumFrames + frame - 1].
            clips.append({'name': name, 'index': len(clips), 'num_angles': angles, 'atlas_ratio_by_frame': [matches[r]['sprite']['scale_ratio'] for r in rows]})
    exported = {n: d for n, d in definitions.items() if n not in rejected}
    frame_meta = [{'name': f['name'], 'package': f['package'], 'atlas': f['atlas'], 'sprite': {k: f['sprite'][k] for k in ('rect', 'trim', 'original', 'scale_ratio', 'flags')}, 'origin': f['origin']} for f in frames]
    catalog = {'Schema': 'HadesActionEffectResources1', 'Target': TARGET, 'Roots': roots, 'Definitions': exported,
               'Rejected': dict(sorted(rejected.items())), 'Clips': clips, 'FrameMetadata': frame_meta,
               'ImageBankSha256': hashlib.sha256(packet.read_bytes()).hexdigest(), 'CompiledDefaults': {},
               'NativeNormalizationEvidence': ['native-action-animation-deserialize-static.json', 'native-action-animation-create-list-static.json']}
    (a.output / 'catalog.json').write_text(json.dumps(catalog, indent=1) + '\n'); (a.output / 'action-effects.lua').write_text('return ' + lua(catalog) + '\n')
    report = {'schema': 'hades-action-effect-resources-v1', 'target_sha256': TARGET, 'packet': identity(packet), 'catalog': identity(a.output / 'catalog.json'),
              'lua': identity(a.output / 'action-effects.lua'), 'coverage': identity(a.coverage),
              'sources': [*[identity(p) for p in sources], *manifests], 'source_record_provenance': {n: provenance[n] for n in exported},
              'dependencies': edges, 'original_payloads': payloads, 'frames': frames,
              'statistics': {'coverage_action_roots': len(roots), 'definitions': len(exported), 'rejected': len(rejected),
                             'unique_original_images': len(frames), 'source_image_bindings': sum(len(r) for r in clip_names.values()),
                             'raw_rgba_bytes': raw_total, 'packet_bytes': packet.stat().st_size},
              'limits': ['Original pixels and source operations only. Allocation, source loop/lifetime/RNG/typed components/sound/light/marker and every CreateAnimations child must execute through actual providers.',
                         'Image bank origin is source atlas midpoint. Explicit OriginX/Y, FlipVertical and PostRotateScale require native draw placement from source definitions; they are deliberately not baked into shared pixels.',
                         'Slide positive DurationFrames and source events retained; Book and Constant nominal image ticks are indexing only.',
                         'Native absent field defaults are not fabricated. Rejected names are listed with their reason and fail loudly at runtime.']}
    (a.output / 'manifest.json').write_text(json.dumps(report, indent=1) + '\n'); print(json.dumps({'packet': report['packet'], **report['statistics']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for k in ['content', 'output', 'coverage']: p.add_argument('--' + k, type=Path, required=True)
    export(p.parse_args())
