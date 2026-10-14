#!/usr/bin/env python3
"""
Generate csr_thunks.h: the 32-bit layouts of the CSR messages of ReactOS and
the code that converts them to and from the 64-bit ones, for wow64.dll.

A 32-bit program talks to CSRSS (the base, console and user servers) with
messages whose handles and pointers are 4 bytes long; CSRSS is 64-bit. The
messages are declared in sdk/include/reactos/subsys; this script reads them
after the C preprocessor ran on them (so that macros and #if are resolved like
in the real build), and writes, for every message type that has handles or
pointers in it:
  - its 32-bit layout, "T_32",
  - T_32to64() and T_64to32(), which convert field by field (handles are
    sign-extended, pointers zero-extended),
  - the offsets of its pointer fields in both layouts, to move the pointers
    of the capture buffers and the pointers into the message itself.
It also finds out which API number uses which message type, from the dispatch
tables of the servers (each Srv* routine names the union member it uses).

Usage: run the 64-bit preprocessor on csr_types.c, with the include paths
and the definitions of a 64-bit user-mode module, then
    gen_csr_thunks.py csr_types.i > csr_thunks.h
The generated header checks the 32-bit layouts against the real ones when it
is compiled for i386 (csr_types.c includes all it needs).
"""
import os
import re
import sys

SRC = os.path.realpath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..'))

# Integer types as wide as a pointer
PTRINT = {'ULONG_PTR': False, 'LONG_PTR': True, 'UINT_PTR': False, 'INT_PTR': True,
          'DWORD_PTR': False, 'SIZE_T': False, 'SSIZE_T': True, 'WPARAM': False,
          'LPARAM': True, 'LRESULT': True, 'KAFFINITY': False, 'HANDLE_PTR': False,
          'size_t': False, 'ptrdiff_t': True, 'intptr_t': True, 'uintptr_t': False}
BUILTIN = {'char': 1, 'short': 2, 'int': 4, 'long': 4, '__int64': 8, 'float': 4,
           'double': 8, 'wchar_t': 2, '_Bool': 1, 'void': 0, '__int8': 1, '__int16': 2,
           '__int32': 4}

# The messages and the other structures to convert
ROOTS = ['CSR_API_CONNECTINFO', 'BASESRV_API_CONNECTINFO', 'CONSRV_API_CONNECTINFO',
         'BASE_STATIC_SERVER_DATA', 'CSR_CLIENT_CONNECT']
SERVERS = [
    # index, message type, dispatch table, file with the table, first API number
    (1, 'BASE_API_MESSAGE', 'BaseServerApiDispatchTable', 'subsystems/win/basesrv/init.c'),
    (2, 'CONSOLE_API_MESSAGE', 'ConsoleServerApiDispatchTable', 'win32ss/user/winsrv/consrv/init.c'),
    (3, 'USER_API_MESSAGE', 'UserServerApiDispatchTable', 'win32ss/user/winsrv/usersrv/init.c'),
]


def tokenize(text):
    return re.findall(r'[A-Za-z_]\w*|0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*|"(?:\\.|[^"])*"|'
                      r"'(?:\\.|[^'])*'|->|<<|>>|[{}()\[\];,*:=<>+\-/&|~!?.%^]", text)


def strip_attributes(tokens):
    out, i = [], 0
    while i < len(tokens):
        t = tokens[i]
        if t in ('__attribute__', '__declspec', '__pragma', '_Pragma', '__asm__', '__asm'):
            i += 1
            depth = 0
            while i < len(tokens):
                if tokens[i] == '(':
                    depth += 1
                elif tokens[i] == ')':
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
            continue
        if t in ('__extension__', '__inline', '__inline__', 'inline', '__restrict', '__restrict__',
                 'restrict', '__unaligned', '__ptr64', '__ptr32', '__stdcall', '__cdecl', '__fastcall'):
            i += 1
            continue
        out.append(t)
        i += 1
    return out


class Field:
    def __init__(self, name, base, ptr, dims, bits, inline=None):
        self.name = name        # None for an anonymous struct or union
        self.base = base        # type name (or 'struct X', 'union X', 'enum X')
        self.ptr = ptr          # pointer, or function pointer
        self.dims = dims        # array dimensions
        self.bits = bits        # bit field width, or None
        self.inline = inline    # Struct for an inline struct or union


class Struct:
    def __init__(self, kind, tag, fields):
        self.kind = kind
        self.tag = tag
        self.fields = fields
        self.name = None        # typedef name


def eval_dim(tokens):
    expr = ' '.join(tokens)
    expr = re.sub(r'(\d+)[uUlL]+', r'\1', expr)
    expr = expr.replace('sizeof', '__sizeof__')
    try:
        return int(eval(expr, {'__builtins__': {}}, {'__sizeof__': SIZEOF_HELPER}))
    except Exception:
        raise ValueError('cannot evaluate array size: ' + expr)


def SIZEOF_HELPER(x):
    raise ValueError('sizeof')


class Parser:
    def __init__(self, tokens, files=None):
        self.t = tokens
        self.i = 0
        self.typedefs = {}      # name -> Field (describing the type)
        self.tags = {}          # 'struct X' -> Struct
        self.files = files      # token index -> header it comes from
        self.origin = {}        # typedef or tag -> header
        self.functions = {}     # name -> (return Field, [parameter Fields])

    def peek(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else None

    def take(self):
        tok = self.t[self.i]
        self.i += 1
        return tok

    def skip_balanced(self, open_, close):
        depth = 0
        while True:
            tok = self.take()
            if tok == open_:
                depth += 1
            elif tok == close:
                depth -= 1
                if depth == 0:
                    return

    def parse_specifier(self):
        """Returns (base name, Struct or None)"""
        words = []
        inline = None
        while True:
            tok = self.peek()
            if tok in ('const', 'volatile', 'static', 'extern', 'register', 'signed', 'auto'):
                self.take()
                if tok == 'signed':
                    words.append('signed')
                continue
            if tok in ('struct', 'union'):
                kind = self.take()
                tag = None
                if re.match(r'[A-Za-z_]\w*$', self.peek()):
                    tag = self.take()
                if self.peek() == '{':
                    fields = self.parse_members()
                    inline = Struct(kind, tag, fields)
                    if tag:
                        self.tags[kind + ' ' + tag] = inline
                        if self.files:
                            self.origin[kind + ' ' + tag] = self.files[min(self.i, len(self.files) - 1)]
                return (kind + ' ' + (tag or '<anon>'), inline)
            if tok == 'enum':
                self.take()
                if re.match(r'[A-Za-z_]\w*$', self.peek()):
                    self.take()
                if self.peek() == '{':
                    self.skip_balanced('{', '}')
                return ('enum', None)
            if tok in ('unsigned',):
                words.append(self.take())
                continue
            if tok in ('long', 'short', 'int', 'char', '__int64', '__int8', '__int16', '__int32',
                       'float', 'double', 'void', '_Bool', 'wchar_t'):
                words.append(self.take())
                continue
            if not words and re.match(r'[A-Za-z_]\w*$', tok or ''):
                words.append(self.take())
                return (' '.join(words), None)
            break
        return (' '.join(words), None)

    def parse_declarator(self):
        """Returns (name, ptr, dims, funcptr)"""
        ptr = 0
        while self.peek() in ('*', 'const', 'volatile', '__ptr64'):
            if self.take() == '*':
                ptr += 1
        name = None
        func = False
        if self.peek() == '(':
            # (*name)(...) or (CALLCONV *name)(...)
            self.take()
            while self.peek() != ')':
                tok = self.take()
                if tok == '*':
                    ptr += 1
                elif re.match(r'[A-Za-z_]\w*$', tok):
                    name = tok
            self.take()
            func = True
        elif re.match(r'[A-Za-z_]\w*$', self.peek() or ''):
            name = self.take()
        dims = []
        while self.peek() in ('[', '('):
            if self.peek() == '(':
                self.skip_balanced('(', ')')
                func = True
                continue
            self.take()
            expr = []
            while self.peek() != ']':
                expr.append(self.take())
            self.take()
            dims.append(expr)
        return name, ptr, dims, func

    def parse_members(self):
        self.take()  # {
        fields = []
        while self.peek() != '}':
            base, inline = self.parse_specifier()
            if self.peek() == ';':
                self.take()
                # an anonymous struct or union, also a typedef'd one (MS extension)
                fields.append(Field(None, base, 0, [], None, inline))
                continue
            while True:
                name, ptr, dims, func = self.parse_declarator()
                bits = None
                if self.peek() == ':':
                    self.take()
                    bits = int(self.take().rstrip('uUlL'), 0)
                f = Field(name, base, ptr + (1 if func and not ptr else 0), dims, bits, inline)
                fields.append(f)
                if self.peek() == ',':
                    self.take()
                    continue
                break
            assert self.take() == ';'
        self.take()  # }
        return fields

    def parse(self):
        while self.peek() is not None:
            if self.peek() == 'typedef':
                self.take()
                start = self.i
                try:
                    base, inline = self.parse_specifier()
                    while True:
                        name, ptr, dims, func = self.parse_declarator()
                        if name:
                            f = Field(name, base, ptr + (1 if func and not ptr else 0), dims, None, inline)
                            self.typedefs[name] = f
                            if self.files:
                                self.origin[name] = self.files[min(self.i, len(self.files) - 1)]
                            if inline and not ptr and not dims and not inline.name:
                                inline.name = name
                        if self.peek() == ',':
                            self.take()
                            continue
                        break
                except Exception:
                    self.i = start
                self.skip_to_semicolon()
            elif self.peek() in ('struct', 'union'):
                start = self.i
                try:
                    self.parse_specifier()
                except Exception:
                    self.i = start
                self.skip_to_semicolon()
            else:
                start = self.i
                try:
                    self.parse_function()
                except Exception:
                    pass
                self.i = start
                self.skip_to_semicolon()

    def parse_function(self):
        """A function declaration: remember its parameters"""
        base, inline = self.parse_specifier()
        ptr = 0
        while self.peek() in ('*', 'const'):
            if self.take() == '*':
                ptr += 1
        name = self.take()
        if not re.match(r'[A-Za-z_]\w*$', name) or self.peek() != '(':
            return
        self.take()
        params, cur, depth = [], [], 0
        while True:
            tok = self.take()
            if tok == '(':
                depth += 1
            elif tok == ')':
                if depth == 0:
                    break
                depth -= 1
            if tok == ',' and depth == 0:
                params.append(cur)
                cur = []
            else:
                cur.append(tok)
        if cur:
            params.append(cur)
        fields = []
        for toks in params:
            if toks == ['void'] or toks == ['...']:
                continue
            sub = Parser(toks + [';'])
            pbase, pinline = sub.parse_specifier()
            pname, pptr, pdims, pfunc = sub.parse_declarator()
            fields.append(Field(pname, pbase, pptr + (1 if pfunc and not pptr else 0), pdims, None, pinline))
        self.functions[name] = (Field(name, base, ptr, [], None, inline), fields)

    def skip_to_semicolon(self):
        depth = 0
        body = False
        while self.peek() is not None:
            prev = self.t[self.i - 1] if self.i else None
            tok = self.take()
            if tok in ('{', '('):
                if tok == '{' and depth == 0 and prev == ')':
                    body = True     # a function definition: it ends with its }
                depth += 1
            elif tok in ('}', ')'):
                depth -= 1
                if body and depth == 0:
                    return
            elif tok == ';' and depth <= 0:
                return


class Gen:
    def __init__(self, parser):
        self.p = parser
        self.needed = []        # Structs to emit, in dependency order
        self.seen = set()
        self.dims_cache = {}

    def dims_of(self, f):
        # As C expressions: the compiler knows the enum values and macros
        return ['(%s)' % ' '.join(d) for d in f.dims]

    # A type is one of: ('scalar', size, cname), ('ptr',), ('handle',), ('ptrint', signed),
    # ('struct', Struct)
    def resolve(self, base, ptr=0, seen=None):
        if ptr:
            if base in ('HANDLE',) or (base.startswith('struct ') and base.endswith('__')):
                return ('handle',)
            return ('ptr',)
        if base in PTRINT:
            return ('ptrint', PTRINT[base])
        if base == 'HANDLE':
            return ('handle',)
        if base.startswith('struct ') or base.startswith('union '):
            s = self.p.tags.get(base)
            if not s:
                raise ValueError('unknown ' + base)
            return ('struct', s)
        if base == 'enum':
            return ('scalar', 4, 'ULONG')
        words = base.split()
        if all(w in BUILTIN or w in ('unsigned', 'signed') for w in words) and words:
            size = max([BUILTIN.get(w, 0) for w in words] + [4 if words in (['unsigned'], ['signed']) else 0])
            if words.count('long') == 2:
                size = 8
            return ('scalar', size, base)
        td = self.p.typedefs.get(base)
        if not td:
            raise ValueError('unknown type ' + base)
        if td.dims:
            n = '*'.join(self.dims_of(td))
            inner = self.resolve(td.base, td.ptr)
            return ('array', inner, n, base)
        if td.ptr:
            if base.startswith('H') or base in ('HANDLE',):
                return ('handle',)
            return ('ptr',)
        if td.inline:
            return ('struct', td.inline)
        r = self.resolve(td.base)
        if r[0] == 'scalar':
            return ('scalar', r[1], base)
        return r

    def needs(self, t):
        if t[0] in ('ptr', 'handle', 'ptrint'):
            return True
        if t[0] == 'array':
            return self.needs(t[1])
        if t[0] == 'struct':
            return any(self.field_needs(f) for f in t[1].fields)
        return False

    def field_needs(self, f):
        if f.inline and not f.ptr:
            return self.needs(('struct', f.inline))
        return self.needs(self.resolve(f.base, f.ptr))

    def type32(self, f):
        """C type of a field in the 32-bit layout"""
        if f.inline and not f.ptr:
            if self.needs(('struct', f.inline)):
                return self.inline32(f.inline)
            return self.inline64(f.inline)
        t = self.resolve(f.base, f.ptr)
        if t[0] in ('ptr', 'handle'):
            return 'ULONG'
        if t[0] == 'ptrint':
            return 'LONG' if t[1] else 'ULONG'
        if t[0] == 'array' and self.needs(t):
            elem = t[1]
            if elem[0] in ('ptr', 'handle'):
                return 'ULONG'
            if elem[0] == 'ptrint':
                return 'LONG' if elem[1] else 'ULONG'
            raise ValueError('array of structs to convert: ' + f.base)
        if t[0] == 'struct' and self.needs(t):
            self.want(t[1])
            return self.name32(t[1])
        return f.base

    def name32(self, s):
        return (s.name or s.tag.lstrip('_')) + '_32'

    def name64(self, s):
        return s.name or (s.kind + ' ' + s.tag)

    def inline64(self, s):
        body = ''.join('    %s;\n' % self.decl(f, f.base if not f.inline else self.inline64(f.inline))
                       for f in s.fields)
        return '%s\n{\n%s}' % (s.kind, body)

    def inline32(self, s):
        body = ''
        for f in s.fields:
            body += '    %s;\n' % self.decl(f, self.type32(f), True)
        return ('%s\n{\n' % s.kind) + '\n'.join('    ' + l if l else l for l in body.rstrip('\n').split('\n')).replace('        ', '    ', 0) + '\n}'

    def decl(self, f, ctype, is32=False):
        ptrs = '' if (is32 or not f.ptr or f.inline is None and False) else ''
        if not is32 and f.ptr:
            ptrs = '*' * f.ptr
        if is32:
            ptrs = ''
        name = f.name or ''
        dims = ''.join('[%s]' % ' '.join(d) for d in f.dims)
        bits = ' : %d' % f.bits if f.bits is not None else ''
        return ('%s %s%s%s%s' % (ctype, ptrs, name, dims, bits)).rstrip()

    def want(self, s):
        key = id(s)
        if key in self.seen:
            return
        self.seen.add(key)
        for f in s.fields:
            if f.inline and not f.ptr:
                for g in f.inline.fields:
                    if not g.inline and not g.ptr:
                        t = self.resolve(g.base)
                        if t[0] == 'struct' and self.needs(t):
                            self.want(t[1])
                continue
            t = self.resolve(f.base, f.ptr)
            if t[0] == 'struct' and self.needs(t):
                self.want(t[1])
        self.needed.append(s)

    def emit_struct(self, s):
        out = 'typedef %s\n{\n' % s.kind
        for f in s.fields:
            out += '    %s;\n' % self.decl(f, self.type32(f), True).replace('\n', '\n    ')
        out += '} %s;\n\n' % self.name32(s)
        return out

    # Converters: a list of C statements for the fields of s, with the
    # prefixes d (destination) and s (source) for the field paths
    def conv(self, s, to64, dpath, spath, out, offsets, path=''):
        for n, f in enumerate(s.fields):
            # A size field first (cbSize...): the size of the other layout
            if n == 0 and not path and f.name in ('cbSize', 'cbStruct') and not f.ptr and not f.dims:
                out.append('%s = sizeof(*d);' % (dpath + '.' + f.name))
                continue
            fd = dpath + ('.' + f.name if f.name else '')
            fs = spath + ('.' + f.name if f.name else '')
            fpath = path + ('.' + f.name if f.name else '')
            if f.bits is not None:
                out.append('%s = %s;' % (fd, fs))
                continue
            if f.name is None and not f.inline:
                # an anonymous member of a typedef'd struct type (MS extension)
                t = self.resolve(f.base)
                if self.needs(t):
                    self.conv(t[1], to64, dpath, spath, out, offsets, path)
                else:
                    first = t[1].fields[0].name
                    out.append('memcpy(&%s.%s, &%s.%s, sizeof(%s));' % (dpath, first, spath, first, f.base))
                continue
            if f.inline and not f.ptr:
                if f.dims:
                    raise ValueError('array of inline structs')
                if f.inline.kind == 'union' and self.needs(('struct', f.inline)):
                    self.conv_union(f.inline, to64, fd, fs, out, offsets, fpath)
                elif self.needs(('struct', f.inline)):
                    self.conv(f.inline, to64, fd, fs, out, offsets, fpath)
                else:
                    out.append('memcpy(&%s, &%s, sizeof(%s));' % (fd, fs, fd))
                continue
            t = self.resolve(f.base, f.ptr)
            dims = self.dims_of(f)
            if t[0] == 'array':
                dims = dims + [t[2]]
                t = t[1]
            if not self.needs(t):
                out.append('memcpy(&%s, &%s, sizeof(%s));' % (fd, fs, fd))
                continue
            idx = ''
            loops, names = [], []
            for n, d in enumerate(dims):
                v = 'i%d' % n
                names.append(v)
                loops.append('for (%s = 0; %s < %s; %s++)' % (v, v, d, v))
                idx += '[%s]' % v
            stmt = self.scalar_conv(t, to64, fd + idx, fs + idx, out, offsets, fpath, dims)
            if loops:
                out.append('{ ULONG %s; %s %s }' % (', '.join(names), ' '.join(loops), stmt))
            elif stmt:
                out.append(stmt)

    def scalar_conv(self, t, to64, fd, fs, out, offsets, fpath, dims):
        if t[0] in ('ptr', 'handle', 'ptrint'):
            if not dims:
                offsets.append((fpath, t[0]))
            if to64:
                if t[0] == 'ptr':
                    return '%s = ULongToPtr(%s);' % (fd, fs)
                if t[0] == 'handle':
                    return '%s = LongToHandle(%s);' % (fd, fs)
                return '%s = %s;' % (fd, fs)
            if t[0] == 'ptr':
                return '%s = PtrToUlong(%s);' % (fd, fs)
            if t[0] == 'handle':
                return '%s = HandleToLong(%s);' % (fd, fs)
            return '%s = (%s)%s;' % (fd, 'LONG' if t[1] else 'ULONG', fs)
        if t[0] == 'struct':
            sub = []
            self.conv(t[1], to64, fd, fs, sub, offsets if not dims else [], fpath)
            return ' '.join(sub)
        return '%s = %s;' % (fd, fs)

    def conv_union(self, u, to64, fd, fs, out, offsets, fpath):
        # Converted as its first member (the alternatives are usually
        # pointers or handles of different types: the same conversion)
        f = u.fields[0]
        sub = gen_struct = Struct('struct', None, [f])
        self.conv(sub, to64, fd, fs, out, offsets, fpath)


def server_api_members(path, table):
    """API index -> union member name, from the dispatch table and the Srv* routines"""
    text = open(os.path.join(SRC, path)).read()
    body = re.search(table + r'\[[^\]]*\]\s*=\s*\{(.*?)\};', text, re.S).group(1)
    routines = re.findall(r'\b(\w+)\s*,', re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S))
    srcdir = os.path.dirname(os.path.join(SRC, path))
    sources = []
    for root, _, files in os.walk(srcdir):
        for f in files:
            if f.endswith('.c'):
                sources.append(open(os.path.join(root, f), errors='replace').read())
    if 'consrv' in path:
        for root, _, files in os.walk(os.path.join(srcdir, '..', '..', '..', 'user', 'winsrv', 'consrv')):
            pass
    alltext = '\n'.join(sources)
    members = []
    for r in routines:
        member = None
        # consrv: CON_API(SrvName, TYPE, RequestName)
        m = re.search(r'CON_API\w*\(\s*' + r + r'\s*,\s*\w+\s*,\s*(\w+)\s*\)', alltext)
        if m:
            members.append((r, m.group(1)))
            continue
        m = re.search(r'CSR_API\(\s*' + r + r'\s*\)\s*\{(.*?)\n\}', alltext, re.S)
        if m:
            mm = re.search(r'->Data\.(\w+)', m.group(1))
            if mm:
                member = mm.group(1)
        members.append((r, member))
    return members


def main():
    text = open(sys.argv[1]).read()
    p = Parser(strip_attributes(tokenize(text)))
    p.parse()
    g = Gen(p)

    roots = []
    for name in ROOTS:
        t = g.resolve(name)
        roots.append((name, t[1]))

    servers = []
    for index, msgtype, table, path in SERVERS:
        msg = g.resolve(msgtype)[1]
        union = [f for f in msg.fields if f.name == 'Data'][0].inline
        members = {f.name: f for f in union.fields}
        apis = []
        for routine, member in server_api_members(path, table):
            if member and member in members:
                f = members[member]
                apis.append((routine, member, f.base))
            else:
                apis.append((routine, member, None))
        servers.append((index, msgtype, apis))

    print('/* Generated by gen_csr_thunks.py from the headers in')
    print(' * sdk/include/reactos/subsys and the dispatch tables of the servers.')
    print(' * Do not edit. */\n')
    print('#ifdef __GNUC__')
    print('#pragma GCC diagnostic ignored "-Wunused-function"')
    print('#endif\n')

    structs = []
    for name, s in roots:
        if g.needs(('struct', s)):
            g.want(s)
    for index, msgtype, apis in servers:
        for routine, member, base in apis:
            if base:
                t = g.resolve(base)
                if g.needs(t):
                    g.want(t[1])

    print('#include <pshpack4.h>\n' if False else '', end='')
    for s in g.needed:
        print(g.emit_struct(s), end='')

    # Checked when compiled for i386 (csr_layout_check.c): the 32-bit layouts
    # must be the real ones
    print('#ifndef _WIN64')
    for s in g.needed:
        n32, n64 = g.name32(s), g.name64(s)
        print('C_ASSERT(sizeof(%s) == sizeof(%s));' % (n32, n64))
        offsets = []
        g.conv(s, True, 'x', 'x', [], offsets)
        for fpath, kind in offsets:
            print('C_ASSERT(FIELD_OFFSET(%s, %s) == FIELD_OFFSET(%s, %s));' % (n32, fpath[1:], n64, fpath[1:]))
    print('#else\n')

    # converters
    for s in g.needed:
        n32, n64 = g.name32(s), g.name64(s)
        for to64 in (True, False):
            out, offsets = [], []
            if to64:
                g.conv(s, True, 'd->_', 's->_', out, offsets)
                print('static void %s_32to64(%s *d, const %s *s)\n{' % (n32[:-3], n64, n32))
            else:
                g.conv(s, False, 'd->_', 's->_', out, offsets)
                print('static void %s_64to32(%s *d, const %s *s)\n{' % (n32[:-3], n32, n64))
            for stmt in out:
                stmt = stmt.replace('d->_.', 'd->').replace('s->_.', 's->').replace('d->_', '(*d)').replace('s->_', '(*s)')
                print('    ' + stmt)
            print('}\n')
        # pointer fields: their offsets in both layouts
        offsets = []
        g.conv(s, True, 'x', 'x', [], offsets)
        print('static const struct csr_field %s_fields[] =\n{' % n32[:-3])
        for fpath, kind in offsets:
            print('    { FIELD_OFFSET(%s, %s), FIELD_OFFSET(%s, %s), CSR_FIELD_%s },' %
                  (n32, fpath[1:], n64, fpath[1:], kind.upper()))
        print('    { ~0u }\n};\n')

    # API tables
    for index, msgtype, apis in servers:
        print('static const struct csr_api csr_apis_%d[] =\n{' % index)
        for routine, member, base in apis:
            if not base:
                print('    { 0 }, /* %s */' % routine)
                continue
            t = g.resolve(base)
            if g.needs(t):
                n = g.name32(t[1])[:-3]
                print('    { sizeof(%s_32), sizeof(%s), (void *)%s_32to64, (void *)%s_64to32, %s_fields }, /* %s */' %
                      (n, base, n, n, n, routine))
            else:
                print('    { sizeof(%s), sizeof(%s) }, /* %s */' % (base, base, routine))
        print('};\n')
    print('#endif /* _WIN64 */')


if __name__ == '__main__':
    main()
