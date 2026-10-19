#!/usr/bin/env python3.13
"""Static animation coverage of the original game for the current native scope.

Derives every animation name the original scripts/data can reach in the scope
the native runtime runs now, with provenance per name, instead of hand lists:

  scope   = fresh-save opening room (RoomOpening), its forced first encounter
            room (RoomSimple01), and the Tartarus combat rooms
            (A_Combat*), the first-boon flow (AthenaUpgrade loot, boon choice,
            door reward previews and door icons), the hero sword arsenal and the
            Tartarus enemy/trap sets (EnemySets.EnemiesBiome1 / TrapsBiome1).

  sources = (1) string literals in every Lua function reachable (static call
            graph: direct calls, function values, function-name strings) from
            StartRoom, function names held by in-scope data, and native
            interaction callbacks whose source-name/group selectors match
            scoped map objects or groups added by reachable scripts;
            anonymous registration callbacks have explicit graph identities;
            statically referenced Lua data subtrees (UIData.UsePrompt, etc.)
            add their resource values and function references;
            literal-concatenation sites inside animation calls resolve to every
            animation matching the literal prefix/suffix;
            (2) every animation-valued field of in-scope Lua data records
            (LootData, EnemyData, WeaponData, ProjectileData, EffectData,
            ObstacleData, ConsumableData, RoomSetData, HeroData,
            StatusAnimations), InheritFrom resolved;
            (3) every animation-valued field of in-scope sjson Units, Weapons,
            Projectiles and Obstacles (map objects of the in-scope rooms),
            InheritFrom resolved (FireFx, HitFx, DeathFx, ImpactFx, Graphic,
            AttachedAnim, Using ...);
            closed over Game/Animations ChildAnimation, CreateAnimation(s),
            ChainTo, VisualFx, Light, Random choices and Slides events.
            Concatenations that build an animation name (inside an animation
            call, or assigned to a local that an animation call then names,
            e.g. AudioScripts.lua:856 CleanUpCurrentSpeechId
            `animation.."_End"`) resolve to every animation whose remainder is
            an in-scope string or an already reached animation; this is
            iterated with the closure to a fixed point (StatusAnimations
            values only ever reach scripts as variables).

Every reached name is classified into a bank (action sprite bank, actor Bink
bank, unsupported shape, package outside scope, missing image) and nothing is
dropped silently. Read-only: no Lua or game code is executed.
"""
import argparse, copy, hashlib, json, re
from animation_postread import normalize_animation_postread
from collections import defaultdict
from pathlib import Path
from build_room_scene import Sjson, merge
from hades_assets import Package, map_objects
from lua_static import LuaFile, top_level_tables

TARGET = 'd98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
# Packages resident for the scope: always-resident engine packages (the same
# set every existing native exporter reads), the Tartarus biome package, and
# packages named after in-scope weapons/loot (RoomManager.lua:1509
# LoadPackages{Name=lootName}, Combat.lua:3742 EquipWeapon LoadPackages).
BASE_PACKAGES = ['Fx', 'GUI', 'RoomManager', 'Dev', 'Tartarus']
ROOMS = re.compile(r'^(RoomOpening|RoomSimple01|A_Combat\d+[AB]?)$')
LOOT = ['AthenaUpgrade']
HERO_SET = 'SwordWeapon'                      # WeaponSets.lua HeroWeaponSets (fresh save)
HERO_EXTRA_WEAPONS = ['RushWeapon', 'RangedWeapon']  # PlayerUnits _PlayerUnit dash / cast
ENEMY_SETS = ['EnemiesBiome1', 'TrapsBiome1']
LOOT_ICON_FIELDS = ['Icon', 'DoorIcon']       # RoomPresentation.lua:1570 CreateDoorRewardPreview (any god door)
SEED_FUNCTIONS = ['StartRoom']
# Scope boundary: flows reachable from StartRoom that the current native scope
# (fresh save, first Tartarus rooms) does not enter. They are not traversed and
# are listed in the report. Run end/House: DeathPresentation, EasyMode (God
# Mode grows after deaths), WeaponKit (House arsenal); meta: gift/relationship,
# demo build, Hell-mode timer; Chaos/secret gates; Death Defiance (none on a
# fresh save); Hades interjections; unlock banners (post-run unlocks).
BOUNDARY = {'DeathPresentation', 'EasyModeLevelUpPresentation', 'WeaponKitSpecialInteractPresentation',
            'MaxedRelationshipPresentation', 'StartDemoPresentation', 'BiomeTimeCheckpointPresentation',
            'HandleSecretSpawns', 'SacrificeHealth', 'SecretDoorUsedPresentation', 'ShrinePointDoorUsedPresentation',
            'PlayerLastStandPresentationStart', 'PlayerLastStandHealingPresentation', 'PlayerLastStandHealingText',
            'DoHadesSpeakingPresentation', 'DisplayUnlockText', 'HeroTraitTransformPresentation'}
# Weapon/projectile names are followed only from combat records (unit weapon
# options, weapon Projectile/FireProjectile fields, projectile spawns); HeroData
# and room/loot records name every weapon kit and are not arsenal edges.
# Arsenal fields (sjson Units CollisionWeapon/OnDeathWeapon/OnTouchdownWeapon,
# Weapons Projectile, Projectiles SpawnOnDetonate/SpawnOnDeath, Lua
# WeaponOptions/AIData.ChainedWeapon/DumbFireWeapons/DashWeapon ...).
EDGE_KEY = re.compile(r'(SwapOnFire|Weapon|Weapons|WeaponOptions|WeaponName|Projectile|SpawnOnDeath|SpawnOnDetonate|AddControlOnDeath2?|DumbFireWeapons|PreAttackDash|EquipRandomWeapon|AthenaUpgrade|\d+)$')
# Conditions, narrative, meta and out-of-scope variants are not arsenal edges:
# elite attributes (EliteAttributeOptions), Pact/shrine overrides, unequip
# lists, codex/genus names, god swaps other than the in-scope loot.
EDGE_SKIP = re.compile(r'Required|VoiceLines|Shrine|Elite|Codex|Genus|Objective|Sound|Immunity|CanBeProjectileDefense|Modifiers|Overrides|Overwrite|Expire|Killing|Unequip|Requirements|Text|Upgrade$|WeaponSwap$|GodUpgradeWeaponSwap|Disarmed|Block|Name$|InheritFrom')
COMBAT_KINDS = {'Units', 'EnemyData', 'Weapons', 'WeaponData', 'Projectiles', 'ProjectileData'}
ANIM_CALLS = {'SetAnimation', 'CreateAnimation', 'CreateAnimationsBetween', 'PlayStatusAnimation', 'StopAnimation',
              'SetAnimationFrameTarget', 'SwapAnimation', 'StopStatusAnimation', 'CreateAnimations'}
OBSTACLE_CALLS = {'SpawnObstacle', 'CreateScreenObstacle', 'CreateScreenComponent'}
ANIM_KEY = re.compile(r'(Fx|FxL|FxR|Graphic|Anim|Animation|Animations|Icon|Light|Preview)$')
DEP_FIELDS = ['ChildAnimation', 'CreateAnimation', 'CreateAnimationOnEnd', 'VisualFx', 'Light', 'ChainTo']
SLIDE_EVENT_FIELDS = ['VisualFx', 'FootstepFxL', 'FootstepFxR']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Coverage:
    def __init__(self, content):
        self.content = content
        self.sources = []
        self.provenance = defaultdict(list)  # name -> [provenance strings]
        self.load_animations()
        self.load_sjson()
        self.load_lua()

    # ---------- original data ----------
    def load_animations(self):
        self.anims, self.anim_file = {}, {}
        for path in sorted((self.content / 'Game/Animations').glob('*.sjson')):
            self.sources.append(path)
            for r in Sjson(path.read_bytes().decode('utf-8-sig')).parse().get('Animations', []):
                if 'Name' not in r:
                    continue
                if r['Name'] in self.anims:
                    raise ValueError('Ambiguous original animation ' + r['Name'])
                self.anims[r['Name']] = r; self.anim_file[r['Name']] = path.name
        self._resolved = {}
        self.binks = defaultdict(list)
        for name in self.anims:
            video = self.resolved(name).get('VideoTexture')
            if video: self.binks[video].append(name)

    def resolved(self, name, stack=()):
        if name in self._resolved:
            return self._resolved[name]
        if name in stack:
            raise ValueError('Animation inheritance cycle ' + name)
        own = self.anims[name]; parent = own.get('InheritFrom')
        d = copy.deepcopy(self.resolved(parent, stack + (name,))) if parent and parent != 'null' else {}
        if own.get('ClearSlides'): d['Slides'] = []
        if own.get('ClearCreateAnimations'): d['CreateAnimations'] = []
        old = d.get('CreateAnimations', []); d.update(copy.deepcopy(own))
        if 'CreateAnimations' in own: d['CreateAnimations'] = old + copy.deepcopy(own['CreateAnimations'])
        d = normalize_animation_postread(d)
        self._resolved[name] = d
        return d

    def load_sjson(self):
        self.things = {}
        for category in ['Units', 'Weapons', 'Projectiles', 'Obstacles']:
            table = {}
            for path in sorted((self.content / 'Game' / category).glob('*.sjson')):
                self.sources.append(path)
                for r in Sjson(path.read_bytes().decode('utf-8-sig')).parse().get(category, []):
                    if 'Name' in r:
                        table.setdefault(r['Name'], (r, path.name))
            self.things[category] = table

    def thing(self, category, name, stack=()):
        r, _ = self.things[category][name]
        parent = r.get('InheritFrom')
        if parent and parent in self.things[category] and parent not in stack:
            return merge(self.thing(category, parent, stack + (name,)), r)
        return r

    def load_lua(self):
        self.lua = {}
        self.data = {}  # dotted table name -> (value, file, line)
        self.data_ranges = defaultdict(list)
        for path in sorted((self.content / 'Scripts').glob('*.lua')):
            self.sources.append(path)
            f = LuaFile(path); self.lua[path.name] = f
            for name, value, line, start, end in top_level_tables(f):
                self.data.setdefault(name, (value, path.name, line))
                self.data_ranges[path.name].append((start, end))
        self.functions = defaultdict(list)
        for fname, f in self.lua.items():
            for r in f.functions:
                if r['name']:
                    self.functions[r['name']].append((fname, r))

    def lua_table(self, name):
        return self.data[name][0]

    def lua_record(self, table, key, stack=()):
        """Lua ProcessDataInheritance: InheritFrom list, later parents override."""
        own = table.get(key)
        if not isinstance(own, dict):
            return {}
        base = {}
        for parent in own.get('InheritFrom', []) if isinstance(own.get('InheritFrom'), list) else []:
            if isinstance(parent, str) and parent in table and parent not in stack:
                base = merge(base, self.lua_record(table, parent, stack + (key,)))
        return merge(base, {k: v for k, v in own.items() if k != 'InheritFrom'})

    # ---------- collection ----------
    def note(self, name, origin):
        if len(self.provenance[name]) < 12 and origin not in self.provenance[name]:
            self.provenance[name].append(origin)

    def walk_values(self, value, origin, keyed, path=()):
        """Animation names held by a record. keyed: require an animation-like key."""
        found = []
        if isinstance(value, dict):
            for k, v in value.items():
                if k in ('Name', 'InheritFrom', '__line__', '__expr__', '__function__'):
                    continue
                found += self.walk_values(v, origin, keyed, path + (str(k),))
            for k in ('__array__',):
                pass
        elif isinstance(value, list):
            for v in value:
                found += self.walk_values(v, origin, keyed, path)
        elif isinstance(value, str) and value in self.binks and path and path[-1].endswith('Binks'):
            # WeaponBinks/PreLoadBinks name Bink videos; the engine plays them
            # through every animation whose VideoTexture is that video.
            found += [(n, origin + ':' + '.'.join(path) + ' ' + value) for n in self.binks[value]]
        elif isinstance(value, str) and value in self.anims:
            key = path[-1] if path else ''
            if not keyed or ANIM_KEY.search(key) or (len(path) >= 2 and path[-2] == 'Using'):
                found.append((value, origin + ':' + '.'.join(path)))
        return found

    def edge_strings(self, value, path=()):
        """Weapon/projectile names held by arsenal fields of a combat record."""
        if isinstance(value, dict):
            for k, v in value.items():
                k = str(k)
                if EDGE_SKIP.search(k) and not (path and path[-1] == 'GodUpgradeWeaponSwap' and k in LOOT):
                    continue
                yield from self.edge_strings(v, path + (k,))
        elif isinstance(value, list):
            for v in value:
                yield from self.edge_strings(v, path)
        elif isinstance(value, str) and path and EDGE_KEY.search(path[-1]) and not value.startswith('Elite'):
            yield value

    def strings(self, value):
        if isinstance(value, dict):
            for k, v in value.items():
                if k not in ('__line__',):
                    yield from self.strings(v)
        elif isinstance(value, list):
            for v in value:
                yield from self.strings(v)
        elif isinstance(value, str):
            yield value

    def run(self):
        roots = {}
        self.universe = set()
        self.entity_origin = {}
        self.obstacle_graphics = {}
        def add(name, origin):
            roots.setdefault(name, None); self.note(name, origin)
        scope = {'rooms': [], 'units': [], 'weapons': [], 'projectiles': [], 'obstacles': [], 'lua_records': [], 'functions': []}
        # Rooms and their map objects.
        obstacles, units = set(), set()
        callback_selectors = set()
        self.scene_graphics = {}
        for path in sorted((self.content / 'Win/Maps').glob('*.thing_bin')):
            if not ROOMS.match(path.stem):
                continue
            scope['rooms'].append(path.stem); self.sources.append(path)
            callback_selectors.add(path.stem)
            for obj in map_objects(path):
                (obstacles if obj['DataType'] == 1 else units).add(obj['Name'])
                callback_selectors.update(g for g in obj['GroupNames'] if g)
        map_placed = set(obstacles)
        enemy_sets = self.lua_table('EnemySets') if 'EnemySets' in self.data else {}
        for s in ENEMY_SETS:
            units.update(x for x in enemy_sets.get(s, []) if isinstance(x, str))
        units.add('_PlayerUnit')
        hero_sets = self.lua_table('WeaponSets')['HeroWeaponSets']  # WeaponSets.lua:133
        weapons = set(hero_sets.get(HERO_SET, [])) | {HERO_SET} | set(HERO_EXTRA_WEAPONS)
        projectiles = set()
        enemy_data = self.lua_table('UnitSetData.Enemies')
        weapon_data = self.lua_table('WeaponData'); projectile_data = self.lua_table('ProjectileData')
        effect_data = self.lua_table('EffectData'); obstacle_data = self.lua_table('ObstacleData')
        loot_data = self.lua_table('LootData'); trait_data = self.lua_table('TraitData'); consumable_data = self.lua_table('ConsumableData')
        rooms_lua = self.lua_table('RoomSetData.Tartarus'); rooms_base = self.lua_table('RoomSetData.Base')
        function_seeds = set(SEED_FUNCTIONS)
        lua_scope = []
        # Fixed point over entity references (weapons/projectiles named by records).
        changed = True
        visited = set()
        while changed:
            changed = False
            records = []
            for u in sorted(units):
                if u in self.things['Units']: records.append(('Units', u, self.thing('Units', u)))
                if u in enemy_data: records.append(('EnemyData', u, self.lua_record(enemy_data, u)))
            for w in sorted(weapons):
                if w in self.things['Weapons']: records.append(('Weapons', w, self.thing('Weapons', w)))
                if w in weapon_data: records.append(('WeaponData', w, self.lua_record(weapon_data, w)))
            for p in sorted(projectiles):
                if p in self.things['Projectiles']: records.append(('Projectiles', p, self.thing('Projectiles', p)))
                if p in projectile_data: records.append(('ProjectileData', p, self.lua_record(projectile_data, p)))
            for o in sorted(obstacles):
                if o in self.things['Obstacles']: records.append(('Obstacles', o, self.thing('Obstacles', o)))
                if o in obstacle_data: records.append(('ObstacleData', o, self.lua_record(obstacle_data, o)))
            for key in LOOT:
                loot = self.lua_record(loot_data, key)
                records.append(('LootData', key, loot))
                # Boons this god offers (UpgradeChoice.lua boon screen icons).
                for trait in sorted(set(self.strings(loot)) & set(trait_data)):
                    records.append(('TraitData', trait, self.lua_record(trait_data, trait)))
            for room in scope['rooms']:
                table = rooms_lua if room in rooms_lua else rooms_base
                if room in table: records.append(('RoomData', room, self.lua_record({**rooms_base, **rooms_lua}, room)))
            records.append(('HeroData', 'HeroData', self.lua_table('HeroData')))
            # EnemyData.lua:27625 StatusAnimations: every value is an animation.
            for k, v in sorted(self.lua_table('StatusAnimations').items()):
                if v in self.anims: add(v, f'StatusAnimations.{k} (RoomManager.lua PlayStatusAnimation)')
            for kind, key, rec in records:
                combat = kind in COMBAT_KINDS
                for s in self.edge_strings(rec) if combat else ():
                    if s in self.things['Weapons'] or s in weapon_data:
                        if s not in weapons: weapons.add(s); changed = True; self.entity_origin[s] = f'{kind}.{key}'
                    if s in self.things['Projectiles'] or s in projectile_data:
                        if s not in projectiles: projectiles.add(s); changed = True; self.entity_origin[s] = f'{kind}.{key}'
                for s in set(self.strings(rec)):
                    if s in self.functions:
                        function_seeds.add(s)
                    self.universe.add(s)
                if (kind, key) in visited:
                    continue
                visited.add((kind, key))
                sjson = kind in ('Units', 'Weapons', 'Projectiles', 'Obstacles')
                for name, origin in self.walk_values(rec, f'{kind}.{key}', keyed=True):
                    if kind == 'Obstacles' and key in map_placed and origin.endswith(':Thing.Graphic'):
                        # Map-placed primaries are owned by the scene/primary banks.
                        self.scene_graphics.setdefault(name, origin); continue
                    add(name, origin)
                # Effects and consumables named by in-scope records.
                for s in set(self.strings(rec)):
                    if s in effect_data and ('EffectData', s) not in visited:
                        visited.add(('EffectData', s))
                        for name, origin in self.walk_values(effect_data[s], f'EffectData.{s}', keyed=True): add(name, origin)
                    if not sjson and s in consumable_data and ('ConsumableData', s) not in visited:
                        visited.add(('ConsumableData', s))
                        for name, origin in self.walk_values(self.lua_record(consumable_data, s), f'ConsumableData.{s}', keyed=True): add(name, origin)
        # Door icons of every god door (CreateDoorRewardPreview).
        for key, rec in sorted(loot_data.items()):
            if isinstance(rec, dict):
                for field in LOOT_ICON_FIELDS:
                    v = rec.get(field)
                    if isinstance(v, str) and v in self.anims: add(v, f'LootData.{key}.{field} (RoomPresentation.lua CreateDoorRewardPreview)')
        scope.update(units=sorted(units), weapons=sorted(weapons), projectiles=sorted(projectiles), obstacles=sorted(obstacles))
        # Native interaction dispatch enters anonymous top-level registrations,
        # outside StartRoom's named-function graph. Derive event kinds from the
        # actual bridge, and selectors from scoped map groups/entities/data.
        # Scope is interaction only; UI mouse/debug/meta events remain excluded.
        callback_source = Path(__file__).resolve().parents[1] / 'app/lua_interaction.cpp'
        self.sources.append(callback_source)
        events = set(re.findall(r'dispatchThingCallbacks\("(On\w+)"', callback_source.read_text()))
        callback_selectors.update(units | weapons | projectiles | obstacles | set(LOOT) | self.universe)
        registrations, excluded = [], []
        pending = [(fname, entry) for fname, f in self.lua.items() for entry in f.registrations if entry['event'] in events]
        while True:
            rest = []
            for fname, entry in pending:
                selectors = entry['selector'].split() if entry['selector'] else []
                selectors = selectors[:selectors.index(';')] if ';' in selectors else selectors
                selected = not selectors or bool(set(selectors) & callback_selectors)
                record = {**entry, 'source': fname, 'reason': 'unfiltered' if not selectors else 'scoped source-name/group match' if selected else 'selector outside scoped maps/data'}
                if selected:
                    registrations.append(record);function_seeds.add(entry['name'])
                else: rest.append((fname,entry))
            reach, sites, dynamic = self.reach(function_seeds)
            extra = set(self.reachable_groups) - callback_selectors
            if not extra:
                excluded = [{**entry, 'source': fname, 'reason': 'selector outside scoped maps/data'} for fname,entry in rest]
                break
            callback_selectors.update(extra);pending=rest
        scope['interaction_callbacks'] = {'native_source': str(callback_source), 'events': sorted(events), 'included': registrations, 'excluded': excluded}
        scope['interaction_callbacks']['script_groups'] = self.reachable_groups
        # Script reachability.
        scope['entity_origin'] = dict(sorted(self.entity_origin.items()))
        scope['scene_map_graphics'] = dict(sorted(self.scene_graphics.items()))
        scope['functions'] = {'boundary': sorted(BOUNDARY), 'seeds': sorted(function_seeds & set(self.functions)), 'reachable': len(reach)}
        for name, origin in sites:
            add(name, origin)
        self.dynamic_sites = dynamic
        return roots, scope

    def reach(self, seeds):
        refs, lits, concat, dyn, strs, obst = defaultdict(set), defaultdict(list), defaultdict(list), defaultdict(list), defaultdict(set), defaultdict(list)
        data_refs = defaultdict(list)
        groups = defaultdict(dict)
        var_concat, var_use = defaultdict(list), defaultdict(dict)
        for fname, f in self.lua.items():
            owner = f.owners(); t = f.tokens
            data = self.data_ranges[fname]
            # Token -> innermost animation call (function name) for concat/dynamic sites.
            call_at = [None] * len(t)
            for i, tk in enumerate(t):
                if tk.kind == 'name' and tk.value in (ANIM_CALLS | OBSTACLE_CALLS | {'AddToGroup'}) and t[i + 1].kind == 'op' and t[i + 1].value == '(':
                    depth, j = 0, i + 1
                    while True:
                        v = t[j]
                        if v.kind == 'op' and v.value in ('(', '{', '['): depth += 1
                        elif v.kind == 'op' and v.value in (')', '}', ']'):
                            depth -= 1
                            if depth == 0: break
                        elif v.kind == 'eof': break
                        call_at[j] = tk.value; j += 1
            for i, tk in enumerate(t):
                o = owner[i]
                if o is None:
                    continue  # unselected top-level code is outside the graph
                # Static data references carry resources indirectly (for example
                # UIData.UsePrompt.AttractAnim), and may name more functions.
                # Resolve only the referenced literal subtree, not the complete
                # global data universe. Dynamic indexing remains unresolved.
                if tk.kind == 'name' and not (t[i - 1].kind == 'op' and t[i - 1].value in ('.', ':')):
                    parts, j = [tk.value], i + 1
                    while j + 1 < len(t) and t[j].value == '.' and t[j + 1].kind == 'name':
                        parts.append(t[j + 1].value); j += 2
                    if len(parts) > 1:
                        for k in range(len(parts), 0, -1):
                            key = '.'.join(parts[:k])
                            if key not in self.data: continue
                            value, source, line = self.data[key]
                            resolved = True
                            for field in parts[k:]:
                                if not isinstance(value, dict) or field not in value:
                                    resolved = False; break
                                value = value[field]
                            if resolved:
                                # Defer actual collection until this function
                                # is reachable: unreachable UI/meta code must
                                # not widen the scope.
                                data_refs[o].append((value, f'{fname}:{tk.line} {o} -> {".".join(parts)} ({source}:{line})'))
                                refs[o].update(s for s in self.strings(value) if s in self.functions)
                            break
                if tk.kind == 'name' and tk.value in self.functions and not (t[i - 1].kind == 'op' and t[i - 1].value in ('.', ':')):
                    refs[o].add(tk.value)
                elif tk.kind == 'str':
                    strs[o].add(tk.value)
                    if (call_at[i] == 'AddToGroup' and t[i - 2].value == 'Name') or (call_at[i] in OBSTACLE_CALLS and t[i - 2].value == 'Group'):
                        groups[o].setdefault(tk.value, f'{fname}:{tk.line} {o} {call_at[i]}')
                    if tk.value in self.functions: refs[o].add(tk.value)
                    if tk.value in self.anims: lits[o].append((tk.value, f'{fname}:{tk.line} {o}'))
                    elif tk.value in self.things['Obstacles']: obst[o].append((tk.value, f'{fname}:{tk.line} {o}'))
                    if ((t[i + 1].kind == 'op' and t[i + 1].value == '..') or (t[i - 1].kind == 'op' and t[i - 1].value == '..')) and len(tk.value) >= 4 and (call_at[i] or len(tk.value) >= 6):
                        suffix = t[i - 1].value == '..' and t[i - 1].kind == 'op'
                        concat[o].append((tk.value, suffix, f'{fname}:{tk.line} {o} {call_at[i] or "<expr>"}'))
                    if call_at[i] is None and ((t[i + 1].kind == 'op' and t[i + 1].value == '..') or (t[i - 1].kind == 'op' and t[i - 1].value == '..')):
                        # `local v = x .. "lit"`: the assignment target, if an
                        # animation call of this function names it (var_use).
                        j = i - 1
                        while j > 0 and (t[j].kind in ('name', 'str') or (t[j].kind == 'op' and t[j].value in ('.', '..', '[', ']'))):
                            j -= 1
                        if t[j].kind == 'op' and t[j].value == '=' and t[j - 1].kind == 'name':
                            var_concat[o].append((t[j - 1].value, tk.value, t[i - 1].kind == 'op' and t[i - 1].value == '..', f'{fname}:{tk.line} {o}'))
                if call_at[i] in ANIM_CALLS and tk.kind == 'name' and tk.value in ('Name', 'Animation') and t[i + 1].value == '=' and t[i + 2].kind == 'name' and t[i + 3].kind == 'op' and t[i + 3].value in (',', '}'):
                    var_use[o].setdefault(t[i + 2].value, call_at[i])
                if call_at[i] in ANIM_CALLS and tk.kind == 'name' and tk.value in ('Name', 'Animation') and t[i + 1].value == '=' and t[i + 2].kind != 'str':
                    dyn[o].append(f'{fname}:{tk.line} {o} {call_at[i]} {tk.value}=' + ''.join(str(x.value) for x in t[i + 2:i + 8]))
        seen = set(s for s in seeds if s in self.functions and s not in BOUNDARY); stack = list(seen)
        while stack:
            x = stack.pop()
            for y in refs.get(x, ()):
                if y not in seen and y not in BOUNDARY: seen.add(y); stack.append(y)
        sites, dynamic = [], []
        self.concat_pending = []  # (text, suffix, origin) of reachable animation-name concatenations
        universe = set(self.universe)
        for fn in seen:
            universe.update(strs.get(fn, ()))
        for fn in sorted(seen):
            sites += lits.get(fn, [])
            for value, origin in data_refs.get(fn, []):
                universe.update(self.strings(value))
                sites += self.walk_values(value, origin, keyed=False)
            # A concatenation resolves only to names whose remainder is an
            # in-scope string (data value or reachable literal).
            for var, text, suffix, origin in var_concat.get(fn, []):
                if var in var_use.get(fn, {}):
                    concat[fn].append((text, suffix, f'{origin} {var_use[fn][var]} via local {var}'))
            for text, suffix, origin in concat.get(fn, []):
                if origin.split()[-1] in OBSTACLE_CALLS or origin.split()[-1] == '<expr>':
                    # Obstacle-name concatenation (e.g. "TraitTray_Slots_"..GetTraitTraySize()):
                    # remainder is an in-scope string or an integer.
                    for obstacle in self.things['Obstacles']:
                        rest = obstacle[:-len(text)] if suffix and obstacle.endswith(text) else (obstacle[len(text):] if not suffix and obstacle.startswith(text) else None)
                        if rest and (rest in universe or rest.isdigit()):
                            obst[fn].append((obstacle, origin + ' ' + repr(text)))
                call = origin.split(' via local ')[0].split()[-1]
                if call not in ANIM_CALLS:
                    continue
                self.concat_pending.append((text, suffix, origin))
                for name in self.anims:
                    if name != text and (name.endswith(text) and name[:-len(text)] in universe if suffix else name.startswith(text) and name[len(text):] in universe):
                        sites.append((name, origin + (' ..' if suffix else '.. ') + repr(text)))
            dynamic += dyn.get(fn, [])
            # SpawnObstacle/CreateScreenObstacle names: a Thing.Graphic that is
            # a sprite path (no AnimationData) is a native Dataless animation.
            for obstacle, origin in obst.get(fn, []):
                graphic = self.thing('Obstacles', obstacle).get('Thing', {}).get('Graphic')
                if isinstance(graphic, str) and graphic and graphic not in self.anims and graphic != 'null':
                    self.obstacle_graphics.setdefault(graphic, []).append(f'{origin} {obstacle}.Thing.Graphic')
        self.reach_universe = universe
        self.reachable_groups = {g:origin for fn in sorted(seen) for g,origin in groups[fn].items()}
        return seen, sites, dynamic

    def resolve_concat(self, reached):
        """Concatenation sites resolved against in-scope strings plus reached animations."""
        pool = self.reach_universe | set(reached)
        out = []
        for text, suffix, origin in self.concat_pending:
            for name in self.anims:
                if name == text:
                    continue
                rest = name[:-len(text)] if suffix and name.endswith(text) else (name[len(text):] if not suffix and name.startswith(text) else None)
                if rest and rest in pool:
                    out.append((name, origin + (' ..' if suffix else '.. ') + repr(text) + ' <- ' + rest))
        return out

    # ---------- closure and classification ----------
    def closure(self, roots):
        names, edges = list(roots), []
        seen = set(names)
        for name in names:
            d = self.resolved(name)
            deps = [(k, d.get(k)) for k in DEP_FIELDS]
            deps += [('Random', c.get('Name')) for c in d.get('Random', []) if isinstance(c, dict)]
            deps += [('CreateAnimations', c.get('Name')) for c in d.get('CreateAnimations', []) if isinstance(c, dict)]
            deps += [('Slides.' + k, s.get(k)) for s in d.get('Slides', []) if isinstance(s, dict) for k in SLIDE_EVENT_FIELDS]
            for field, dep in deps:
                if not isinstance(dep, str) or not dep or dep == 'null':
                    continue
                edges.append({'parent': name, 'field': field, 'child': dep})
                if dep in self.anims and dep not in seen:
                    seen.add(dep); names.append(dep); self.note(dep, f'closure {name}.{field}')
        return names, edges

    def sprites(self, packages):
        index = {}
        for package in packages:
            path = self.content / 'Win/Packages' / (package + '.pkg_manifest')
            if not path.exists():
                continue
            self.sources.append(path)
            for e in Package(path).entries():
                for s in e.get('sprites', []):
                    old = index.get(s['name'])
                    if old is None or sum(abs(v - 1) for v in s['scale_ratio']) < sum(abs(v - 1) for v in old['sprite']['scale_ratio']):
                        index[s['name']] = {'package': package, 'atlas': e['name'], 'sprite': s}
        return index


MAX_ANGLES = 64  # ActorAnimationBank::load clip angle bound


def image_names(d):
    # BookAnimation::PrepDraw 0x1802af9a6: atlas texture index =
    # GetAngleNum x NumFrames + frame (angle-major); 1-based 4-digit names
    # whenever more than one image exists.
    n = d.get('NumFrames', 1) * d.get('NumAngles', 1)
    return [d['FilePath'] + f'{i:04}' for i in range(1, n + 1)] if n > 1 else [d['FilePath']]


def classify(cov, names, index, all_index):
    banks = defaultdict(list); images = {}
    for name in names:
        d = cov.resolved(name)
        if d.get('VideoTexture'):
            banks['actor_bink'].append(name); continue
        if d.get('Type') == 'Random':
            banks['action'].append(name); continue
        if not isinstance(d.get('NumAngles', 1), int) or not 1 <= d.get('NumAngles', 1) <= MAX_ANGLES:
            banks['unsupported_multi_angle'].append(name); continue
        if 'FilePath' not in d or d.get('Type') not in ('Slide', 'Book', 'Constant', 'ConstantNoUpdate'):
            banks['unsupported_type'].append(name); continue
        rows = image_names(d)
        if all(r in index for r in rows):
            banks['action'].append(name); images.update({r: index[r] for r in rows}); continue
        if all(r in all_index for r in rows):
            banks['package_outside_scope'].append(name); continue
        banks['missing_image'].append(name)
    return banks, images


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--content', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--encounter-manifest',type=Path,help='Source-derived forced-encounter actor bank roots/provenance')
    a = p.parse_args()
    if a.output.exists():
        raise ValueError('Fresh output required')
    cov = Coverage(a.content)
    roots, scope = cov.run()
    if a.encounter_manifest:
        encounter=json.loads(a.encounter_manifest.read_text())
        if encounter.get('schema')!='hades-original-actor-animations-v1' or 'encounter_coverage'not in encounter:raise ValueError('Expected source-bound encounter actor manifest')
        extra=encounter['encounter_coverage'];cov.sources.append(a.encounter_manifest)
        for source in extra['sources']:
            path=Path(source['path'])
            if sha(path)!=source['sha256']:raise ValueError('Forced encounter source changed: '+str(path))
            cov.sources.append(path)
        for name,origins in extra['roots'].items():
            if name not in cov.anims:raise ValueError('Forced encounter resource absent original data: '+name)
            roots.setdefault(name,None)
            for origin in origins:cov.note(name,{'kind':'forced-encounter-unit-weapon-data','room':extra['room'],'encounter':extra['encounter'],'manifest':str(a.encounter_manifest),'origin':origin})
        scope['forced_encounter_asset_roots']={'room':extra['room'],'encounter':extra['encounter'],'units':extra['units'],'manifest':str(a.encounter_manifest),'root_count':len(extra['roots'])}
    names, edges = cov.closure(sorted(roots))
    while True:  # concatenation/closure fixed point
        extra = [(n, o) for n, o in cov.resolve_concat(names) if n not in roots]
        if not extra:
            break
        for n, o in extra:
            roots.setdefault(n, None); cov.note(n, o)
        names, edges = cov.closure(sorted(roots))
    packages = BASE_PACKAGES + sorted({n for n in scope['weapons'] + LOOT if (a.content / 'Win/Packages' / (n + '.pkg_manifest')).exists()})
    index = cov.sprites(packages)
    all_packages = sorted(x.stem for x in (a.content / 'Win/Packages').glob('*.pkg_manifest'))
    all_index = cov.sprites([x for x in all_packages if x not in packages]) | index
    banks, images = classify(cov, names, index, all_index)
    raw = sum(v['sprite']['rect'][2] * v['sprite']['rect'][3] * 4 for v in images.values())
    report = {
        'schema': 'hades-animation-coverage-v1', 'target_sha256': TARGET,
        'method': __doc__.strip(), 'scope': scope, 'packages': packages,
        'roots': sorted(roots), 'closure': len(names),
        'banks': {k: sorted(v) for k, v in sorted(banks.items())},
        'action_roots': sorted(n for n in banks['action']),
        'provenance': {n: cov.provenance.get(n, []) for n in sorted(names)},
        'dependencies': edges,
        'dynamic_sites': cov.dynamic_sites,
        'obstacle_graphics': {k: v[:6] for k, v in sorted(cov.obstacle_graphics.items())},
        'statistics': {'roots': len(roots), 'closure': len(names), **{k: len(v) for k, v in banks.items()},
                       'action_unique_images': len(images), 'action_raw_rgba_bytes': raw},
        'sources': sorted({str(s.resolve()): None for s in cov.sources}),
    }
    report['sources'] = [{'path': s, 'sha256': sha(s)} for s in report['sources']]
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report['statistics']))


if __name__ == '__main__':
    main()
