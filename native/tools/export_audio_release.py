#!/usr/bin/env python3
"""One-command audio pack release: derived coverage -> evidence -> streams -> events.

Rolls a previous audio-events manifest forward to version N without hand lists:

  1. scope cues: audio_coverage.AudioCoverage over the current animation-coverage
     scope (rooms, reward chain, data records, reachable Lua) -> studio events
     and /VO/ programmer voices not already exported by the base catalog;
  2. selected-animation closure: every original cue string of the selected
     action-effect and actor animation catalogs (SelectedAnimationDefinitions
     class, the binding build_native.validate_animation_audio_binding checks);
  3. each cue class is decoded from the original bank with the pinned vgmstream
     (PCM hash evidence), cut into single-sample FSB5 containers, and every
     container is re-decoded PCM-identically before it is admitted;
  4. export_audio_events merges base + new stream catalogs into audio-events-vN.

Decoded float WAVs are removed once their container verification passed; the
PCM hashes stay in the evidence manifests. Fresh output directories only.
"""
import argparse, json, shutil, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from audio_coverage import AudioCoverage, bank_index
from build_room_scene import identity
import export_audio_evidence, export_audio_streams, export_voice_evidence, export_audio_events
import hashlib

ROOT = Path(__file__).resolve().parents[2]


def animation_coverage(content, manifests, output):
    """SelectedAnimationDefinitions cue class (same contract as export_animation_audio.py)."""
    cov = AudioCoverage(content); sources = []; counts = {}
    for path in manifests:
        manifest = json.loads(path.read_text()); sources.append(identity(path))
        if manifest['schema'] == 'hades-action-effect-resources-v1':
            row = manifest['catalog']; catalog = Path(row['path'])
            if hashlib.sha256(catalog.read_bytes()).hexdigest() != row['sha256']: raise ValueError('Animation definition catalog changed')
            sources.append(identity(catalog)); records = json.loads(catalog.read_text())['Definitions']
        elif manifest['schema'] == 'hades-original-actor-animations-v1':
            records = {clip['name']: clip['definition'] for clip in manifest['clips']}
        else: raise ValueError('Expected selected original action or actor animation manifest')
        counts[str(path)] = len(records)
        for name, record in records.items(): cov.scan(record, 'SelectedAnimation.' + name + ' (' + str(path) + ')')
    studio = {c: cov.events[c] for c in cov.cues if c in cov.events}; owners, banks = bank_index(content, studio); cues = {}
    for cue in sorted(cov.cues):
        candidates = [r for r in owners.get(cue, []) if r['complete'] and r['waveforms']]
        if candidates: cues[cue] = {'kind': 'studio_event', 'bank': candidates[0]['bank'], 'guid': studio[cue], 'owners': owners[cue]}
        elif any(r['complete'] for r in owners.get(cue, [])): cues[cue] = {'kind': 'no_waveform', 'bank': None, 'guid': studio[cue], 'owners': owners[cue]}
        elif cue.startswith('/VO/') and cue not in cov.events: cues[cue] = {'kind': 'programmer_voice', 'bank': 'VO.fsb'}
        else: cues[cue] = {'kind': 'unresolved', 'bank': None, 'owners': owners.get(cue, []), 'reason': 'Selected animation cue has no complete waveform Studio graph.'}
    report = {'schema': 'hades-audio-coverage-v1', 'scope': {'class': 'SelectedAnimationDefinitions', 'catalog_records': counts,
              'boundary': 'Every original cue string in resolved definitions of selected action and actor catalogs; original children/slide events included by producer definition closure.'},
              'cues': cues, 'provenance': dict(cov.cues), 'banks': banks, 'sources': sources}
    output.write_text(json.dumps(report, indent=2) + '\n')


def drop_pcm(evidence):
    # Verified containers keep the PCM hashes in the evidence manifest; the
    # float WAVs themselves are temporary decode products.
    pcm = evidence / 'audio-pcm'
    if pcm.is_dir(): shutil.rmtree(pcm)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--content', type=Path, default=ROOT / '.local/game/base/Content')
    p.add_argument('--decoder', type=Path, default=ROOT / '.local/tools/vgmstream-db77f98f/build/cli/vgmstream-cli')
    p.add_argument('--base', type=Path, required=True, help='previous audio-events-vN/manifest.json')
    p.add_argument('--version', type=int, required=True)
    p.add_argument('--animation-manifest', type=Path, action='append', default=[], help='selected action-effects / actor-actions manifest')
    p.add_argument('--coverage', type=Path, help='existing audio_coverage.py report (default: derive now)')
    p.add_argument('--max-pcm-bytes', type=int, default=4 * 1024 * 1024 * 1024)
    a = p.parse_args()
    content = a.content.resolve(); v = a.version
    reports = ROOT / f'.local/reports/audio-v{v}'; assets = ROOT / '.local/native-assets'
    evidence_root = assets / f'audio-evidence-release-v{v}'; streams_root = assets / f'audio-streams-release-v{v}'; output = assets / f'audio-events-v{v}'
    for path in [evidence_root, streams_root, output]:
        if path.exists(): raise ValueError('Fresh release output required: ' + str(path))
    reports.mkdir(parents=True, exist_ok=True); evidence_root.mkdir(parents=True); streams_root.mkdir(parents=True)
    coverage = a.coverage
    if coverage is None:
        coverage = reports / 'audio-coverage.json'
        if coverage.exists(): raise ValueError('Fresh coverage report required: ' + str(coverage))
        import subprocess
        subprocess.run([sys.executable, str(Path(__file__).resolve().parent / 'audio_coverage.py'), '--content', str(content), '--output', str(coverage)], check=True)
    base = json.loads(a.base.read_text()); new_streams = []; log = {}
    def stage(name, evidence_fn):
        ev = evidence_root / name; st = streams_root / name
        log[name] = {'evidence': evidence_fn(ev)}
        log[name]['streams'] = export_audio_streams.export(content, ev / 'manifest.json', a.decoder, st)
        drop_pcm(ev); new_streams.append(st / 'manifest.json')
    stage('scope', lambda ev: export_audio_evidence.export(content, a.decoder, ev, a.max_pcm_bytes, coverage=coverage, exclude=[a.base]))
    stage('scope-voice', lambda ev: export_voice_evidence.export(content, a.decoder, ev, [], a.max_pcm_bytes, coverage=coverage, exclude=[a.base]))
    if a.animation_manifest:
        anim = reports / 'animation-audio-coverage.json'
        if anim.exists(): raise ValueError('Fresh animation coverage required')
        animation_coverage(content, a.animation_manifest, anim)
        stage('animation', lambda ev: export_audio_evidence.export(content, a.decoder, ev, a.max_pcm_bytes, coverage=anim, exclude=[a.base]))
    extra = [Path(x['path']) for x in base['additional_stream_manifests']] + new_streams
    log['events'] = export_audio_events.export(content, Path(base['stream_manifest']['path']), output, extra, bool(base.get('mixing_control')), True)
    (reports / 'release.json').write_text(json.dumps(log, indent=2) + '\n')
    print(json.dumps(log['events'], indent=2))


if __name__ == '__main__':
    main()
