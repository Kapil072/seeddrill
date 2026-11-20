#!/usr/bin/env python3
"""Packs the captured screens into an SD-card DWIN_SET for the T5L display and
generates firmware/lib/hmi/hmi_map.h, so the two can never disagree.

File formats follow the T5L DGUS II Application Development Guide (control field
tables, Ch. 6-7). The container layouts the guide leaves out (ICL header, 14.bin
page table, 13.bin end mark) were checked against files produced by DWIN's own DGUS
tool. tools/hmi_emulator.py reads the output back through an independent parser.
"""
import hashlib
import io
import json
import os
import struct
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, 'build')
OUT = os.path.join(ROOT, 'DWIN_SET')
HEADER = os.path.join(ROOT, '..', 'firmware', 'lib', 'hmi', 'hmi_map.h')

W, H = 1024, 600
BLOCK = 256 * 1024             # display flash is addressed in 256 KB blocks
FLASH_BLOCKS = 64              # 16 MB
BG_LIB = 32                    # factory CFG byte 0x08 points here
ICON_LIBS = (23, 15)           # 23..31 and 15..21: free space around 22_Config
PRESS_BASE = 32                # pressed look of page n is image n + 32
MAX_PICTURE = 248 * 1024       # T5L1 limit for one JPEG
JPEG_BG_Q, JPEG_ICON_Q = 90, 93
VP_BASE, VP_END = 0x5000, 0x7FFF   # 0x1000-0x4FFF is curve buffer, 0x8000+ is SP
HIDE = 0xFFFF


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ (0xA001 if c & 1 else 0)
    return c


def rgb565(h):
    h = h.lstrip('#')
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def jpeg(im, quality):
    b = io.BytesIO()
    im.convert('RGB').save(b, 'JPEG', quality=quality, subsampling=0, optimize=False, progressive=False)
    return b.getvalue()


def build_config():
    """Generate 22_Config.bin with proper DWIN T5L configuration."""
    cfg = bytearray(0x20000)  # 128KB
    # Offset 0x00: System config
    cfg[0x00] = 0x5A  # Magic byte
    cfg[0x01] = 0x00  # Baud rate: 00 = 115200 (not 03=19200)
    cfg[0x02] = 0x38  # Auto-upload on (bit 3), touch sound on (bit 4)
    cfg[0x08] = BG_LIB  # Background image library ID
    return bytes(cfg)


def sos_end(j):
    """Offset of the entropy-coded data, just past the SOS header."""
    i = 2
    while True:
        marker, size = j[i + 1], int.from_bytes(j[i + 2:i + 4], 'big')
        if marker == 0xDA:
            return i + 2 + size
        i += 2 + size


def align_scan(j, file_pos):
    """The T5L decoder wants entropy data on a 4-byte address. DGUS gets there by
    stretching the SOS header with 0xFF fill, so we do the same."""
    x = sos_end(j)
    pad = -(file_pos + x) % 4
    if pad:
        s = x - 14
        assert j[s:s + 4] == b'\xff\xda\x00\x0c'
        j = j[:s + 2] + (12 + pad).to_bytes(2, 'big') + j[s + 4:x] + b'\xff' * pad + j[x:]
        x += pad
    return j, x


def build_icl(images, quality):
    """images: {index: PIL image}. Returns file bytes."""
    n = max(images) + 1
    pos = 0x10 + 4 * n
    table, bodies = [0] * n, []
    for k in range(n):
        if k not in images:
            continue
        im = images[k]
        j = jpeg(im, quality)
        if len(j) > MAX_PICTURE:
            raise SystemExit(f'image {k} is {len(j)} bytes, over the T5L limit')
        j, x = align_scan(j, pos + 10)
        data = len(j) - x
        padded = (data + 3) // 4 * 4 + 8
        body = struct.pack('>HHHI', im.width, im.height, x, padded) + j + b'\0' * (padded - data)
        table[k] = pos
        pos += len(body)
        bodies.append(body)
    rest = struct.pack('>I', pos - 8) + b'\x04' + struct.pack('>H', len(images)) + b'\0'
    rest += b''.join(struct.pack('>I', o) for o in table) + b''.join(bodies)
    return b'DGUS_3' + crc16(rest).to_bytes(2, 'little') + rest


def row_icon(vp, x, y, count, first, lib):
    """Variable icon (0x5A00), background mode: value v shows icon first+v."""
    r = struct.pack('>BBHH', 0x5A, 0x00, 0xFFFF, 0x000A)
    r += struct.pack('>7H', vp, x, y, 0, count - 1, first, first + count - 1)
    return (r + bytes([lib, 1])).ljust(32, b'\0')


def row_text(vp, rect, color, length, font, align):
    """Text display (0x5A11). Encode 0x01 uses the factory ASCII font with half-width
    glyphs; bits .6-.5 set alignment."""
    x0, y0, x1, y1 = rect
    enc = 0x01 | {'l': 0x00, 'r': 0x20, 'c': 0x40}[align]
    r = struct.pack('>BBHH', 0x5A, 0x11, 0xFFFF, 0x000D)
    r += struct.pack('>4H', vp, x0, y0, rgb565(color)) + struct.pack('>5H', x0, y0, x1, y1, length)
    return (r + bytes([0, 0, font, font, enc, 0, 0])).ljust(32, b'\0')


def row_curve(ch, rect, color, vmin, vmax):
    """Real-time curve (0x5A20). Samples are RPM x 1 written with command 0x84."""
    x0, y0, x1, y1 = rect
    mul = int((y1 - y0) * 256 / max(1, vmax - vmin))
    r = struct.pack('>BBHH', 0x5A, 0x20, 0xFFFF, 0x000B)
    r += bytes([0, 0]) + struct.pack('>4H', x0, y0, x1, y1)
    r += struct.pack('>HHHH', (y0 + y1) // 2, (vmin + vmax) // 2, rgb565(color), mul)
    return (r + bytes([ch, 2, 1, 0])).ljust(32, b'\0')


def row_qr(vp, x, y, unit):
    """QR code (0x5A25): auto-sized, black on white."""
    r = struct.pack('>BBHH', 0x5A, 0x25, 0xFFFF, 0x0008)
    r += struct.pack('>4H', vp, x, y, unit) + bytes([0, 0]) + struct.pack('>2H', 0xFFFF, 0x0000)
    return r.ljust(32, b'\0')


def show_file(pages):
    """pages: list (by page id) of lists of 32-byte rows."""
    table, body, pos = bytearray(), bytearray(), 0x4000
    for p in range((0x4000 - 0x10) // 4):
        rows = pages[p] if p < len(pages) else []
        table += struct.pack('<H', len(rows)) + struct.pack('>H', pos)
        for row in rows:
            assert len(row) == 32
            body += row
        pos += 32 * len(rows)
    assert pos + 32 <= 0x40000, '14ShowFile.bin over 256 KB'
    head = b'\x14DGUS_2\x10\x00' + bytes([len(pages) - 1]) + b'\0' * 6
    return head + bytes(table) + bytes(body) + b'\xff' * 32


def touch_key(page, rect, key_vp, code, pressed):
    """Return-key-code touch (0xFE05)."""
    x0, y0, x1, y1 = rect
    r = struct.pack('>8H', page, x0, y0, x1, y1, 0xFF00, pressed, 0xFE05)
    r += bytes([0xFE]) + struct.pack('>H', key_vp) + bytes([0x00]) + struct.pack('>H', code) + bytes([0])
    return r.ljust(32, b'\0')


class Allocator:
    def __init__(self):
        self.next, self.names = VP_BASE, {}

    def get(self, name, words):
        if name in self.names:
            vp, have = self.names[name]
            if have != words:
                raise SystemExit(f'{name}: used with {have} and {words} words')
            return vp
        vp = self.next
        self.next += words
        if self.next > VP_END:
            raise SystemExit('VP space exhausted')
        self.names[name] = (vp, words)
        return vp


class IconLibs:
    """Packs icon strips into libraries, de-duplicating identical strips so the same
    digit style on two pages costs nothing extra."""

    def __init__(self):
        self.libs = [{'id': i, 'images': {}, 'next': 0} for i in ICON_LIBS]
        self.seen = {}
        self.cur = 0

    def add(self, strip):
        key = hashlib.sha1(b''.join(im.tobytes() + bytes(str(im.size), 'ascii') for im in strip)).digest()
        if key in self.seen:
            return self.seen[key]
        lib = self.libs[self.cur]
        size = sum(len(jpeg(im, JPEG_ICON_Q)) + 32 for im in strip)
        lib.setdefault('bytes', 0)
        limit = {23: 9, 15: 7}[lib['id']] * BLOCK - 4096
        if lib['bytes'] + size > limit:
            self.cur += 1
            if self.cur >= len(self.libs):
                raise SystemExit('icon libraries full')
            lib = self.libs[self.cur]
            lib.setdefault('bytes', 0)
        first = lib['next']
        for k, im in enumerate(strip):
            lib['images'][first + k] = im
        lib['next'] += len(strip)
        lib['bytes'] += size
        self.seen[key] = (lib['id'], first)
        return self.seen[key]


def main():
    layout = json.load(open(os.path.join(BUILD, 'layout.json')))
    pages = layout['pages']
    ids = {p['id']: i for i, p in enumerate(pages)}
    alloc = Allocator()
    key_vp = alloc.get('KEY', 1)
    libs = IconLibs()

    # Stable key codes: printable ASCII for keyboard characters, 0x100+ for functions.
    keys = {}
    for p in pages:
        for k in p['keys']:
            n = k['name']
            if n.startswith('CH_'):
                continue
            keys.setdefault(n, 0x100 + len(keys))

    show, touch, maps = [], bytearray(), {'states': {}, 'cells': {}, 'texts': {}}
    for pid, p in enumerate(pages):
        rows = []
        frames, glyphs = {}, {}

        def frame(s):
            if s not in frames:
                frames[s] = Image.open(os.path.join(BUILD, 'states', p['id'], f's{s}.png')).convert('RGB')
            return frames[s]

        def glyph(g):
            if g not in glyphs:
                glyphs[g] = Image.open(os.path.join(BUILD, 'states', p['id'], f'g{g}.png')).convert('RGB')
            return glyphs[g]

        for s in p['states']:
            vp = alloc.get(s['name'], 1)
            maps['states'][s['name']] = (vp, s['n'])
            strip = [frame(i).crop(s['rect']) for i in range(s['n'])]
            lib, first = libs.add(strip)
            rows.append(row_icon(vp, s['rect'][0], s['rect'][1], s['n'], first, lib))

        counts = {}
        for c in p['cells']:
            counts[c['name']] = max(counts.get(c['name'], 0), c['i'] + 1)
        for c in p['cells']:
            base = alloc.get(c['name'], counts[c['name']])
            maps['cells'][c['name']] = (base, counts[c['name']])
            strip = [glyph(g).crop(c['rect']) for g in range(12)]
            lib, first = libs.add(strip)
            rows.append(row_icon(base + c['i'], c['rect'][0], c['rect'][1], 12, first, lib))

        for t in p['texts']:
            words = (t['len'] + 2) // 2 + 1        # room for the 0xFFFF terminator
            vp = alloc.get(t['name'], words)
            maps['texts'][t['name']] = (vp, t['len'])
            rows.append(row_text(vp, t['rect'], t['color'], t['len'], t['font'], t['align']))

        for c in p['curves']:
            rows.append(row_curve(c['ch'], c['rect'], c['color'], c['min'], c['max']))

        for q in p['qrs']:
            vp = alloc.get(q['name'], 80)
            maps['texts'][q['name']] = (vp, 154)
            # 45 units x 3 px centred in the white tile
            rows.append(row_qr(vp, q['rect'][0] + 3, q['rect'][1] + 3, q['unit']))

        show.append(rows)
        if len(rows) > 255:
            raise SystemExit(f"{p['id']}: too many controls")

        for k in p['keys']:
            n = k['name']
            code = int(n[3:], 16) if n.startswith('CH_') else keys[n]
            touch += touch_key(pid, k['rect'], key_vp, code, pid + PRESS_BASE)
    touch += b'\xff\xff'

    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))

    bg = {}
    for pid, p in enumerate(pages):
        bg[pid] = Image.open(os.path.join(BUILD, 'bg', p['id'] + '.png')).convert('RGB')
        bg[pid + PRESS_BASE] = Image.open(os.path.join(BUILD, 'press', p['id'] + '.png')).convert('RGB')
    files = {f'{BG_LIB}.icl': build_icl(bg, JPEG_BG_Q),
             '13TouchFile.bin': bytes(touch),
             '14ShowFile.bin': show_file(show),
             '22_Config.bin': build_config()}
    for lib in libs.libs:
        if lib['images']:
            files[f"{lib['id']}.icl"] = build_icl(lib['images'], JPEG_ICON_Q)

    used = {}
    for name, data in files.items():
        start = int(name[:2])
        for b in range(start, start + (len(data) + BLOCK - 1) // BLOCK):
            if b >= FLASH_BLOCKS or b in used:
                raise SystemExit(f'{name} collides with {used.get(b)} at flash block {b}')
            used[b] = name
        with open(os.path.join(OUT, name), 'wb') as f:
            f.write(data)
        print(f'{name:18s} {len(data) / 1024:8.1f} KB  blocks {start}..{start + (len(data) - 1) // BLOCK}')

    write_header(ids, keys, alloc, maps)
    json.dump({'pages': [p['id'] for p in pages], 'press_base': PRESS_BASE, 'key_vp': key_vp,
               'keys': keys, 'cells': maps['cells'], 'states': maps['states'], 'texts': maps['texts']},
              open(os.path.join(ROOT, 'DWIN_SET.map.json'), 'w'), indent=1)
    print(f'VP used 0x{VP_BASE:04X}..0x{alloc.next - 1:04X}; {len(keys)} function keys; {sum(len(r) for r in show)} controls')


def write_header(ids, keys, alloc, maps):
    o = ['// Generated by hmi/src/pack.py. Do not edit: change hmi/src/pages.mjs and rebuild.',
         '#pragma once', '#include <stdint.h>', '', 'namespace hmi {', '',
         'enum Page : uint16_t {']
    o += [f'  PAGE_{k} = {v},' for k, v in ids.items()]
    o += [f'  PAGE_COUNT = {len(ids)},', '};', '', 'enum Key : uint16_t {']
    o += [f'  KEY_{k} = 0x{v:03X},' for k, v in keys.items()]
    o += ['};', '', f'constexpr uint16_t VP_KEY = 0x{alloc.names["KEY"][0]:04X};', '',
          '// Digit cells: one word per digit, 0-9, DIGIT_DASH or DIGIT_BLANK',
          'constexpr uint16_t DIGIT_DASH = 10, DIGIT_BLANK = 11;']
    for k, (vp, n) in sorted(maps['cells'].items()):
        o.append(f'constexpr uint16_t N_{k} = 0x{vp:04X}; constexpr uint8_t N_{k}_LEN = {n};')
    o += ['', '// Variable icons: value selects a state']
    for k, (vp, n) in sorted(maps['states'].items()):
        o.append(f'constexpr uint16_t S_{k} = 0x{vp:04X}; constexpr uint8_t S_{k}_N = {n};')
    o += ['', '// Text fields: VP and maximum characters']
    for k, (vp, n) in sorted(maps['texts'].items()):
        o.append(f'constexpr uint16_t T_{k} = 0x{vp:04X}; constexpr uint8_t T_{k}_LEN = {n};')
    o += [f'constexpr uint16_t VP_TOP = 0x{alloc.next:04X};', '', '}  // namespace hmi', '']
    os.makedirs(os.path.dirname(HEADER), exist_ok=True)
    open(HEADER, 'w', newline='\n').write('\n'.join(o))


if __name__ == '__main__':
    sys.exit(main())
