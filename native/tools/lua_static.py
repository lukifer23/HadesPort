"""Static Lua 5.2 lexer, function index and data-table reader for original scripts.

Read-only analysis of the original game scripts. Nothing here executes Lua.
Used by animation_coverage.py to derive reachable animation names with exact
file:line provenance instead of hand-maintained name lists.
"""
import re
from pathlib import Path

KEYWORDS = {'and','break','do','else','elseif','end','false','for','function','goto','if','in',
            'local','nil','not','or','repeat','return','then','true','until','while'}
_NAME = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')
_NUM = re.compile(r'0[xX][0-9a-fA-F.]+(?:[pP][+-]?\d+)?|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?')
_OPS = ['...', '..', '==', '~=', '<=', '>=', '::', '<<', '>>', '//']
_ESC = {'n': '\n', 't': '\t', 'r': '\r', 'a': '\a', 'b': '\b', 'f': '\f', 'v': '\v', '\\': '\\', '"': '"', "'": "'", '\n': '\n'}


class Token:
    __slots__ = ('kind', 'value', 'line')

    def __init__(self, kind, value, line):
        self.kind, self.value, self.line = kind, value, line

    def __repr__(self):
        return f'{self.kind}:{self.value!r}@{self.line}'


def _long_bracket(text, i):
    """Return (level, start_of_content) for [[ / [==[ at i, else None."""
    if text[i] != '[':
        return None
    j = i + 1
    while j < len(text) and text[j] == '=':
        j += 1
    if j < len(text) and text[j] == '[':
        return j - i - 1, j + 1
    return None


def tokenize(text):
    out = []
    i, line, n = 0, 1, len(text)
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1; i += 1; continue
        if c in ' \t\r\f\v':
            i += 1; continue
        if text.startswith('--', i):
            lb = _long_bracket(text, i + 2) if i + 2 < n else None
            if lb:
                close = ']' + '=' * lb[0] + ']'
                end = text.find(close, lb[1])
                end = n if end < 0 else end + len(close)
                line += text.count('\n', i, end); i = end
            else:
                end = text.find('\n', i)
                i = n if end < 0 else end
            continue
        if c == '[':
            lb = _long_bracket(text, i)
            if lb:
                close = ']' + '=' * lb[0] + ']'
                end = text.find(close, lb[1])
                if end < 0:
                    raise ValueError(f'Unterminated long string at line {line}')
                value = text[lb[1]:end]
                if value.startswith('\n'):
                    value = value[1:]
                out.append(Token('str', value, line))
                line += text.count('\n', i, end + len(close)); i = end + len(close)
                continue
        if c in '"\'':
            j, buf, start = i + 1, [], line
            while True:
                if j >= n:
                    raise ValueError(f'Unterminated string at line {start}')
                d = text[j]
                if d == c:
                    j += 1; break
                if d == '\n':
                    raise ValueError(f'Newline in string at line {start}')
                if d == '\\':
                    e = text[j + 1]
                    if e in _ESC:
                        buf.append(_ESC[e]); j += 2
                        if e == '\n':
                            line += 1
                    elif e.isdigit():
                        k = j + 1
                        while k < j + 4 and k < n and text[k].isdigit():
                            k += 1
                        buf.append(chr(int(text[j + 1:k]))); j = k
                    elif e == 'x':
                        buf.append(chr(int(text[j + 2:j + 4], 16))); j += 4
                    elif e == 'z':
                        j += 2
                        while j < n and text[j] in ' \t\r\n':
                            line += text[j] == '\n'; j += 1
                    else:
                        raise ValueError(f'Invalid escape at line {line}')
                    continue
                buf.append(d); j += 1
            out.append(Token('str', ''.join(buf), start)); i = j; continue
        m = _NAME.match(text, i)
        if m:
            w = m.group(0)
            out.append(Token('kw' if w in KEYWORDS else 'name', w, line)); i = m.end(); continue
        if c.isdigit() or (c == '.' and i + 1 < n and text[i + 1].isdigit()):
            m = _NUM.match(text, i)
            v = m.group(0)
            out.append(Token('num', float.fromhex(v) if v[:2].lower() == '0x' and ('.' in v or 'p' in v.lower()) else (int(v, 16) if v[:2].lower() == '0x' else float(v)), line))
            i = m.end(); continue
        for op in _OPS:
            if text.startswith(op, i):
                out.append(Token('op', op, line)); i += len(op); break
        else:
            out.append(Token('op', c, line)); i += 1
    out.append(Token('eof', None, line))
    return out


class LuaFile:
    """Tokens plus a block-structured function index for one original script."""

    def __init__(self, path):
        self.path = Path(path)
        self.tokens = tokenize(self.path.read_text(encoding='utf-8-sig', errors='strict'))
        self.functions = []  # dicts: name, start, end (token indices), line
        self._index()
        self.registrations = self._registrations()

    def _registrations(self):
        """Index anonymous callbacks passed to top-level OnEvent{...} calls.

        Give the outer callback a stable owner so its literals/calls participate
        in the same reachability graph as named functions. This only identifies
        registrations; callers decide which native events/selectors are in scope.
        """
        t = self.tokens
        inside = set()
        for rec in self.functions:
            inside.update(range(rec['start'], rec['end'] + 1))
        out = []
        for i, tok in enumerate(t[:-1]):
            if i in inside or tok.kind != 'name' or not re.fullmatch(r'On[A-Z]\w*', tok.value) or t[i + 1].value != '{':
                continue
            depth, j = 1, i + 2
            while depth:
                if t[j].kind == 'eof':
                    raise ValueError(f'{self.path.name}: unclosed registration at {tok.line}')
                if t[j].value == '{': depth += 1
                elif t[j].value == '}': depth -= 1
                j += 1
            callbacks = [r for r in self.functions if i < r['start'] < r['end'] < j and r['name'] is None]
            for rec in callbacks:
                if any(parent['start'] < rec['start'] < parent['end'] for parent in callbacks):
                    continue
                rec['name'] = f'@{self.path.name}:{tok.line}:{tok.value}:{rec["line"]}'
                selector = t[i + 2].value if t[i + 2].kind == 'str' else None
                entry = {'event': tok.value, 'selector': selector, 'name': rec['name'], 'line': tok.line}
                out.append(entry)
        return out

    def _index(self):
        t = self.tokens
        stack = []  # (kind, function-record-or-None)
        for i, tok in enumerate(t):
            if tok.kind != 'kw':
                continue
            v = tok.value
            if v == 'function':
                stack.append(('function', self._function_record(i)))
            elif v in ('if', 'do', 'repeat'):
                # 'for'/'while' open through their 'do'.
                stack.append((v, None))
            elif v in ('end', 'until'):
                if not stack:
                    raise ValueError(f'{self.path.name}: unbalanced {v} at line {tok.line}')
                kind, rec = stack.pop()
                if (v == 'until') != (kind == 'repeat'):
                    raise ValueError(f'{self.path.name}: mismatched {v} at line {tok.line}')
                if rec is not None:
                    rec['end'] = i
                    self.functions.append(rec)
        if stack:
            raise ValueError(f'{self.path.name}: unclosed block')

    def _function_record(self, i):
        t = self.tokens
        name = None
        j = i + 1
        if t[j].kind == 'name':  # function A.B:C(
            parts = [t[j].value]; j += 1
            while t[j].kind == 'op' and t[j].value in ('.', ':') and t[j + 1].kind == 'name':
                parts.append(t[j + 1].value); j += 2
            name = parts[-1] if len(parts) == 1 else '.'.join(parts)
            if len(parts) > 1:
                name = parts[-1] if parts[0] in ('self',) else '.'.join(parts)
        elif i >= 2 and t[i - 1].kind == 'op' and t[i - 1].value == '=' and t[i - 2].kind == 'name':
            # X = function(  /  local X = function(  /  { Key = function(
            name = t[i - 2].value
            anon_field = i >= 3 and t[i - 3].kind == 'op' and t[i - 3].value in ('{', ',', ';')
            if anon_field:
                name = None  # table member: attributed to the enclosing scope
        return {'name': name, 'start': i, 'end': None, 'line': t[i].line}

    def owners(self):
        """Map token index -> innermost NAMED function name (None = top level)."""
        owner = [None] * len(self.tokens)
        for rec in sorted(self.functions, key=lambda r: -(r['end'] - r['start'])):
            if rec['name'] is None:
                continue
            for k in range(rec['start'], rec['end'] + 1):
                owner[k] = rec['name']
        return owner


class _DataReader:
    """Evaluate literal table constructors (strings, numbers, booleans, nested
    tables). Non-literal expressions are kept as {'__expr__': source tokens}."""

    def __init__(self, tokens):
        self.t = tokens

    def value(self, i):
        tok = self.t[i]
        if tok.kind == 'op' and tok.value == '{':
            return self.table(i)
        # Collect one expression up to a top-level separator.
        j, depth, parts = i, 0, []
        while True:
            tk = self.t[j]
            if tk.kind == 'eof':
                break
            if tk.kind == 'op' and tk.value in ('(', '{', '['):
                depth += 1
            elif tk.kind == 'op' and tk.value in (')', '}', ']'):
                if depth == 0:
                    break
                depth -= 1
            elif depth == 0 and tk.kind == 'op' and tk.value in (',', ';'):
                break
            parts.append(tk); j += 1
        if len(parts) == 1:
            p = parts[0]
            if p.kind in ('str', 'num'):
                return p.value, j
            if p.kind == 'kw' and p.value in ('true', 'false'):
                return p.value == 'true', j
            if p.kind == 'kw' and p.value == 'nil':
                return None, j
        if parts and parts[0].kind == 'kw' and parts[0].value == 'function':
            # Skip a function body: balance block keywords.
            depth, k = 0, i
            while True:
                tk = self.t[k]
                if tk.kind == 'kw' and tk.value in ('function', 'if', 'do', 'repeat'):
                    depth += 1
                elif tk.kind == 'kw' and tk.value in ('end', 'until'):
                    depth -= 1
                    if depth == 0:
                        return {'__function__': self.t[i].line}, k + 1
                k += 1
        return {'__expr__': [(p.kind, p.value) for p in parts], '__line__': tok.line}, j

    def table(self, i):
        assert self.t[i].value == '{'
        i += 1
        arr, obj = [], {}
        while True:
            tok = self.t[i]
            if tok.kind == 'op' and tok.value == '}':
                return ({'__array__': arr, **obj} if arr and obj else (arr if arr else obj)), i + 1
            if tok.kind == 'op' and tok.value in (',', ';'):
                i += 1; continue
            if tok.kind == 'name' and self.t[i + 1].kind == 'op' and self.t[i + 1].value == '=' :
                v, i = self.value(i + 2); obj[tok.value] = v; continue
            if tok.kind == 'op' and tok.value == '[':
                k, i = self.value(i + 1)
                if not (self.t[i].kind == 'op' and self.t[i].value == ']' and self.t[i + 1].value == '='):
                    raise ValueError(f'Unsupported table key at line {tok.line}')
                v, i = self.value(i + 2)
                obj[k if not isinstance(k, dict) else repr(k)] = v; continue
            v, i = self.value(i); arr.append(v)


def top_level_tables(lua_file):
    """Global `Name = { ... }` / `Name.Field = { ... }` top-level assignments:
    (dotted name, literal value, line, first token, end token)."""
    t = lua_file.tokens
    owners = lua_file.owners()
    inside = set()
    for rec in lua_file.functions:
        inside.update(range(rec['start'], rec['end'] + 1))
    reader = _DataReader(t)
    out = []
    i = 0
    while i < len(t) - 2:
        if i in inside or t[i].kind != 'name' or (i and t[i - 1].kind == 'op' and t[i - 1].value in ('.', ':')):
            i += 1; continue
        j, parts = i + 1, [t[i].value]
        while t[j].kind == 'op' and t[j].value == '.' and t[j + 1].kind == 'name':
            parts.append(t[j + 1].value); j += 2
        if t[j].kind == 'op' and t[j].value == '=' and t[j + 1].kind == 'op' and t[j + 1].value == '{' and not (i and t[i - 1].kind == 'op' and t[i - 1].value in ('{', ',')):
            value, end = reader.table(j + 1)
            out.append(('.'.join(parts), value, t[i].line, i, end))
            i = end; continue
        i += 1
    del owners
    return out
