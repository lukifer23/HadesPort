#!/usr/bin/env python3
"""Recover sgg::Controls default bindings from EngineWin64s.dll static instructions.

Never executes game code. Disassembles selected PDB procedures with llvm-objdump and
performs a linear, conservative scan for writes into the 29 sgg::Controls
RemappableControl globals (field offsets from the PDB RemappableControl field list):
  0x10 Enabled, 0x11 RequiresShift, 0x12 UseGamepadButton, 0x13 DefaultUseGamepadButton,
  0x14 AllowMouseDoubleMapping, 0x15 AllowGamepadDoubleMapping,
  0x16 DefaultGamepadButtonUnmapped, 0x18 TextSymbol, 0x30 ControlName,
  0x48 MouseControlName, 0x60 KeyName, 0x78 OrKeys (vector<KeyboardButtonId>),
  0x90 DefaultOrKeys, 0xa8 GamepadButton, 0xac DefaultGamepadButton,
  0xb0 MouseButton, 0xb4 DefaultMouseButton.
Inlined RemappableControl::AddKey appears as push_back (0x18004f210) on OrKeys with the
key in a stack slot. Every event keeps its instruction address.
"""
from pathlib import Path
import argparse, json, re, struct, subprocess, hashlib, os

ROOT = Path(__file__).resolve().parents[2]
DLL = ROOT / '.local/game/base/x64/EngineWin64s.dll'
PDB = ROOT / '.local/reports/native-engine-pdb.json'
LLVM = Path(os.environ.get('LLVM_OBJDUMP') or Path(os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT') or Path.home()/'Library/Android/sdk')/'ndk/29.0.14206865/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-objdump')
PUSH_BACK = 0x18004f210
STRING_ASSIGN = 0x18003a7c0
VECTOR_ASSIGN = 0x180142210
FIELDS = {0x10: 'Enabled', 0x11: 'RequiresShift', 0x12: 'UseGamepadButton', 0x13: 'DefaultUseGamepadButton',
          0x14: 'AllowMouseDoubleMapping', 0x15: 'AllowGamepadDoubleMapping', 0x16: 'DefaultGamepadButtonUnmapped',
          0x18: 'TextSymbol', 0x30: 'ControlName', 0x48: 'MouseControlName', 0x60: 'KeyName',
          0x78: 'OrKeys', 0x90: 'DefaultOrKeys', 0xa8: 'GamepadButton', 0xac: 'DefaultGamepadButton',
          0xb0: 'MouseButton', 0xb4: 'DefaultMouseButton', 0x08: 'Index', 0x0c: 'LastMappingTime'}
SIZE = 184
# PDB RemappableControl::SetupScheme overloads; body semantics from their disassembly:
#  0x180199d20: (name, r8d) -> GamepadButton=r8d, UseGamepadButton=1
#  0x180199dd0: (name, r8d) -> MouseButton=r8d
#  0x180199e80: (name, r8d, r9d) -> MouseButton=r8d, GamepadButton=r9d, UseGamepadButton=1
SETUP_SCHEME = {0x180199d20: '(name,GamepadButton)', 0x180199dd0: '(name,MouseButton)', 0x180199e80: '(name,MouseButton,GamepadButton)'}
FUNCS = ['sgg::Controls::SetCommonDefaults', 'sgg::Controls::SetKeyboardMoveDefaults',
         'sgg::Controls::SetupControlScheme', 'sgg::Controls::AddControls', 'sgg::Controls::InitializeControls']
REG32 = {'rax': 'eax', 'rbx': 'ebx', 'rcx': 'ecx', 'rdx': 'edx', 'rsi': 'esi', 'rdi': 'edi', 'rbp': 'ebp',
         'r8': 'r8d', 'r9': 'r9d', 'r10': 'r10d', 'r11': 'r11d', 'r12': 'r12d', 'r13': 'r13d', 'r14': 'r14d', 'r15': 'r15d'}
R8 = {'al': 'eax', 'bl': 'ebx', 'cl': 'ecx', 'dl': 'edx', 'sil': 'esi', 'dil': 'edi', 'r8b': 'r8d', 'r9b': 'r9d', 'r14b': 'r14d', 'r15b': 'r15d', 'r12b': 'r12d', 'r13b': 'r13d'}


def norm(r):
    r = r.lstrip('%')
    return REG32.get(r, R8.get(r, r))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--output', required=True); ap.add_argument('--final'); args = ap.parse_args()
    data = DLL.read_bytes()
    pe = struct.unpack_from('<I', data, 60)[0]; nsec = struct.unpack_from('<H', data, pe + 6)[0]
    opt = struct.unpack_from('<H', data, pe + 20)[0]; base = struct.unpack_from('<Q', data, pe + 48)[0]
    secs = []
    for i in range(nsec):
        off = pe + 24 + opt + i * 40; vs, rva, rs, fo = struct.unpack_from('<IIII', data, off + 8); secs.append((rva, rs, fo))

    def cstr(va):
        r = va - base
        for rv, sz, fo in secs:
            if rv <= r < rv + sz:
                p = fo + r - rv; e = data.find(b'\0', p, p + 256)
                b = data[p:e]
                if b and all(32 <= x < 127 for x in b): return b.decode()
        return None

    pdb = json.loads(PDB.read_text())
    controls = {}
    for s in pdb['globals'] + pdb['module_symbols']:
        if s.get('kind') == 'data' and s.get('rva') and s['name'].startswith('sgg::Controls::'):
            va = s['rva'] + base
            if va >= 0x181ca0000: controls[va] = s['name'].split('::')[-1]
    controls.pop(next((a for a, n in controls.items() if n == 'ControlsExtension'), None), None)
    procs = {s['name']: s for s in pdb['module_symbols'] if s['kind'] == 'procedure' and s['rva'] is not None and s['name'] in FUNCS}

    def ctl(va):
        for a, n in controls.items():
            if a <= va < a + SIZE: return n, va - a
        return None

    report = {'dll_sha256': hashlib.sha256(data).hexdigest(), 'pdb_identity': pdb['identity'],
              'controls': {n: hex(a) for a, n in sorted(controls.items())}, 'functions': {}}
    for fname in FUNCS:
        p = procs[fname]; start = base + p['rva']; end = start + p['length']
        asm = subprocess.check_output([str(LLVM), '--disassemble', f'--start-address={hex(start)}', f'--stop-address={hex(end)}', str(DLL)], text=True)
        regs, slots, events, branches, calls = {}, {}, [], [], []
        last_lea = {}
        last_string = None
        for line in asm.splitlines():
            m = re.match(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\S+)\s*(.*)$', line)
            if not m: continue
            addr, op, rest = int(m.group(1), 16), m.group(2), m.group(3)
            ops = rest.split('#')[0].strip()
            cmt = rest.split('#', 1)[1].strip() if '#' in rest else ''
            target = None
            tm = re.search(r'0x([0-9a-f]{9,})', cmt) or re.search(r'^0x([0-9a-f]{9,})', ops)
            if tm: target = int(tm.group(1), 16)
            parts = [x.strip() for x in re.split(r',(?![^(]*\))', ops)] if ops else []
            if op.startswith('xor') and len(parts) == 2 and parts[0] == parts[1]:
                regs[norm(parts[1])] = 0; continue
            if op in ('movl', 'movb', 'movq') and len(parts) == 2:
                src, dst = parts
                val = int(src[1:], 16) if src.startswith('$') else None
                if val is None and src.startswith('%'): val = regs.get(norm(src))
                if op == 'movl' and val is not None and val > 0x7fffffff: val -= 1 << 32
                if '(%rip)' in dst and target is not None:
                    c = ctl(target)
                    if c:
                        events.append({'address': hex(addr), 'control': c[0], 'field': FIELDS.get(c[1], hex(c[1])), 'value': val,
                                       'source': src, 'instruction': f'{op} {ops}'})
                    continue
                if re.match(r'^-?0x[0-9a-f]+\(%r[bs]p\)$', dst):
                    slots[dst] = val; continue
                if dst.startswith('%'):
                    regs[norm(dst)] = val
                continue
            if op in ('leaq', 'leal') and len(parts) == 2:
                dst = norm(parts[1])
                if '(%rip)' in parts[0] and target is not None:
                    last_lea[dst] = ('global', target)
                    lit = cstr(target)
                    if lit and not lit.startswith('{'): last_string = lit
                    regs.pop(dst, None)
                else:
                    last_lea[dst] = ('stack', parts[0])
                    mm = re.match(r'^(-?0x[0-9a-f]+)?\((%\w+)\)$', parts[0])
                    baseval = regs.get(norm(mm.group(2))) if mm else None
                    if mm and baseval is not None and not mm.group(2) in ('%rbp', '%rsp'):
                        regs[dst] = baseval + (int(mm.group(1), 16) if mm.group(1) else 0)
                    else:
                        regs.pop(dst, None)
                continue
            if op.startswith('j') and op != 'jmp' and target is not None:
                if not (start <= target < end) or abs(target - addr) > 0x40:
                    branches.append({'address': hex(addr), 'instruction': f'{op} {ops}'})
            if op == 'callq' and target is not None:
                rc = last_lea.get('ecx'); rd = last_lea.get('edx')
                cinfo = ctl(rc[1]) if rc and rc[0] == 'global' else None
                if target == PUSH_BACK and cinfo and cinfo[1] == 0x78 and rd and rd[0] == 'stack':
                    events.append({'address': hex(addr), 'control': cinfo[0], 'field': 'OrKeys.push_back(AddKey)',
                                   'value': slots.get(rd[1]), 'source': rd[1], 'instruction': 'callq 0x18004f210 eastl::vector<KeyboardButtonId>::push_back'})
                elif target == STRING_ASSIGN and cinfo:
                    s = cstr(rd[1]) if rd and rd[0] == 'global' else None
                    if s is None and cinfo[1] == 0x30: s = last_string
                    events.append({'address': hex(addr), 'control': cinfo[0], 'field': FIELDS.get(cinfo[1], hex(cinfo[1])) + '.assign',
                                   'value': s, 'source': hex(rd[1]) if rd and rd[0] == 'global' else 'computed', 'instruction': 'callq 0x18003a7c0 eastl::string::assign'})
                elif target == VECTOR_ASSIGN and cinfo:
                    events.append({'address': hex(addr), 'control': cinfo[0], 'field': FIELDS.get(cinfo[1], hex(cinfo[1])) + '.assign(OrKeys)',
                                   'value': None, 'source': 'OrKeys', 'instruction': 'callq 0x180142210 vector assign'})
                else:
                    rec = {'address': hex(addr), 'target': hex(target), 'rcx': (hex(rc[1]) if rc and rc[0] == 'global' else None),
                           'rcx_control': cinfo[0] if cinfo else None}
                    if target in SETUP_SCHEME:
                        rec['overload'] = SETUP_SCHEME[target]
                        rec['r8d'] = regs.get('r8d'); rec['r9d'] = regs.get('r9d')
                        events.append({'address': hex(addr), 'control': cinfo[0] if cinfo else None, 'field': 'SetupScheme' + SETUP_SCHEME[target],
                                       'value': {'r8d': regs.get('r8d'), 'r9d': regs.get('r9d')}, 'source': 'call', 'control_name': last_string, 'instruction': f'callq {hex(target)} RemappableControl::SetupScheme'})
                    calls.append(rec)
                for r in ('eax', 'ecx', 'edx', 'r8d', 'r9d', 'r10d', 'r11d'): regs.pop(r, None)
                last_lea = {}
        report['functions'][fname] = {'va': hex(start), 'length': p['length'], 'events': events, 'calls': calls, 'external_branches': branches}
    Path(args.output).write_text(json.dumps(report, indent=1) + '\n')
    for f, r in report['functions'].items():
        print('==', f, r['va'], 'events', len(r['events']), 'calls', len(r['calls']), 'branches', len(r['external_branches']))
    if args.final:
        Path(args.final).write_text(json.dumps(simulate(report), indent=1) + '\n')


def simulate(report):
    """Replay the recovered call order: App::Initialize -> InitializeControls -> SetupControlScheme
    (inline/SetupScheme gamepad+mouse) -> AddControls -> tail jmp SetKeyboardMoveDefaults, which first
    calls SetCommonDefaults (loop over Scheme: OrKeys cleared, MouseButton=-1) and then adds keys."""
    state = {n: {'controlName': None, 'gamepadButton': None, 'useGamepadButton': False, 'defaultGamepadButtonUnmapped': False,
                 'allowGamepadDoubleMapping': False, 'mouseButton': None, 'keys': [], 'provenance': []} for n in report['controls']}
    def run(fname):
        for e in report['functions'][fname]['events']:
            c = e['control']; s = state.get(c)
            if s is None: continue
            f = e['field']; v = e['value']; prov = {'function': fname, 'address': e['address'], 'instruction': e['instruction'], 'field': f, 'value': v}
            if f == 'ControlName.assign': s['controlName'] = v
            elif f == 'GamepadButton': s['gamepadButton'] = v; s['provenance'].append(prov)
            elif f == 'UseGamepadButton': s['useGamepadButton'] = bool(v); s['provenance'].append(prov)
            elif f == 'DefaultGamepadButtonUnmapped': s['defaultGamepadButtonUnmapped'] = bool(v); s['provenance'].append(prov)
            elif f == 'AllowGamepadDoubleMapping': s['allowGamepadDoubleMapping'] = bool(v); s['provenance'].append(prov)
            elif f == 'MouseButton': s['mouseButton'] = v; s['provenance'].append(prov)
            elif f == 'OrKeys.push_back(AddKey)': s['keys'].append(v); s['provenance'].append(prov)
            elif f.startswith('SetupScheme'):
                s['controlName'] = e.get('control_name')
                if f == 'SetupScheme(name,GamepadButton)': s['gamepadButton'] = v['r8d']; s['useGamepadButton'] = True
                elif f == 'SetupScheme(name,MouseButton)': s['mouseButton'] = v['r8d']
                else: s['mouseButton'] = v['r8d']; s['gamepadButton'] = v['r9d']; s['useGamepadButton'] = True
                s['provenance'].append(prov)
    run('sgg::Controls::SetupControlScheme')
    for s in state.values():  # SetCommonDefaults 0x18018d720..0x18018d76f loop over Controls::Scheme
        s['keys'] = []; s['mouseButton'] = -1
        s['provenance'].append({'function': 'sgg::Controls::SetCommonDefaults', 'address': '0x18018d728',
                                'instruction': 'movq %rax,0x80(%rcx) (OrKeys.end=begin); 0x18018d72f movl $0xffffffff,0xb0(%rcx)', 'field': 'reset', 'value': None})
    run('sgg::Controls::SetCommonDefaults')
    run('sgg::Controls::SetKeyboardMoveDefaults')
    return state


if __name__ == '__main__':
    main()
