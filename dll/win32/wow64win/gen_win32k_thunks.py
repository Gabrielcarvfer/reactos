#!/usr/bin/env python3
"""
Generate win32k_thunks.c for wow64win.dll: the thunks of the win32k system
calls of 32-bit programs.

The 32-bit user32, gdi32 and imm32 of 64-bit ReactOS (the BUILD_WOW6432 build)
call win32k in the order of win32ss/w32ksvc32.h. Each thunk takes the 32-bit
arguments, widens them (handles are sign-extended, pointers zero-extended),
converts what they point to when its layout differs between 32 and 64 bits,
and calls the 64-bit function of win32u.dll by name.

What needs converting: the structures of the Windows API that have handles
or pointers (MSG, CREATESTRUCT, ...). The structures of win32ss/include have
the 64-bit layout in the BUILD_WOW6432 build already: they are passed as is.

Some calls cannot be described by their prototype (arrays of handles, the
meaning of a parameter that depends on another one...): those are written by
hand in wow64win.c, and listed in MANUAL below.

Usage: run the 64-bit preprocessor (keeping the line markers) on
win32k_types.c, then
    gen_win32k_thunks.py win32k_types.i > win32k_thunks.c
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'wow64', 'csr'))
import gen_csr_thunks as gen  # noqa: E402

SRC = os.path.realpath(os.path.join(HERE, '..', '..', '..'))

# Written by hand in wow64win.c
MANUAL = set()
MANUAL_FILE = os.path.join(HERE, 'wow64win.c')
if os.path.exists(MANUAL_FILE):
    MANUAL = set(re.findall(r'^NTSTATUS WINAPI wow64_(Nt\w+)\( UINT \*args \)', open(MANUAL_FILE).read(), re.M))


def shared_structures():
    """The structures that have the 64-bit layout in the BUILD_WOW6432 build:
    those with KPTR fields (see win32ss/include/kptr.h)"""
    names, bodies = set(), {}
    for hdr in ('ntuser.h', 'ntgdihdl.h', 'ntgdityp.h'):
        text = open(os.path.join(SRC, 'win32ss', 'include', hdr)).read()
        text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
        for m in re.finditer(r'typedef\s+struct\s+\w*\s*\{', text):
            depth, i = 0, m.end() - 1
            while True:
                if text[i] == '{':
                    depth += 1
                elif text[i] == '}':
                    depth -= 1
                    if depth == 0:
                        break
                i += 1
            body = text[m.end():i]
            tail = re.match(r'\}\s*([\w\s,*]*);', text[i:])
            if not tail:
                continue
            for n in tail.group(1).split(','):
                if n.strip() and '*' not in n:
                    bodies[n.strip()] = body
                    if re.search(r'\bKPTR\(|\bK(LIST_ENTRY|UNICODE_STRING|MSG|LARGE_UNICODE_STRING)\b', body):
                        names.add(n.strip())
    # and those that contain them
    changed = True
    while changed:
        changed = False
        for n, body in bodies.items():
            if n not in names and any(re.search(r'\b%s\b' % re.escape(x), body) for x in names):
                names.add(n)
                changed = True
    return names


SHARED = shared_structures()


def read_with_origins(path):
    """Tokens of a preprocessor output, and the header each one comes from"""
    tokens, files = [], []
    current = None
    for line in open(path, errors='replace'):
        m = re.match(r'#\s*(\d+)\s+"([^"]*)"', line)
        if m:
            current = m.group(2)
            continue
        if line.startswith('#'):
            continue
        toks = gen.strip_attributes(gen.tokenize(line))
        tokens += toks
        files += [current] * len(toks)
    return tokens, files


def syscalls64():
    """The win32k system calls the 64-bit kernel has (the stubs of win32ksys)"""
    text = open(os.path.join(SRC, 'win32ss', 'w32ksvc64.h')).read()
    return set('Nt' + m.group(1) for m in re.finditer(r'SVC_\((\w+),', text))


def syscalls():
    """The win32k system calls, in the order of the 32-bit numbers"""
    out, skip = [], []
    for line in open(os.path.join(SRC, 'win32ss', 'w32ksvc32.h')):
        line = line.strip()
        if line.startswith('#if'):
            # Only the 64-bit kernel has the _WIN64 ones (the checked build has the DBG ones)
            skip.append('_WIN64' in line)
            continue
        if line.startswith('#else'):
            skip[-1] = not skip[-1]
            continue
        if line.startswith('#endif'):
            skip.pop()
            continue
        m = re.match(r'SVC_\((\w+),\s*(\d+)\)', line)
        if m and not any(skip):
            out.append((m.group(1), int(m.group(2))))
    return out


class Thunks:
    def __init__(self, parser):
        self.p = parser
        self.g = gen.Gen(parser)
        self.converted = []     # structures with a 32-bit layout to convert
        self.problems = []

    def origin(self, struct):
        for name in (struct.name, (struct.kind + ' ' + struct.tag) if struct.tag else None):
            if name and name in self.p.origin:
                return self.p.origin[name]
        return None

    def param_kind(self, f):
        """('scalar' | 'scalar64' | 'handle' | 'ptrint' | 'ptr' | 'convert' | 'ptrptr', type)"""
        g = self.g
        # A pointer type (PMSG...): what it points to
        if not f.ptr:
            td = self.p.typedefs.get(f.base)
            while td and not td.ptr and not td.inline and not td.dims and td.base in self.p.typedefs:
                td = self.p.typedefs[td.base]
            if td and td.ptr == 1 and not td.dims and not td.base.startswith('struct ') and td.base not in ('void', 'VOID'):
                if not (f.base.startswith('H') and g.resolve(f.base)[0] == 'handle'):
                    return self.param_kind(gen.Field(f.name, td.base, 1, [], None, td.inline))
            elif td and td.ptr == 1 and td.base.startswith('struct '):
                return self.param_kind(gen.Field(f.name, td.base, 1, [], None, td.inline))
        if f.ptr:
            if f.ptr > 1:
                return ('ptrptr', None)
            if f.base in ('void', 'VOID', 'char', 'CHAR', 'WCHAR', 'wchar_t'):
                return ('ptr', None)
            t = g.resolve(f.base)
            if t[0] in ('ptr', 'handle', 'ptrint'):
                return ('ptrptr', t)
            if t[0] == 'struct' and g.needs(t) and t[1].name not in SHARED:
                return ('convert', t[1])
            return ('ptr', None)
        t = g.resolve(f.base)
        if t[0] == 'handle':
            return ('handle', None)
        if t[0] == 'ptr':
            return ('ptr', None)
        if t[0] == 'ptrint':
            return ('ptrint', t[1])
        if t[0] == 'scalar':
            return ('scalar64', None) if t[1] == 8 else ('scalar', None)
        if t[0] == 'struct':
            return ('struct', t[1])
        raise ValueError('parameter type ' + f.base)

    def thunk(self, name, nargs):
        fn = self.p.functions.get(name)
        if not fn:
            self.problems.append('%s: no prototype' % name)
            return None
        ret, params = fn
        lines, call_args, before, after = [], [], [], []
        for i, f in enumerate(params):
            pname = 'p%d' % i
            kind, t = self.param_kind(f)
            if kind == 'scalar' and i in CBSIZE.get(name, {}):
                # The size of one element: the 64-bit one for the kernel
                st = CBSIZE[name][i]
                lines.append('    ULONG %s = get_ulong( &args );' % pname)
                before.append('    if (%s == sizeof(%s_32)) %s = sizeof(%s);' % (pname, st, pname, st))
                call_args.append('(%s)%s' % (self.ctype(f), pname))
            elif kind == 'scalar':
                lines.append('    ULONG %s = get_ulong( &args );' % pname)
                call_args.append('(%s)%s' % (self.ctype(f), pname))
            elif kind == 'scalar64':
                lines.append('    ULONG64 %s = get_ulong64( &args );' % pname)
                call_args.append('*(%s *)&%s' % (self.ctype(f), pname))
            elif kind == 'handle':
                lines.append('    HANDLE %s = get_handle( &args );' % pname)
                call_args.append('(%s)%s' % (self.ctype(f), pname))
            elif kind == 'ptrint':
                if t:  # signed
                    lines.append('    LONG_PTR %s = (LONG)get_ulong( &args );' % pname)
                else:
                    lines.append('    ULONG_PTR %s = get_ulong( &args );' % pname)
                call_args.append('(%s)%s' % (self.ctype(f), pname))
            elif kind == 'ptr':
                lines.append('    void *%s = get_ptr( &args );' % pname)
                call_args.append('(%s)%s' % (self.ctype(f), pname))
            elif kind == 'convert' and i in ARRAYS.get(name, {}):
                # An array of structures, its length in another parameter
                n32 = self.g.name32(t)
                n64 = self.g.name64(t)
                self.g.want(t)
                count = ARRAYS[name][i]
                lines.append('    %s *%s = get_ptr( &args );' % (n32, pname))
                lines.append('    %s *%s_64 = NULL;' % (n64, pname))
                lines.append('    ULONG %s_count = 0, %s_i;' % (pname, pname))
                before.append('    if (%s && (%s_count = %s))' % (pname, pname, count))
                before.append('    {')
                before.append('        %s_64 = Wow64AllocateTemp( %s_count * sizeof(*%s_64) );' % (pname, pname, pname))
                before.append('        memset( %s_64, 0, %s_count * sizeof(*%s_64) );' % (pname, pname, pname))
                before.append('        for (%s_i = 0; %s_i < %s_count; %s_i++) %s_32to64( &%s_64[%s_i], &%s[%s_i] );'
                              % ((pname,) * 3 + (pname, n32[:-3]) + (pname,) * 4))
                before.append('    }')
                after.append('    for (%s_i = 0; %s_64 && %s_i < %s_count; %s_i++) WRITE_BACK( %s, &%s[%s_i], &%s_64[%s_i] );'
                             % ((pname,) * 5 + (n32[:-3],) + (pname,) * 4))
                call_args.append('%s_64 ? %s_64 : (void *)%s' % (pname, pname, pname))
            elif kind == 'convert':
                n32 = self.g.name32(t)
                n64 = self.g.name64(t)
                self.g.want(t)
                lines.append('    %s *%s = get_ptr( &args );' % (n32, pname))
                lines.append('    %s %s_64;' % (n64, pname))
                # (values below 64K are not pointers: class atoms, resource
                # numbers... passed as they are)
                before.append('    if (!IS_INTRESOURCE(%s)) %s_32to64( &%s_64, %s );' % (pname, n32[:-3], pname, pname))
                after.append('    if (!IS_INTRESOURCE(%s)) WRITE_BACK( %s, %s, &%s_64 );' % (pname, n32[:-3], pname, pname))
                call_args.append('IS_INTRESOURCE(%s) ? (void *)%s : &%s_64' % (pname, pname, pname))
            elif kind == 'ptrptr':
                # One value (an output, usually): converted both ways. Arrays
                # are in wow64win.c (MANUAL).
                if t is None:
                    self.problems.append('%s: parameter %d (%s %s) points to pointers to pointers' % (name, i, f.base, f.name))
                    return None
                lines.append('    ULONG *%s = get_ptr( &args );' % pname)
                lines.append('    ULONG64 %s_64 = 0;' % pname)
                if t[0] == 'handle' or (t[0] == 'ptrint' and t[1]):
                    before.append('    if (%s) %s_64 = (LONG)*%s;' % (pname, pname, pname))
                else:
                    before.append('    if (%s) %s_64 = *%s;' % (pname, pname, pname))
                after.append('    if (%s) *%s = (ULONG)%s_64;' % (pname, pname, pname))
                call_args.append('%s ? (void *)&%s_64 : NULL' % (pname, pname))
            elif kind == 'struct':
                if self.g.needs(('struct', t)):
                    self.problems.append('%s: parameter %d (%s) is a structure with pointers' % (name, i, f.base))
                    return None
                # Passed by value: the same bytes in both (a POINT, a SIZEL...)
                lines.append('    %s %s;' % (f.base, pname))
                lines.append('    memcpy( &%s, args, sizeof(%s) ); args += (sizeof(%s) + 3) / 4;' % (pname, pname, pname))
                call_args.append(pname)
        slots = sum(2 if self.param_kind(f)[0] == 'scalar64' else
                    (max(1, (self.struct_size(f) + 3) // 4) if self.param_kind(f)[0] == 'struct' else 1)
                    for f in params)
        if slots != nargs:
            self.problems.append('%s: %d argument slots, %d in w32ksvc32.h' % (name, slots, nargs))
            return None
        out = ['NTSTATUS WINAPI wow64_%s( UINT *args )' % name, '{']
        out += lines
        rtype = self.ret_type(ret)
        if lines:
            out.append('')
        if rtype:
            out.append('    %s ret;' % rtype)
            out.append('')
        out += before
        call = '%s( %s )' % (name, ', '.join(call_args))
        if rtype:
            out.append('    ret = %s;' % call)
        else:
            out.append('    %s;' % call)
        out += after
        if rtype:
            out.append('    return (NTSTATUS)(ULONG_PTR)ret;')
        else:
            out.append('    return STATUS_SUCCESS;')
        out.append('}')
        return '\n'.join(out) + '\n'

    # Sizes of the plain structures passed by value
    SIZES = {'POINT': 8, 'POINTL': 8, 'SIZE': 8, 'SIZEL': 8, 'BLENDFUNCTION': 4, 'PALETTEENTRY': 4,
             'RECT': 16, 'RECTL': 16, 'LARGE_INTEGER': 8, 'ULARGE_INTEGER': 8, 'COLORREF': 4}

    def struct_size(self, f):
        if f.base not in self.SIZES:
            raise ValueError('size of %s' % f.base)
        return self.SIZES[f.base]

    def ctype(self, f):
        t = f.base
        if t.startswith('struct ') or t.startswith('union '):
            pass
        return t + ' ' + '*' * f.ptr if f.ptr else t

    def ret_type(self, ret):
        if ret.base in ('void', 'VOID') and not ret.ptr:
            return None
        return ret.base + (' ' + '*' * ret.ptr if ret.ptr else '')


# Pointers to arrays of structures: the C expression of their length
ARRAYS = {
    'NtUserSendInput': {1: 'p0'},
    'NtGdiPolyPatBlt': {2: 'p3'},
    'NtGdiPolyTextOutW': {1: 'p2'},
    'NtGdiConsoleTextOut': {1: 'p2'},
    'NtUserGetRawInputDeviceList': {0: '(p1 ? *(UINT *)p1 : 0)'},
    'NtUserGetRegisteredRawInputDevices': {0: '(p1 ? *(UINT *)p1 : 0)'},
    'NtUserRegisterRawInputDevices': {0: 'p1'},
    'NtUserGetMouseMovePointsEx': {2: 'p3'},
}

# Parameters giving the size of one structure, which the kernel checks
CBSIZE = {
    'NtUserSendInput': {2: 'INPUT'},
    'NtUserGetRawInputDeviceList': {2: 'RAWINPUTDEVICELIST'},
    'NtUserGetRegisteredRawInputDevices': {2: 'RAWINPUTDEVICE'},
    'NtUserRegisterRawInputDevices': {2: 'RAWINPUTDEVICE'},
    'NtUserGetMouseMovePointsEx': {0: 'MOUSEMOVEPOINT'},
}

# Also needed by the callbacks of win32k (callbacks.c): their arguments, and
# the structures of the messages whose lParam points to data
EXTRA_STRUCTS = [
    'WINDOWPROC_CALLBACK_ARGUMENTS', 'SENDASYNCPROC_CALLBACK_ARGUMENTS', 'HOOKPROC_CALLBACK_ARGUMENTS',
    'EVENTPROC_CALLBACK_ARGUMENTS', 'LOADMENU_CALLBACK_ARGUMENTS', 'COPYIMAGE_CALLBACK_ARGUMENTS',
    'CLIENT_LOAD_LIBRARY_ARGUMENTS', 'SETWNDICONS_CALLBACK_ARGUMENTS', 'DDEPOSTGET_CALLBACK_ARGUMENTS',
    'LPK_CALLBACK_ARGUMENTS', 'IMMPROCESSKEY_CALLBACK_ARGUMENTS', 'IMMLOADLAYOUT_CALLBACK_ARGUMENTS',
    'IMMLOADLAYOUT_CALLBACK_OUTPUT', 'HOOKPROC_CBT_CREATEWND_EXTRA_ARGUMENTS',
    'CREATESTRUCTW', 'WINDOWPOS', 'NCCALCSIZE_PARAMS', 'DRAWITEMSTRUCT', 'MEASUREITEMSTRUCT',
    'COMPAREITEMSTRUCT', 'DELETEITEMSTRUCT', 'HELPINFO', 'MDICREATESTRUCTW', 'COPYDATASTRUCT',
    'MOUSEHOOKSTRUCT', 'KBDLLHOOKSTRUCT', 'MSLLHOOKSTRUCT', 'CBTACTIVATESTRUCT', 'CWPSTRUCT',
    'CWPRETSTRUCT', 'MSG', 'EVENTMSG', 'DEBUGHOOKINFO', 'MDINEXTMENU', 'DOSENDMESSAGE', 'BROADCASTPARM',
    'CALL_BACK_INFO',
    # (wow64win.c)
    'OUTLINETEXTMETRICW',
]


def main():
    tokens, files = read_with_origins(sys.argv[1])
    p = gen.Parser(tokens, files)
    p.parse()
    th = Thunks(p)

    bodies, table = [], []
    have64 = syscalls64()
    for svc, nargs in syscalls():
        name = 'Nt' + svc
        if name not in have64:
            th.problems.append('%s: not a system call of the 64-bit kernel' % name)
            table.append(None)
            continue
        if name in MANUAL:
            table.append(name)
            continue
        try:
            body = th.thunk(name, nargs)
        except ValueError as e:
            th.problems.append('%s: %s' % (name, e))
            body = None
        if body:
            bodies.append(body)
            table.append(name)
        else:
            table.append(None)

    for name in EXTRA_STRUCTS:
        t = th.g.resolve(name)
        if t[0] == 'struct' and th.g.needs(t):
            th.g.want(t[1])
        else:
            th.problems.append('%s: nothing to convert' % name)

    # The 32-bit layouts of the structures to convert, and their converters,
    # go to win32k_structs.h (also used by callbacks.c)
    real_stdout = sys.stdout
    sys.stdout = open(os.path.join(HERE, 'win32k_structs.h'), 'w')
    print('/* Generated by gen_win32k_thunks.py: the 32-bit layouts of the')
    print(' * structures that win32k system calls and callbacks pass, and their')
    print(' * conversions. Do not edit. */\n')
    print('#pragma once\n')
    print('#ifdef __GNUC__')
    print('#pragma GCC diagnostic ignored "-Wunused-function"')
    print('#endif\n')
    for s in th.g.needed:
        print(th.g.emit_struct(s), end='')
    for s in th.g.needed:
        n32, n64 = th.g.name32(s), th.g.name64(s)
        for to64 in (True, False):
            out, offsets = [], []
            th.g.conv(s, to64, 'd->_', 's->_', out, offsets)
            if to64:
                print('static void %s_32to64(%s *d, const %s *s)\n{' % (n32[:-3], n64, n32))
            else:
                print('static void %s_64to32(%s *d, const %s *s)\n{' % (n32[:-3], n32, n64))
            for stmt in out:
                stmt = stmt.replace('d->_.', 'd->').replace('s->_.', 's->').replace('d->_', '(*d)').replace('s->_', '(*s)')
                print('    ' + stmt)
            print('}\n')
    sys.stdout.close()
    sys.stdout = real_stdout
    print('/* Generated by gen_win32k_thunks.py from the win32k headers and')
    print(' * win32ss/w32ksvc32.h. Do not edit: the calls it cannot describe are')
    print(' * in wow64win.c. */\n')
    print('#include "wow64win_private.h"')
    print('#include "win32k_structs.h"\n')
    for name in sorted(MANUAL):
        print('extern NTSTATUS WINAPI wow64_%s( UINT *args ); /* wow64win.c */' % name)
    print()
    for b in bodies:
        print(b)
    print('/* NULL: not converted yet, see the list at the end */')
    print('const ULONG_PTR wow64win_thunks[] =\n{')
    for name in table:
        print('    (ULONG_PTR)%s,' % ('wow64_' + name if name else 'NULL'))
    print('};\n')
    print('/* For the debug output of wow64.dll */')
    print('const char * const wow64win_names[] =\n{')
    for svc, nargs in syscalls():
        print('    "Nt%s",' % svc)
    print('};\n')
    print('const BYTE wow64win_args[] =\n{')
    for svc, nargs in syscalls():
        print('    %d,' % (nargs * 4))
    print('};\n')
    print('const ULONG wow64win_count = %d;\n' % len(table))
    print('/* Not converted:')
    for pr in th.problems:
        print(' * ' + pr)
    print(' */')
    print('%d thunks, %d by hand, %d missing' % (len(bodies), len([t for t in table if t in MANUAL]),
                                                 table.count(None)), file=sys.stderr)


if __name__ == '__main__':
    main()
