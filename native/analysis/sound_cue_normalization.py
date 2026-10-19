"""Typed source SoundCue normalization from the exact native deserializer.

This resolves names and known-invalid sentinels only. Named audio event validity
requires a real event-description lookup; unknown validity stays None.
"""
from typing import Any

EMPTY_CUE_NAME = '/EmptyCue'


def normalize_sound_cue_source(value: str) -> dict[str, Any]:
    """Normalize an explicit string override, never an omitted default field.

    SGD_DeserializeSoundCue compares lowercase 'null' including its terminator.
    GetSoundCue otherwise adds '/' unless already absolute or GUID-prefixed.
    A JSON null value is not evidence of that original string sentinel.
    """
    if not isinstance(value, str):
        raise ValueError('Native SoundCue string override requires a string; JSON null is not the literal string sentinel')
    if '\0' in value:
        raise ValueError('Embedded-NUL SoundCue source requires binary string-boundary recovery')
    if value == 'null':
        return {'owner': None, 'name': EMPTY_CUE_NAME, 'valid': False,
                'validity_resolution': 'native_invalid_cue_sentinel'}
    name = value if not value or value[0] in '/{' else '/' + value
    if name == EMPTY_CUE_NAME:
        return {'owner': None, 'name': name, 'valid': False,
                'validity_resolution': 'native_empty_cue_cache_entry'}
    return {'owner': None, 'name': name, 'valid': None,
            'validity_resolution': 'requires_original_event_description_lookup'}


def compiled_empty_sound_cue() -> dict[str, Any]:
    """Verified inline WeaponData defaults, separate from source deserialization.

    Callers must retain the per-field constructor provenance from
    native-soundcue-compiled-defaults.json. This is not a universal constructor
    assertion for every unrelated SoundCue allocation.
    """
    return {'owner': None, 'name': '', 'valid': False,
            'validity_resolution': 'verified_weapon_inline_compiled_default'}


def validate_normalization() -> None:
    assert compiled_empty_sound_cue()['name'] == ''
    assert normalize_sound_cue_source('null')['name'] == '/EmptyCue'
    assert normalize_sound_cue_source('null')['valid'] is False
    assert normalize_sound_cue_source('EmptyCue')['valid'] is False
    assert normalize_sound_cue_source('/EmptyCue')['valid'] is False
    assert normalize_sound_cue_source('Null')['name'] == '/Null'
    assert normalize_sound_cue_source('Null')['valid'] is None
    assert normalize_sound_cue_source('/null')['name'] == '/null'
    assert normalize_sound_cue_source('/null')['valid'] is None
    assert normalize_sound_cue_source('')['name'] == ''
    assert normalize_sound_cue_source('')['valid'] is None
    assert normalize_sound_cue_source('SFX/Test')['name'] == '/SFX/Test'
    assert normalize_sound_cue_source('/SFX/Test')['name'] == '/SFX/Test'
    assert normalize_sound_cue_source('{GUID}')['name'] == '{GUID}'
    try:
        normalize_sound_cue_source(None)
    except ValueError:
        pass
    else:
        raise AssertionError('JSON null was conflated with source string null')


if __name__ == '__main__':
    validate_normalization()
    print('SOUNDCUE_NORMALIZATION_OK compiled_empty literal_null slash_prefix guid_prefix unresolved_named_validity')
