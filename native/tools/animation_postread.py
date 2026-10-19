"""Reviewed native AnimationData.PostRead type/frame normalization."""
import copy


def normalize_animation_postread(definition):
    """Apply the bounded Constant/ConstantNoUpdate branches after source Read.

    Exact EngineWin64s PostRead97550: full Type+bb ==3 enters97819;
    NumFrames+310 >1 or HasDuration98880 promotes it to Constant2 at97832.
    An already Constant2 record instead forces NumFrames=EndFrame=1 at97800.
    Constructor default Type3 is not guessed from motion/child properties.
    Book/Slide/Random frame-list branches remain the existing exporters' scope.
    """
    result = copy.deepcopy(definition)
    kind = result.get('Type', 'ConstantNoUpdate')
    if kind == 'Constant':
        result['NumFrames'] = result['EndFrame'] = 1
    elif kind == 'ConstantNoUpdate':
        has_duration = any(result.get(key, 0) > 0 for key in
                           ('Duration', 'DurationFrames', 'RandomDurationMin', 'RandomDurationMax'))
        if result.get('NumFrames', 1) > 1 or has_duration:
            kind = 'Constant'
    result['Type'] = kind
    return result
