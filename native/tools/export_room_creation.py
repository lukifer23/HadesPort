#!/usr/bin/env python3
"""Bounded, source-linked RoomOpening creation metadata. Does not activate objects."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
from build_room_scene import Sjson, identity, merge
from hades_assets import map_objects

NATIVE_DLL_SHA256 = 'd98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254'
PLAYER_CONTROLLED_DEFAULT = {
    'value': False,
    'kind': 'compiled_native_default',
    'target_sha256': NATIVE_DLL_SHA256,
    'member': 'sgg::UnitDataDef::mPlayerControlled',
    'member_offset': '0x19',
    'full_UnitData_offset': '0x69',
    'constructor_zero_store_va': '0x1800f13e7',
    'assignment_call_va': '0x1800f1631',
    'assignment_read_va': '0x1800f1c56',
    'assignment_write_va': '0x1800f1c5a',
    'report': 'hades/.local/reports/native-player-controlled-default-recovery.json',
}


class RangedSjson(Sjson):
    """Retain exact UTF-8 byte ranges of dictionaries using the existing parser."""
    def __init__(self, text):
        super().__init__(text)
        self.ranges = {}
        self.token_start = 0

    def token(self):
        self.skip()
        self.token_start = self.pos
        return super().token()

    def value(self, t=None):
        if t is None:
            t = self.token()
        start = self.token_start
        result = super().value(t)
        if isinstance(result, dict):
            begin = len(self.text[:start].encode('utf-8'))
            end = len(self.text[:self.pos].encode('utf-8'))
            self.ranges[id(result)] = [begin, end - begin]
        return result


def leaf_paths(value, prefix=()):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from leaf_paths(child, prefix + (key,))
    else:
        yield '.'.join(prefix)


def export(content, output, room):
    if output.exists():
        raise ValueError('Output must be new')
    native_identity = identity(content.parent / 'x64/EngineWin64s.dll')
    if native_identity['sha256'] != NATIVE_DLL_SHA256:
        raise ValueError('Compiled defaults require exact matching native binary')
    namespaces = {}
    sources = []
    for category in ('Obstacles', 'Units'):
        table = {}
        for path in sorted((content / 'Game' / category).glob('*.sjson')):
            raw = path.read_bytes()
            # Preserve BOM in text so recorded UTF-8 byte offsets remain exact.
            text = raw.decode('utf-8')
            parser = RangedSjson(text)
            if text.startswith('\ufeff'):
                parser.pos = 1
            data = parser.parse()
            source = identity(path)
            sources.append(source)
            for index, definition in enumerate(data.get(category, [])):
                name = definition['Name']
                if name in table:
                    raise ValueError(f'Duplicate {category} name: {name}')
                span = parser.ranges[id(definition)]
                fragment = raw[span[0]:sum(span)]
                if Sjson(fragment.decode('utf-8')).parse() != definition:
                    raise ValueError('Definition range does not round-trip')
                table[name] = {'value': definition, 'source': {
                    'path': source['path'], 'file_sha256': source['sha256'],
                    'definition_index': index, 'source_range': span,
                    'range_sha256': hashlib.sha256(fragment).hexdigest()}}
        namespaces[category] = table

    resolved = {}
    def resolve(category, name, stack=()):
        key = category + '/' + name
        if key in resolved:
            return resolved[key]
        if name in stack:
            raise ValueError('Inheritance cycle: ' + repr(stack + (name,)))
        own = namespaces[category][name]
        parent = own['value'].get('InheritFrom')
        if parent is not None and not isinstance(parent, str):
            raise ValueError('Unsupported inheritance shape')
        base = resolve(category, parent, stack + (name,)) if parent else None
        value = merge(base['value'], own['value']) if base else own['value']
        provenance = dict(base['property_provenance']) if base else {}
        for leaf in leaf_paths(own['value']):
            # Replacing an array/scalar also removes any inherited sub-properties.
            for old in list(provenance):
                if old.startswith(leaf + '.'):
                    del provenance[old]
            provenance[leaf] = key
        result = {'key': key, 'value': value, 'source': own['source'],
                  'inheritance': ([key] + base['inheritance']) if base else [key],
                  'property_provenance': provenance}
        resolved[key] = result
        return result

    map_path = content / 'Win/Maps' / (room + '.thing_bin')
    raw = map_path.read_bytes()
    objects = map_objects(map_path)
    ids = {record['Id']: record for record in objects}
    if len(ids) != len(objects):
        raise ValueError('Duplicate map IDs')
    records = []
    edge_errors = []
    depths = collections.Counter()
    used = set()
    for ordinal, obj in enumerate(objects):
        category = {1: 'Obstacles', 2: 'Units'}.get(obj['DataType'])
        if category is None:
            raise ValueError('Unknown native DataType')
        definition = resolve(category, obj['Name'])
        used.update(definition['inheritance'])
        thing = definition['value'].get('Thing', {})
        trigger = thing.get('TriggerOnSpawn', True)
        trigger_source = definition['property_provenance'].get('Thing.TriggerOnSpawn')
        player_controlled = None
        player_source_value = definition['value'].get('PlayerControlled') if category == 'Units' else None
        player_provenance = None
        if category == 'Units':
            if 'PlayerControlled' in definition['value']:
                if not isinstance(player_source_value, bool):
                    raise ValueError('Non-boolean source PlayerControlled')
                player_controlled = player_source_value
                player_provenance = {'kind': 'resolved_source',
                    'definition_key': definition['property_provenance']['PlayerControlled']}
            else:
                player_controlled = PLAYER_CONTROLLED_DEFAULT['value']
                player_provenance = dict(PLAYER_CONTROLLED_DEFAULT)
        depth = 0
        ancestor = obj
        ancestry = [obj['Id']]
        while ancestor['AttachToID'] > 0:
            parent_id = ancestor['AttachToID']
            if parent_id in ancestry:
                raise ValueError('Attachment cycle')
            if parent_id not in ids:
                edge_errors.append({'id': obj['Id'], 'missing_parent': parent_id})
                break
            ancestor = ids[parent_id]
            ancestry.append(parent_id)
            depth += 1
        depths[depth] += 1
        for child in obj['AttachedIDs']:
            if child not in ids or ids[child]['AttachToID'] != obj['Id']:
                edge_errors.append({'id': obj['Id'], 'inconsistent_child': child})
        if obj['AttachToID'] > 0 and obj['Id'] not in ids[obj['AttachToID']]['AttachedIDs']:
            edge_errors.append({'id': obj['Id'], 'missing_reverse_edge': True})
        span = obj['source_range']
        if span[0] < 12 or sum(span) > len(raw):
            raise ValueError('Map range outside source')
        # Field order is established by matching v12 native loader. All this map's
        # records have presence=true, so these first three fields always exist.
        fields = {'DoCreate': [span[0], 1]}
        if obj['DoCreate']:
            fields.update({'ActivateAtRange': [span[0] + 1, 1],
                           'ActivationRange': [span[0] + 2, 4],
                           'Active': [span[0] + 6, 1]})
        gate = ('attached_child' if obj['AttachToID'] > 0 else
                'inactive' if not obj['Active'] else
                'range_pending_runtime_state' if obj['ActivateAtRange'] else
                'direct_top_level')
        record = {
            'ordinal': ordinal, 'id': obj['Id'], 'name': obj['Name'],
            'definition_key': definition['key'], 'data_type': obj['DataType'],
            'native_identity': 'unit' if category == 'Units' else 'obstacle',
            'player_controlled': player_controlled,
            'player_controlled_source_value': player_source_value,
            'player_controlled_provenance': player_provenance,
            'source_range': span, 'source_field_ranges': fields,
            'range_sha256': hashlib.sha256(raw[span[0]:sum(span)]).hexdigest(),
            'raw_map': obj, 'creation_classification': gate,
            'attachment_ancestry_self_first': ancestry,
            'trigger_on_spawn': {'value': trigger,
                'source': trigger_source or 'compiled_ThingData_constructor_default',
                'caller_can_suppress': True},
            'resolved_thing': thing,
            'scale_inputs': {'map_scale': obj['Scale'],
                'definition_scale': thing.get('Scale'),
                'definition_min_scale': thing.get('MinScale'),
                'definition_max_scale': thing.get('MaxScale')},
            'geometry_inputs': {'map_points': obj['Points'],
                'definition_points': thing.get('Points'), 'definition_size': thing.get('Size'),
                'rotate_geometry': thing.get('RotateGeometry'),
                'mirror_geometry': thing.get('MirrorGeometry'),
                'map_collision': obj['Collision'], 'stops_units': thing.get('StopsUnits')},
        }
        records.append(record)
    # Include anchor definitions even when no matching placement needs them.
    for name in ('BlankObstacle', 'InvisibleTarget'):
        used.update(resolve('Obstacles', name)['inheritance'])
    counts = {
        'records': len(records), 'do_create_true': sum(o['DoCreate'] for o in objects),
        'do_create_false': sum(not o['DoCreate'] for o in objects),
        'active': sum(o['Active'] for o in objects),
        'inactive': sum(not o['Active'] for o in objects),
        'activate_at_range': sum(o['ActivateAtRange'] for o in objects),
        'attached_records': sum(o['AttachToID'] > 0 for o in objects),
        'attachment_parent_lists': sum(bool(o['AttachedIDs']) for o in objects),
        'attachment_edges': sum(len(o['AttachedIDs']) for o in objects),
        'attachment_depths': dict(depths),
        'classification': dict(collections.Counter(r['creation_classification'] for r in records)),
        'identity': dict(collections.Counter(r['native_identity'] for r in records)),
        'player_controlled_units': dict(collections.Counter(str(r['player_controlled']).lower() for r in records if r['native_identity'] == 'unit')),
        'trigger_on_spawn': dict(collections.Counter(str(r['trigger_on_spawn']['value']).lower() for r in records)),
        'resolved_definitions_including_ancestors_and_anchors': len(used),
    }
    result = {'schema': 'hades-room-creation-v1', 'room': room,
        'map_source': identity(map_path), 'definition_sources': sources,
        'native_target': native_identity,
        'compiled_defaults': {'UnitDataDef.PlayerControlled': PLAYER_CONTROLLED_DEFAULT},
        'counts': counts, 'attachment_errors': edge_errors,
        'records': records, 'definitions': {k: resolved[k] for k in sorted(used)},
        'semantics': {
            'DoCreate': 'v12 serialized payload presence, not Active; false still pushes default MapThing',
            'DataType': {'1': 'ObstacleManager::Create', '2': 'GetUnitData then PlayerControlled selects player/enemy'},
            'TriggerOnSpawn_default': {'value': True, 'ctor_va': '0x1800eb822',
                'full_ThingData_offset': '0x370', 'ThingDataDef_offset': '0x368'},
            'activation': 'Classification only. Attached children are skipped by top-level loop; runtime gate/caller handling retained separately.',
        },
        'limitations': [
            'Derived static metadata; does not execute original game or claim objects are alive.',
            'Inheritance uses nested dictionary overrides; complete original definition loading/merging runtime is not reconstructed.',
            'Only TriggerOnSpawn and UnitDataDef.PlayerControlled have explicit compiled defaults here. Other missing fields stay null/absent, not guessed.',
            'Scale/geometry inputs are preserved separately; final native component transforms are not inferred here.',
            'Initial world frame, existing player handles, unit-manager callback flags, and dynamic range activation remain separate runtime work.',
        ]}
    if edge_errors:
        raise ValueError('Invalid attachment graph: ' + repr(edge_errors))
    output.mkdir(parents=True)
    (output / 'creation.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'output': str(output / 'creation.json'), 'counts': counts}))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--room', default='RoomOpening')
    args = parser.parse_args()
    export(args.content, args.output, args.room)
