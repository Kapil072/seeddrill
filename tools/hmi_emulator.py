#!/usr/bin/env python3
"""Emulates the DWIN panel on the PC and walks the whole operator workflow.

Reads the *generated* DWIN_SET files, not the design sources: backgrounds and icons
come out of the .icl containers, controls out of 14ShowFile.bin, touches are resolved
through 13TouchFile.bin. Firmware behaviour comes from the real Ui code running in
emu_host. A wrong byte anywhere in that chain fails here.

Usage: python tools/hmi_emulator.py [--shots DIR]
"""
import argparse
import io
import json
import os
import struct
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SET = os.path.join(ROOT, 'hmi', 'DWIN_SET')
HOST = os.path.join(ROOT, 'firmware', 'test', 'host', 'out', 'emu_host.exe')


def crc16(data):
    c = 0xFFFF
    for b in data:
        c ^= b
        for _ in range(8):
            c = (c >> 1) ^ (0xA001 if c & 1 else 0)
    return c


def sos_end(j):
    i = 2
    while True:
        m, n = j[i + 1], int.from_bytes(j[i + 2:i + 4], 'big')
        if m == 0xDA:
            return i + 2 + n
        i += 2 + n


def plain_jpeg(j):
    """Undo the SOS stretching so Pillow accepts the stream."""
    x = sos_end(j)
    s = j.rfind(b'\xff\xda', 0, x)
    return j[:s + 2] + (12).to_bytes(2, 'big') + j[s + 4:s + 14] + j[s + 2 + int.from_bytes(j[s + 2:s + 4], 'big'):]


class Icl:
    def __init__(self, path):
        d = open(path, 'rb').read()
        assert d[:6] == b'DGUS_3', path
        assert d[6:8] == crc16(d[8:]).to_bytes(2, 'little'), f'{path}: bad CRC'
        assert struct.unpack('>I', d[8:12])[0] == len(d) - 8, f'{path}: bad length'
        self.d = d
        self.count = int.from_bytes(d[13:15], 'big')
        self.table = []
        i = 0x10
        first = None
        while first is None or i < first:
            o = struct.unpack('>I', d[i:i + 4])[0]
            self.table.append(o)
            if o and first is None:
                first = o
            i += 4
        self.cache = {}

    def image(self, k):
        if k not in self.cache:
            o = self.table[k]
            assert o, f'no image {k}'
            w, h, x, n = struct.unpack('>HHHI', self.d[o:o + 10])
            assert (o + 10 + x) % 4 == 0, 'entropy data not aligned'
            jpg = self.d[o + 10:o + 10 + x + n]
            self.cache[k] = Image.open(io.BytesIO(plain_jpeg(jpg))).convert('RGB')
            assert self.cache[k].size == (w, h)
        return self.cache[k]


class Panel:
    def __init__(self):
        self.bg = Icl(os.path.join(SET, '32.icl'))
        self.icons = {int(f[:2]): Icl(os.path.join(SET, f)) for f in os.listdir(SET) if f.endswith('.icl') and f != '32.icl'}
        self.map = json.load(open(os.path.join(ROOT, 'hmi', 'DWIN_SET.map.json')))
        self.parse_show()
        self.parse_touch()
        self.vp = {}
        self.page = 0
        self.config = None
        self.bright = None
        self.beeps = 0
        self.curve = []
        self.font_cache = {}

    def parse_show(self):
        d = open(os.path.join(SET, '14ShowFile.bin'), 'rb').read()
        assert d[:7] == b'\x14DGUS_2'
        self.rows = {}
        for p in range(d[9] + 1):
            n = struct.unpack('<H', d[0x10 + 4 * p:0x12 + 4 * p])[0]
            pos = struct.unpack('>H', d[0x12 + 4 * p:0x14 + 4 * p])[0]
            self.rows[p] = [d[pos + 32 * i:pos + 32 * i + 32] for i in range(n)]

    def parse_touch(self):
        d = open(os.path.join(SET, '13TouchFile.bin'), 'rb').read()
        self.keys = {}
        i = 0
        while d[i:i + 2] != b'\xff\xff':
            pg, x0, y0, x1, y1, nxt, on, code = struct.unpack('>8H', d[i:i + 16])
            assert code == 0xFE05 and nxt == 0xFF00 and d[i + 16] == 0xFE
            vp = struct.unpack('>H', d[i + 17:i + 19])[0]
            key = struct.unpack('>H', d[i + 20:i + 22])[0]
            self.keys.setdefault(pg, []).append((x0, y0, x1, y1, vp, key, on))
            i += 32

    def word(self, a):
        return self.vp.get(a, 0)

    def write(self, addr, data):
        for i in range(0, len(data) - 1, 2):
            self.vp[addr + i // 2] = data[i] << 8 | data[i + 1]
        if addr == 0x0084:
            assert data[0] == 0x5A and data[1] == 0x01
            self.page = data[2] << 8 | data[3]
            assert self.bg.table[self.page], f'page {self.page} has no image'
        elif addr == 0x0080:
            self.config = data
        elif addr == 0x0082:
            self.bright = data[0]
        elif addr == 0x00A0:
            self.beeps += 1

    def frame(self, b):
        """Handles one frame from the controller; returns reply bytes or None."""
        assert b[:2] == b'\x5a\xa5', b.hex()
        assert b[2] == len(b) - 3, f'length byte wrong: {b.hex()}'
        cmd = b[3]
        if cmd == 0x82:
            self.write(b[4] << 8 | b[5], b[6:])
            return None
        if cmd == 0x83:
            a, n = b[4] << 8 | b[5], b[6]
            if a == 0x0014:
                return bytes([0x5A, 0xA5, 4 + 2, 0x83, b[4], b[5], 1, self.page >> 8, self.page & 255])
            return None
        if cmd == 0x84:
            self.curve.append(b[5] << 8 | b[6])
            return None
        raise AssertionError(f'unknown command {cmd:02x}')

    def font(self, px):
        if px not in self.font_cache:
            self.font_cache[px] = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', int(px * 0.92))
        return self.font_cache[px]

    def read_text(self, vp, maxlen):
        out = bytearray()
        for i in range((maxlen + 3) // 2):
            w = self.word(vp + i)
            for c in (w >> 8, w & 255):
                if c in (0xFF, 0):
                    return out.decode('ascii', 'replace')
                out.append(c)
        return out.decode('ascii', 'replace')

    def render(self):
        im = self.bg.image(self.page).copy()
        dr = ImageDraw.Draw(im)
        for r in self.rows.get(self.page, []):
            kind = r[1]
            if kind == 0x00:
                vp, x, y, vmin, vmax, imin, imax = struct.unpack('>7H', r[6:20])
                lib, mode = r[20], r[21]
                assert mode == 1
                v = self.word(vp)
                if vmin <= v <= vmax:
                    icon = self.icons[lib].image(imin + v)
                    im.paste(icon, (x, y))
            elif kind == 0x11:
                vp, x, y, col = struct.unpack('>4H', r[6:14])
                x0, y0, x1, y1, n = struct.unpack('>5H', r[14:24])
                fx, fy, enc = r[26], r[27], r[28]
                s = self.read_text(vp, n)
                colour = ((col >> 11) << 3, ((col >> 5) & 63) << 2, (col & 31) << 3)
                cw = fx // 2
                assert len(s) * cw <= x1 - x0 + cw, f'text overflows box: {s!r}'
                tw = len(s) * cw
                al = (enc >> 5) & 3
                tx = x0 if al == 0 else (x1 - tw if al == 1 else (x0 + x1 - tw) // 2)
                for i, c in enumerate(s):
                    dr.text((tx + i * cw, y0), c, fill=colour, font=self.font(fy))
            elif kind == 0x20:
                x0, y0, x1, y1 = struct.unpack('>4H', r[8:16])
                yc, vc, col, mul = struct.unpack('>4H', r[16:24])
                dis = r[25]
                pts = self.curve[-((x1 - x0) // dis):]
                colour = ((col >> 11) << 3, ((col >> 5) & 63) << 2, (col & 31) << 3)
                xy = [(x1 - (len(pts) - 1 - i) * dis, max(y0, min(y1, yc - (v - vc) * mul // 256))) for i, v in enumerate(pts)]
                if len(xy) > 1:
                    dr.line(xy, fill=colour, width=2)
            elif kind == 0x25:
                vp, x, y, unit = struct.unpack('>4H', r[6:14])
                s = self.read_text(vp, 154)
                if s:
                    # A placeholder pattern: the panel draws the real code.
                    w = 45 * unit
                    dr.rectangle((x, y, x + w, y + w), fill=(255, 255, 255))
                    h = sum(map(ord, s))
                    for a in range(45):
                        for b in range(45):
                            edge = (a < 7 or a > 37) and (b < 7)or (a < 7 and b > 37)
                            bit = (a * 31 + b * 17 + h * (a ^ b)) % 3 == 0 if not edge else (a % 6 == 0 or b % 6 == 0 or a in (2, 3, 4, 40, 41, 42) and b in (2, 3, 4, 40, 41, 42))
                            if bit:
                                dr.rectangle((x + a * unit, y + b * unit, x + a * unit + unit - 1, y + b * unit + unit - 1), fill=(0, 0, 0))
        return im

    def key_at(self, x, y):
        for k in self.keys.get(self.page, []):
            if k[0] <= x <= k[2] and k[1] <= y <= k[3]:
                return k
        return None


class Emu:
    def __init__(self, shots):
        self.panel = Panel()
        self.p = subprocess.Popen([HOST], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.shots = shots
        self.n = 0
        self.names = self.panel.map['pages']
        self.keynames = self.panel.map['keys']
        self.tx_bytes = 0
        self.pump()

    def read_until_ok(self):
        replies = []
        while True:
            line = self.p.stdout.readline()
            assert line, 'host exited'
            line = line.strip()
            if line == 'ok':
                return replies
            if line.startswith('tx '):
                b = bytes.fromhex(line[3:])
                self.tx_bytes += len(b)
                r = self.panel.frame(b)
                if r:
                    replies.append(r)

    def send(self, s):
        self.p.stdin.write(s + '\n')
        self.p.stdin.flush()
        return self.read_until_ok()

    def pump(self):
        self.send('run 0')

    def run(self, ms):
        left = ms
        while left > 0:
            step = min(100, left)
            for r in self.send(f'run {step}'):
                self.send('rx ' + r.hex())
            left -= step

    def in_(self, name, v):
        self.send(f'in {name} {v}')

    def touch(self, name):
        """Presses a key by name, going through the coordinates in 13TouchFile.bin."""
        pg = self.panel.page
        code = int(name[3:], 16) if name.startswith('CH_') else self.keynames[name]
        for k in self.panel.keys.get(pg, []):
            if k[5] == code:
                x, y = (k[0] + k[2]) // 2, (k[1] + k[3]) // 2
                hit = self.panel.key_at(x, y)
                assert hit and hit[5] == code, f'{name}: covered by another key'
                assert hit[4] == self.panel.map['key_vp']
                frame = bytes([0x5A, 0xA5, 6, 0x83, hit[4] >> 8, hit[4] & 255, 1, code >> 8, code & 255])
                self.send('rx ' + frame.hex())
                self.run(240)
                return
        raise AssertionError(f'key {name} is not on page {self.page()}')

    def type(self, s):
        for c in s:
            self.touch('CH_%02x' % ord(c))

    def page(self):
        return self.names[self.panel.page]

    def text(self, name):
        vp, n = self.panel.map['texts'][name]
        return self.panel.read_text(vp, n)

    def state(self, name):
        return self.panel.word(self.panel.map['states'][name][0])

    def num(self, name):
        vp, n = self.panel.map['cells'][name]
        s = ''
        for i in range(n):
            d = self.panel.word(vp + i)
            s += {10: '-', 11: ' '}.get(d, str(d))
        return s

    def shot(self, label):
        if self.shots:
            self.n += 1
            self.panel.render().save(os.path.join(self.shots, f'{self.n:02d}_{label}.png'))
        else:
            self.panel.render()

    def close(self):
        self.p.stdin.write('quit\n')
        self.p.stdin.close()
        self.p.wait()


results = []


def check(cond, what):
    results.append((bool(cond), what))
    print(('  ok   ' if cond else '  FAIL ') + what)


def scenario(e):
    print('boot')
    e.run(1000)
    check(e.panel.config == bytes([0x5A, 0, 0, 0x38]), 'touch auto-upload forced on after first reply')
    check(e.panel.bright == 80, 'brightness applied')
    check(e.page() == 'BOOT', 'boot page shown')
    e.shot('boot')
    e.run(2500)
    check(e.page() == 'HOME', 'home page reached')
    check(e.text('PROFILE') == 'Vishwakarma 11 row', 'profile name on home')
    check(e.state('H_SIM') == 1, 'simulation badge on')
    e.shot('home')

    print('test setup and numeric entry')
    e.touch('GO_SETUP')
    check(e.page() == 'SETUP', 'setup page')
    check(e.num('C_OUT').strip().replace(' ', '') == '531', 'calculated output 53.1 RPM for 5 km/h, 0.5 m wheel')
    e.touch('F_SPEED')
    check(e.page() == 'NUMPAD' and e.text('NP_TITLE') == 'Travel speed', 'numpad opened for speed')
    e.type('6.5')
    e.shot('numpad')
    e.touch('NP_OK')
    check(e.page() == 'SETUP' and e.num('F_SPEED_V').replace(' ', '') == '65', 'speed saved as 6.5')
    check(e.num('C_OUT').replace(' ', '') == '690', 'output recalculated to 69.0 RPM')
    check(e.state('VALID') == 0, 'within limits')
    e.shot('setup')

    e.touch('F_SPEED')
    e.type('20')
    e.touch('NP_OK')
    check(e.state('VALID') == 1, '20 km/h rejected as out of range')
    check('fast' in e.text('VALID_MSG'), 'reason shown')
    e.shot('setup_out_of_range')
    e.touch('GO_PRE')
    check(e.page() == 'SETUP', 'cannot continue with an invalid target')
    e.touch('F_SPEED')
    e.type('999')
    e.touch('NP_OK')
    check(e.page() == 'NUMPAD' and 'range' in e.text('NP_ERR'), 'numpad refuses a value outside its range')
    e.touch('NP_CANCEL')
    e.touch('F_SPEED')
    e.type('5')
    e.touch('NP_OK')

    e.touch('F_LENGTH')
    e.touch('NP_CLEAR')
    e.type('0.5')
    e.touch('NP_OK')
    check(e.num('F_LENGTH_V').replace(' ', '') == '05', 'duration set to 0.5 min')

    print('transmission and crop')
    e.touch('GO_TRANS')
    check(e.page() == 'TRANS', 'transmission page')
    e.touch('F_S1A')
    e.type('15')
    e.touch('NP_OK')
    e.touch('F_S1B')
    e.type('30')
    e.touch('NP_OK')
    check(e.num('C_RATIO').replace(' ', '') == '0500', 'ratio 15/30 = 0.500')
    e.shot('transmission')
    e.touch('BACK')
    check(e.page() == 'SETUP', 'back returns to setup')
    e.touch('GO_CROP')
    e.touch('CROP1')
    check(e.num('F_SPACING_V').replace(' ', '') == '50' and e.num('F_CELLS_V').strip() == '30', 'soybean preset loaded')
    check(e.num('K_DISC').replace(' ', '') == '556', 'disc speed 55.6 RPM at 5 km/h')
    e.shot('crop')
    e.touch('BACK')

    print('pre-start checks')
    e.in_('guard', 0)
    e.run(300)
    e.touch('GO_PRE')
    check(e.page() == 'PRE', 'pre-check page')
    check(e.state('PRE1') == 0, 'open guard shows red')
    e.touch('PRE_CONFIRM')
    check(e.state('PRE_GO') == 0, 'start stays locked with guard open')
    b = e.panel.beeps
    e.touch('RUN_START')
    check(e.page() == 'PRE' and e.panel.beeps > b, 'start refused with an error beep')
    e.shot('precheck_blocked')
    e.in_('guard', 1)
    e.run(300)
    check(e.state('PRE_GO') == 1, 'start unlocks when all checks pass')
    e.shot('precheck_ok')

    print('test run')
    e.touch('RUN_START')
    e.run(400)
    check(e.page() == 'RUN', 'run page')
    e.run(9000)
    check(e.state('GAUGE') > 20, 'gauge moved')
    e.shot('running')
    e.run(26000)
    check(e.page() == 'DONE', 'report opens by itself at test end')
    check(e.text('R_TITLE').startswith('Test #1'), 'report title')
    check(e.state('R_STATE') == 0, 'completed')
    print('     avg', e.num('R_RPM'), 'acc', e.num('R_ACC'), 'dist', e.num('R_DIST'))
    check(int(e.num('R_ACC').replace(' ', '')) > 980, 'speed accuracy above 98 %')

    print('manual measurement')
    e.touch('F_SEED')
    e.type('250.5')
    e.touch('NP_OK')
    check(e.page() == 'DONE', 'back on report')
    check(e.num('F_SEED_V').replace(' ', '') == '2505', 'seed mass stored')
    check(int(e.num('R_SEED').replace(' ', '')) > 0, 'kg/ha calculated')
    e.shot('report')
    e.touch('R_HOME')
    check(e.page() == 'HOME' and e.state('STATE') == 0, 'finish returns home, ready')

    print('emergency stop during a run')
    e.touch('GO_PRE')
    e.touch('PRE_CONFIRM')
    e.touch('RUN_START')
    e.run(7000)
    e.in_('estop', 0)
    e.run(400)
    check(e.page() == 'FAULT', 'fault page on E-stop')
    check('Emergency stop' in e.text('FT_TITLE'), 'fault named')
    e.shot('fault')
    e.touch('FT_RESET')
    check(e.page() == 'FAULT', 'reset refused while E-stop is still pressed')
    e.in_('estop', 1)
    e.run(300)
    check(e.page() == 'FAULT', 'stays latched after release')
    e.touch('FT_RESET')
    e.run(500)
    check(e.page() == 'HOME', 'reset returns home')
    check(e.state('STATE') == 0, 'ready, nothing started')

    print('reports list')
    e.touch('GO_REPORTS')
    check(e.page() == 'REPORTS', 'reports page')
    check('#2' in e.text('RP0_T') and 'FAULT' in e.text('RP0_T'), 'newest test first, marked as faulted')
    check('#1' in e.text('RP1_T') and 'OK' in e.text('RP1_T'), 'older test listed')
    e.shot('reports')
    e.touch('RP1')
    check(e.page() == 'DONE' and e.num('F_SEED_V').replace(' ', '') == '2505', 'opens report with saved measurement')
    e.touch('R_HOME')
    check(e.page() == 'REPORTS', 'returns to the list')
    e.touch('BACK')

    print('Wi-Fi')
    e.touch('GO_SETTINGS')
    e.shot('settings')
    e.touch('GO_WIFI')
    check(e.page() == 'WIFI', 'wifi page')
    e.run(600)
    check(e.text('WF0_T') == 'SPK-Workshop' and e.state('WF0_SIG') == 4 and e.state('WF0_L') == 1, 'scan list with signal and lock')
    check(e.state('WF1_L') == 0, 'open network has no lock')
    e.shot('wifi_scan')
    e.touch('WF_NEXT')
    check(e.text('WF0_T') == 'Machine-Shop-AP', 'page two of networks')
    e.touch('WF_PREV')
    e.touch('WF0')
    check(e.page() == 'KB_LOW', 'password keyboard for a secured network')
    e.type('short')
    b = e.panel.beeps
    e.touch('KB_OK')
    check(e.page().startswith('KB') and e.panel.beeps > b, 'password under 8 characters refused')
    for _ in range(5):
        e.touch('KB_BKSP')
    e.type('wrongpass')
    check(set(e.text('KB_VALUE')) <= set('*_'), 'password is masked')
    e.touch('KB_SHOW')
    check(e.text('KB_VALUE') == 'wrongpass_', 'eye reveals password')
    e.shot('keyboard')
    e.touch('KB_OK')
    e.run(800)
    check(e.page() == 'WIFI' and e.state('W_STATE') == 4, 'wrong password reported')
    e.shot('wifi_wrong_password')
    e.touch('WF0')
    e.touch('KB_UPPER')
    check(e.page() == 'KB_UP', 'shift shows capitals')
    e.touch('CH_43')
    check(e.page() == 'KB_LOW', 'shift is one-shot')
    e.touch('KB_SYM')
    check(e.page() == 'KB_SYM', 'symbols layout')
    e.shot('keyboard_symbols')
    e.touch('CH_21')
    e.touch('KB_LOWER')
    for _ in range(2):
        e.touch('KB_BKSP')
    e.type('correcthorse')
    e.touch('KB_OK')
    e.run(800)
    check(e.state('W_STATE') == 2 and e.state('WF0_ON') == 1, 'connected')
    check('192.168.1.57' in e.text('W_STATUS'), 'IP shown')
    check(e.state('H_WIFI') == 4 and e.state('H_CLOUD') == 2, 'header shows wifi and cloud online')
    e.shot('wifi_connected')
    e.touch('BACK')

    print('remote permission and drive settings')
    e.touch('REMOTE_ON')
    e.run(300)
    check(e.state('REMOTE') == 1 and e.state('H_REMOTE') == 1, 'remote control allowed')
    e.touch('GO_DRIVE')
    check(e.page() == 'NUMPAD', 'PIN requested')
    e.type('1111')
    e.touch('NP_OK')
    check(e.page() == 'NUMPAD' and 'PIN' in e.text('NP_ERR'), 'wrong PIN refused')
    e.type('2580')
    check(e.text('NP_VALUE') == '****', 'PIN is masked')
    e.touch('NP_OK')
    check(e.page() == 'DRIVE', 'drive settings open')
    check(e.num('F_GEAR_V').replace(' ', '') == '1500', 'gearbox 15.00')
    e.shot('drive')
    e.touch('BACK')
    check(e.page() == 'SETTINGS', 'back to settings, PIN page skipped')
    e.touch('GO_DIAG')
    e.run(1200)
    e.shot('diagnostics')
    e.touch('BACK')
    e.touch('BACK')

    print('profiles')
    e.touch('GO_PROFILES')
    check('Vishwakarma' in e.text('PF0_T') and 'Demo 15' in e.text('PF1_T'), 'profiles listed')
    e.touch('PF1')
    check(e.text('PF1_T').startswith('>'), 'selection marker')
    e.touch('PF_LOAD')
    check(e.state('PF1_ON') == 1 and e.state('PF0_ON') == 0, 'profile loaded')
    e.touch('PF_RENAME')
    check(e.page() == 'KB_LOW' and 'Demo' in e.text('KB_VALUE'), 'rename shows current name')
    for _ in range(4):
        e.touch('KB_BKSP')
    e.type('new')
    e.touch('KB_OK')
    check('new' in e.text('PF1_T'), 'renamed')
    e.touch('PF2')
    e.touch('PF_SAVE')
    check('(empty)' not in e.text('PF2_T'), 'saved into empty slot')
    e.touch('PF_DELETE')
    check(e.page() == 'CONFIRM', 'delete asks first')
    e.shot('confirm')
    e.touch('CF_YES')
    check(e.page() == 'PROFILES' and '(empty)' in e.text('PF2_T'), 'deleted')
    e.shot('profiles')
    e.touch('PF_NEXT')
    check('07' in e.text('PF0_T'), 'second page of slots')
    e.touch('BACK')
    check(e.page() == 'HOME', 'home')

    print(f'wire traffic: {e.tx_bytes} bytes over the session')


def all_keys_reachable(e):
    """Every key in the map must exist on a page, and every page's keys must be inside the screen."""
    seen = set()
    for pg, ks in e.panel.keys.items():
        for k in ks:
            assert 0 <= k[0] < k[2] <= 1024 and 0 <= k[1] < k[3] <= 600, f'key off-screen on page {pg}'
            assert (k[2] - k[0]) >= 40 and (k[3] - k[1]) >= 40, f'key too small to press: page {pg} {k[:4]}'
            seen.add(k[5])
    missing = [n for n, c in e.keynames.items() if c not in seen]
    check(not missing, 'every function key has a touch area and is at least 40 px')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--shots')
    a = ap.parse_args()
    if a.shots:
        os.makedirs(a.shots, exist_ok=True)
    e = Emu(a.shots)
    try:
        all_keys_reachable(e)
        scenario(e)
    finally:
        e.close()
    bad = [w for ok, w in results if not ok]
    print(f'\n{len(results) - len(bad)} passed, {len(bad)} failed')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
