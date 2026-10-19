#!/usr/bin/env python3.13
"""Static sound-cue coverage of the original game for the current native scope.

Reuses the animation-coverage scope (animation_coverage.Coverage: fresh-save
RoomOpening + forced RoomSimple01 + Tartarus A_Combat rooms, AthenaUpgrade first-boon flow, the hero
sword arsenal with dash/cast, EnemiesBiome1/TrapsBiome1, and every Lua function
statically reachable from StartRoom, in-scope data and native interaction
registrations, including referenced Lua data subtrees) and collects every FMOD
cue name those can play, with provenance per cue, instead of hand lists:

  sources = (1) string literals in every reachable Lua function (PlaySound,
            PlayVoiceLine(s), Sound/SoundName/...Sound arguments) plus the
            GlobalVoiceLines.<Set> tables those functions name;
            (2) every string of an in-scope Lua data record (WeaponData,
            EnemyData, LootData, ProjectileData, EffectData, ObstacleData,
            ConsumableData, RoomData, HeroData) including GlobalVoiceLines
            sets referenced by expression, InheritFrom resolved;
            (3) every string of in-scope sjson Units/Weapons/Projectiles/
            Obstacles (map objects + obstacles spawned by reachable scripts),
            InheritFrom resolved (FireSound, DetonateSound, HitSound, ...);
            (4) every string of the animation-coverage closure's original
            animation records (Slides Sound/FootstepSound ...).

A string is a cue only when it exactly names an original Studio event in
GUIDs.txt or an original VO.h programmer sample (/VO/<name>), so no field-name
guessing is needed. Each cue resolves to its owning bank (the bank whose node
table holds the event GUID) or to VO.fsb.

Scope bound: a cue under a *Secret* field (secret rooms are behind the
HandleSecretSpawns boundary) and a /VO/ cue found inside a data subtree gated by a cross-run
(meta-progress) requirement that a fresh save cannot meet is reported under
`out_of_scope` with the gating key, not exported. Nothing is dropped
silently. Read-only: no Lua, FMOD or game code is executed.
"""
import argparse, json, re, sys
from collections import defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from animation_coverage import Coverage, sha
from hades_audio import Bank, guid_names

# Cross-run requirements that are false on a fresh save (no prior runs, no
# text lines played, no cosmetics/keepsakes/codex, no gifts or prior kills).
META_REQUIRED = re.compile(
    r'^Required(TextLines|AnyTextLines|AllTextLines|MinAnyTextLines|PlayedTextLines|AnyQueuedTextLines|QueuedTextLines'
    r'|MinCompletedRuns|MinRunsCleared|CompletedRuns|MinTimesCleared|RunsCleared|MinClears|Cosmetics|AnyCosmetics'
    r'|Keepsake|Keepsakes|AnyKeepsake|Gift|MinGiftCount|Codex|CodexEntry|SeenRooms|MinSeenRooms|Killed.*|Kills|'
    r'MinKills.*|Achievement.*|Fish.*|Meta.*|Weapon(Unlock|sUnlocked).*|ActiveMetaUpgrade.*|Unlocked.*|Ever.*|'
    r'Bounty.*|Shrine.*|Run(History|Count).*|PreviousRun.*|LastRun.*|Prev.*|MinRunCount|Quest.*|Relationship.*|'
    r'Friendship.*|Trait(History|Ever).*|PlayedThisRun.*|MinCompletedRunsCache)$')


class AudioCoverage(Coverage):
    def __init__(self, content):
        super().__init__(content)
        names, self.guids_identity = guid_names(content / 'Audio/FMOD/Build/GUIDs.txt')
        self.events = {k[6:]: v for k, v in names.items() if k.startswith('event:')}
        header = (content / 'Audio/FMOD/Build/Desktop/VO.h').read_bytes()
        self.voice = {k.decode().lower() for k in re.findall(rb'^#define\s+(\w+)\s+\d+\s*$', header, re.M)}
        self.sources += [content / 'Audio/FMOD/Build/GUIDs.txt', content / 'Audio/FMOD/Build/Desktop/VO.h']
        self.cues = defaultdict(list); self.voice_gated = defaultdict(list); self._scanned = set()

    def cue(self, s):
        if not isinstance(s, str) or not s or len(s) > 200:
            return None
        n = s if s.startswith('/') else None
        if n is None:
            return None
        if n in self.events:
            return n
        if n.startswith('/VO/') and n.rsplit('/', 1)[-1].lower() in self.voice:
            return n
        return None

    def add_cue(self, cue, origin, gate=None):
        gated = gate and (cue.startswith('/VO/') or gate.startswith('Secret'))
        target = self.voice_gated if gated else self.cues
        if gated: origin = f'{origin} [gated by {gate}]'
        if len(target[cue]) < 8 and origin not in target[cue]:
            target[cue].append(origin)

    def scan(self, value, origin, path=(), gate=None):
        if isinstance(value, dict):
            if gate is None:
                gate = next((k for k in value if isinstance(k, str) and META_REQUIRED.match(k)), None)
            for k, v in value.items():
                if k == '__line__':
                    continue
                if k == '__expr__':
                    self.scan_expr(v, origin + ':' + '.'.join(path), gate); continue
                # Secret-room fields (ShopSecretMusic ...) sit behind the
                # HandleSecretSpawns scope boundary of the coverage scope.
                sub = gate if gate or not (isinstance(k, str) and 'Secret' in k) else f'Secret field {k} (HandleSecretSpawns boundary)'
                self.scan(v, origin, path + (str(k),), sub)
        elif isinstance(value, list):
            for v in value:
                self.scan(v, origin, path, gate)
        else:
            c = self.cue(value)
            if c: self.add_cue(c, origin + ':' + '.'.join(path), gate)

    def scan_expr(self, tokens, origin, gate=None):
        tokens = list(tokens)
        for i in range(len(tokens) - 2):
            if tokens[i] == ('name', 'GlobalVoiceLines') and tokens[i + 1] == ('op', '.') and tokens[i + 2][0] == 'name':
                self.voice_set(tokens[i + 2][1], origin, gate)
            elif tokens[i][0] == 'str':
                c = self.cue(tokens[i][1])
                if c: self.add_cue(c, origin, gate)

    def voice_set(self, name, origin, gate=None):
        key = 'GlobalVoiceLines.' + name
        if key not in self.data or (key, gate is None) in self._scanned:
            return
        self._scanned.add((key, gate is None))
        self.scan(self.data[key][0], f'{key} (via {origin})', (), gate)

    # Record scanning piggybacks on the animation walk of every in-scope record.
    def walk_values(self, value, origin, keyed, path=()):
        if not path and (origin, id(value)) not in self._scanned:
            self._scanned.add((origin, id(value)))
            self.scan(value, origin)
        return super().walk_values(value, origin, keyed, path)

    def scripts(self, reach):
        for fname, f in self.lua.items():
            owner = f.owners(); t = f.tokens
            for i, tk in enumerate(t):
                o = owner[i]
                if o is None or o not in reach:
                    continue
                where = f'{fname}:{tk.line} {o}'
                if tk.kind == 'str':
                    c = self.cue(tk.value)
                    if c: self.add_cue(c, where)
                    elif tk.value in self.things['Obstacles'] and ('Obstacles', tk.value) not in self._scanned:
                        self._scanned.add(('Obstacles', tk.value))
                        self.scan(self.thing('Obstacles', tk.value), f'Obstacles.{tk.value} (spawned {where})')
                elif tk.kind == 'name' and tk.value == 'GlobalVoiceLines' and i + 2 < len(t) and t[i + 1].value == '.' and t[i + 2].kind == 'name':
                    self.voice_set(t[i + 2].value, where)

    def run(self):
        roots, scope = super().run()
        reach, _, _ = self.reach({*self.functions} & set(scope['functions']['seeds']))
        self.scripts(reach)
        names, _ = self.closure(sorted(roots))
        for n in names:
            self.scan(self.resolved(n), f'Animations.{n} ({self.anim_file[n]})')
        return scope, len(names)


# Always-resident banks first (AudioData.lua/Main load Master + Sounds; the
# Tartarus biome bank is resident in scope), then the rest by name.
BANK_PRIORITY = ['Sounds.bank', 'MasterBank.bank', 'Tartarus.bank', 'Enemies.bank', 'Ambience.bank', 'Music.bank']


def bank_index(content, cues):
    """Owning bank per cue. Banks also hold reference nodes for events they do
    not define; owners hold the event body (EVTB). FMOD duplicates shared event
    bodies into several banks, so among owners whose typed graph resolves
    completely (no missing nodes, at least one waveform) the first resident
    bank is chosen; every owner and its body hash is retained as provenance."""
    desktop = content / 'Audio/FMOD/Build/Desktop'; owners = defaultdict(list); banks = {}
    paths = sorted(desktop.glob('*.bank'), key=lambda p: (BANK_PRIORITY.index(p.name) if p.name in BANK_PRIORITY else 99, p.name))
    for path in paths:
        if path.name.startswith('MasterBank.strings'):
            continue
        b = Bank(path); banks[path.name] = b.original
        for cue, guid in cues.items():
            node = b.nodes.get(guid)
            if node and node['kind'] == 'EVTB':
                r = b.resolve(guid)
                owners[cue].append({'bank': path.name, 'evtb_sha256': node['payload_sha256'], 'complete': not r['missing_nodes'], 'waveforms': len(r['samples'])})
    return owners, banks


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--content', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.output.exists():
        raise ValueError('Fresh output required')
    content = a.content.resolve()
    cov = AudioCoverage(content)
    scope, closure = cov.run()
    gated = {c: v for c, v in cov.voice_gated.items() if c not in cov.cues}
    studio = {c: cov.events[c] for c in cov.cues if c in cov.events}
    owners, banks = bank_index(content, studio)
    resolved = {}
    for cue in sorted(cov.cues):
        if cue.startswith('/VO/') and cue.rsplit('/', 1)[-1].lower() in cov.voice:
            resolved[cue] = {'bank': 'VO.fsb', 'kind': 'programmer_voice'}
        elif any(o['complete'] and o['waveforms'] for o in owners.get(cue, [])):
            chosen = next(o for o in owners[cue] if o['complete'] and o['waveforms'])
            resolved[cue] = {'bank': chosen['bank'], 'kind': 'studio_event', 'guid': studio[cue], 'owners': owners[cue]}
        elif any(o['complete'] for o in owners.get(cue, [])):
            resolved[cue] = {'bank': None, 'kind': 'no_waveform', 'guid': studio[cue], 'owners': owners[cue]}
        else:
            resolved[cue] = {'bank': None, 'kind': 'unresolved', 'owners': owners.get(cue, []), 'guid': studio.get(cue)}
    by_bank = defaultdict(list)
    for cue, r in resolved.items():
        by_bank[r['bank'] or 'unresolved'].append(cue)
    report = {
        'schema': 'hades-audio-coverage-v1', 'method': __doc__.strip(),
        'scope': {k: scope[k] for k in ('rooms', 'units', 'weapons', 'projectiles', 'functions', 'interaction_callbacks')}, 'animation_closure': closure,
        'cues': resolved, 'by_bank': {k: sorted(v) for k, v in sorted(by_bank.items())},
        'provenance': {c: cov.cues[c] for c in sorted(cov.cues)},
        'out_of_scope': {c: v for c, v in sorted(gated.items())},
        'statistics': {'cues': len(resolved), **{k: len(v) for k, v in sorted(by_bank.items())}, 'out_of_scope': len(gated)},
        'banks': banks,
        'sources': [{'path': str(s), 'sha256': sha(s)} for s in sorted({Path(s).resolve() for s in cov.sources})],
    }
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report['statistics']))


if __name__ == '__main__':
    main()
