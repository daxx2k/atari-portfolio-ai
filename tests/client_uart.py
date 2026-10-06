"""Exercise the real COM binary in 16-bit emulation with BIOS service doubles.

This validates client logic, not Atari BIOS timing or the physical serial link.
"""
import json
import sys
from collections import deque
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent / 'pydeps'))
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_INSN
from unicorn.x86_const import *

BINARY = Path(sys.argv[1] if len(sys.argv) > 1 else str(Path(__file__).resolve().parents[1]/'portfolio/AI.COM')).read_bytes()
ENGLISH = b'Robot terminal' in BINARY or b'FIELD TERMINAL' in BINARY
def ui(text):
    translations = {
        'Tasto: pagina seguente':'Any key: next page',
        'Testo troncato':'Text truncated',
        'Modulo seriale non rilevato':'Serial interface not detected',
        'Errore trasmissione':'Serial transmission error',
        'Errore ricezione':'Serial receive error',
        '180 secondi':'180 seconds',
    }
    return translations.get(text,text) if ENGLISH else text


def exercise(keys, answer=b'ESP32 OK\nCiao!\x04', *, port=True, tx_fail=False,
             timeout=False, cancel=False, base=0x8070, rx_error=False, setup_file=None, file_error=2,
             file_state=None, write_fail=False, rename_fail=False):
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, 1024*1024)
    uc.mem_write(0x10100, BINARY)
    for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
        uc.reg_write(r, 0x1000)
    uc.reg_write(UC_X86_REG_SP, 0xfffe)
    uc.reg_write(UC_X86_REG_IP, 0x100)
    uc.mem_write(0x400, base.to_bytes(2, "little"))
    ier, mcr = 0x0b, 0x10
    original_ier, original_mcr = ier, mcr
    io_reads = []
    io_writes = []
    inputs = deque(keys)
    received = deque()
    sent = bytearray()
    written = []
    screens = []
    rows = [[' ']*40 for _ in range(8)]
    row = col = 0
    stage = 'input'
    clock = 0
    exit_code = None
    old_mode = 2
    modes = []
    initialized = False
    files={} if file_state is None else file_state
    if setup_file is not None:files[b'A:\\AISETUP.TXT']=setup_file
    handles={};next_handle=5

    def dos_name(address):
        raw=bytes(uc.mem_read(address,80));return raw.split(b'\0')[0]

    def char(ch):
        nonlocal row, col, rows
        written.append(chr(ch))
        if ch == 13: col = 0
        elif ch == 10: row += 1
        elif ch == 8: col = max(0, col-1)
        else:
            rows[row][col] = chr(ch)
            col += 1
            if col == 40: col=0; row+=1
        if row >= 8:
            rows.pop(0); rows.append([' ']*40); row=7

    def interrupt(u, number, _):
        nonlocal row, col, stage, clock, exit_code, rows, initialized, next_handle
        ax = u.reg_read(UC_X86_REG_AX)
        ah, al = ax >> 8, ax & 255
        dx = u.reg_read(UC_X86_REG_DX)
        if number == 0x61:
            if ah == 0:
                assert not initialized, 'Repeated extended BIOS initialization'
                initialized = True
                return
            if len(sys.argv) > 1:
                assert initialized, 'Extended BIOS used before initialization'
            assert ah == 0x0e
            if al == 0:
                u.reg_write(UC_X86_REG_DX, old_mode)
            else:
                modes.append(dx & 255)
        elif number == 0x11:
            u.reg_write(UC_X86_REG_AX, 0x0200 if port else 0)
        elif number == 0x14:
            assert dx == 0, 'Unexpected serial port'
            if ah == 0:
                assert al == 0xe3, 'Wrong 9600/8N1 config'
                u.reg_write(UC_X86_REG_AX, 0x6000)
            else: raise AssertionError('Data transfer unexpectedly used BIOS handshake service')
        elif number == 0x16:
            # Silent startup drains the settings reply and returns directly to input.
            if stage == 'display' and ''.join(written[-10:]).endswith('OPERATOR> '):
                stage = 'input'
            available = bool(inputs) if stage == 'input' else stage == 'display' or cancel
            if ah == 1:
                flags = u.reg_read(UC_X86_REG_EFLAGS)
                u.reg_write(UC_X86_REG_EFLAGS, (flags & ~64) if available else flags | 64)
            else:
                assert ah == 0
                if stage == 'input' and inputs: c = inputs.popleft()
                elif stage == 'display' and ui('Tasto: pagina seguente') in ''.join(written[-100:]): c = 32
                elif stage == 'display' and inputs: c = 32;stage='input'
                else: c = 27
                u.reg_write(UC_X86_REG_AX, {128:0x4b00,129:0x4d00,130:0x4800,131:0x5000}.get(c,c))
        elif number == 0x10:
            if ah == 6:
                screens.append([''.join(r) for r in rows])
                rows = [[' ']*40 for _ in range(8)]
            elif ah == 2: row, col = dx >> 8, dx & 255
            elif ah == 3: u.reg_write(UC_X86_REG_DX, row*256+col)
            elif ah == 0xe: char(al)
            else: raise AssertionError(f'Unexpected video {ax:04x}')
        elif number == 0x1a:
            assert ah == 2
            clock += 1
            u.reg_write(UC_X86_REG_DX, (clock % 60) << 8)
        elif number == 0x21:
            flags=u.reg_read(UC_X86_REG_EFLAGS)
            if ah==0x3d:
                address=u.reg_read(UC_X86_REG_DS)*16+dx
                name=dos_name(address);assert name==b'A:\\AISETUP.TXT'
                missing=name not in files
                if not missing:handles[next_handle]=name;next_handle+=1
                u.reg_write(UC_X86_REG_AX,file_error if missing else next_handle-1)
                u.reg_write(UC_X86_REG_EFLAGS,flags|1 if missing else flags&~1)
            elif ah==0x3f:
                chunk=files[handles[u.reg_read(UC_X86_REG_BX)]][:u.reg_read(UC_X86_REG_CX)]
                u.mem_write(u.reg_read(UC_X86_REG_DS)*16+dx,chunk)
                u.reg_write(UC_X86_REG_AX,len(chunk));u.reg_write(UC_X86_REG_EFLAGS,flags&~1)
            elif ah==0x3e:
                del handles[u.reg_read(UC_X86_REG_BX)]
                u.reg_write(UC_X86_REG_EFLAGS,flags&~1)
            elif ah==0x3c:
                name=dos_name(u.reg_read(UC_X86_REG_DS)*16+dx)
                files[name]=b'';handles[next_handle]=name;next_handle+=1
                u.reg_write(UC_X86_REG_AX,next_handle-1);u.reg_write(UC_X86_REG_EFLAGS,flags&~1)
            elif ah==0x40:
                chunk=bytes(u.mem_read(u.reg_read(UC_X86_REG_DS)*16+dx,u.reg_read(UC_X86_REG_CX)))
                if not write_fail:files[handles[u.reg_read(UC_X86_REG_BX)]]=chunk
                u.reg_write(UC_X86_REG_AX,0 if write_fail else len(chunk));u.reg_write(UC_X86_REG_EFLAGS,flags&~1)
            elif ah==0x41:
                name=dos_name(u.reg_read(UC_X86_REG_DS)*16+dx);missing=name not in files
                if not missing:del files[name]
                u.reg_write(UC_X86_REG_AX,2 if missing else 0);u.reg_write(UC_X86_REG_EFLAGS,flags|1 if missing else flags&~1)
            elif ah==0x56:
                old=dos_name(u.reg_read(UC_X86_REG_DS)*16+dx)
                new=dos_name(u.reg_read(UC_X86_REG_ES)*16+u.reg_read(UC_X86_REG_DI))
                failed=old not in files or new in files or (rename_fail and old.endswith(b'.NEW'))
                if not failed:files[new]=files.pop(old)
                u.reg_write(UC_X86_REG_AX,2 if old not in files and failed else 5 if failed else 0)
                u.reg_write(UC_X86_REG_EFLAGS,flags|1 if failed else flags&~1)
            else:
                assert ah == 0x4c
                exit_code = al
                u.emu_stop()
        else: raise AssertionError(f'Unexpected interrupt {number:x}')

    def io_in(u, address, size, _):
        io_reads.append(address)
        assert size == 1
        if address == base + 1: return ier
        if address == base + 4: return mcr
        if address == base + 5:
            return (0 if tx_fail else 0x60) | (1 if received else 0) | (0x02 if rx_error and stage == 'receive' else 0)
        if address == base:
            assert received, 'Read empty receiver'
            c = received.popleft()
            if c == 4: stage_change('display')
            return c
        raise AssertionError(f'Unexpected UART read {address:x}')

    def stage_change(value):
        nonlocal stage
        stage = value

    def io_out(u, address, size, value, _):
        nonlocal ier, mcr, stage
        io_writes.append((address, value))
        assert size == 1
        if address == base + 1: ier = value
        elif address == base + 4: mcr = value
        elif address == base:
            assert ier == 0, 'Polling UART left interrupts enabled'
            assert mcr == 3, 'RTS/DTR not set or loopback still enabled'
            sent.append(value)
            if value == 10:
                last=bytes(sent).rstrip(b'\n').split(b'\n')[-1]
                if last.startswith(b'@SETUP ') and not last.startswith(b'@SETUP END '):return
                stage = 'receive'
                if not timeout:
                    binary=last in (b'/frame',b'/liveframe',b'/talk',b'/talkpoll',b'/talkstop')
                    received.extend(answer if binary or b'\x02' in answer else b'\x02'+answer)
        else: raise AssertionError(f'Unexpected UART write {address:x}')

    uc.hook_add(UC_HOOK_INSN, io_in, None, 1, 0, UC_X86_INS_IN)
    uc.hook_add(UC_HOOK_INSN, io_out, None, 1, 0, UC_X86_INS_OUT)
    uc.hook_add(UC_HOOK_INTR, interrupt)
    uc.emu_start(0x10100, 0xfffff, count=10_000_000)
    assert exit_code == 0, 'Client did not exit normally'
    assert modes == [1, old_mode], 'Display mode was not restored'
    assert (ier, mcr) == (original_ier, original_mcr), 'UART control registers not restored'
    assert base + 6 not in io_reads, 'Unexpected modem handshake dependency'
    if not port or base in (0, 0xffff):
        assert not io_reads and not io_writes, 'Touched UART without valid port'
    return bytes(sent), ''.join(written), screens


results=[]
sent, text, _ = exercise(b'/test\r')
assert sent == b'/test\n' and 'ESP32 OK' in text
results.append('serial request and reply')
sent, text, pages = exercise(b'ciao\r', answer=(b'1234567890'*40+b'\n')*4+b'\x04')
assert sent == b'ciao\n' and len(pages)>4
results.append('40-column pagination')
sent, _, _ = exercise(b'x'*37+b'y\x08Z\r')
assert sent == b'x'*37+b'Z\n'
results.append('backspace across line boundary')
sent, _, _ = exercise(b'x'*200+b'\r')
assert sent == b'x'*160+b'\n'
results.append('160-character input bound')
_, text, _ = exercise(b'ciao\r', answer=b'x'*5000+b'\x04')
assert ui('Testo troncato') in text
results.append('bounded reply and complete frame drain')
sent, text, _ = exercise(b'\x1b', port=False)
assert sent == b'' and ui('Modulo seriale non rilevato') in text
results.append('missing expansion')
_, text, _ = exercise(b'ciao\r', tx_fail=True)
assert ui('Errore trasmissione') in text
results.append('transmit timeout')
_, text, _ = exercise(b'ciao\r', timeout=True)
assert ui('180 secondi') in text
results.append('receive deadline')
_, text, _ = exercise(b'ciao\r', timeout=True, cancel=True)
assert ui('180 secondi') not in text
results.append('cancel pending request')
sent, _, _ = exercise(b'\r\x1b')
assert sent == b''
results.append('empty request not sent')
sent, _, _ = exercise(b'/test\r', base=0x3f8)
assert sent == b'/test\n'
results.append('UART base from BIOS data area, alternative address')
for invalid_base in (0, 0xffff):
    sent, text, _ = exercise(b'\x1b', base=invalid_base)
    assert not sent and ui('Modulo seriale non rilevato') in text
results.append('invalid UART addresses rejected before I/O')
_, text, _ = exercise(b'ciao\r', rx_error=True)
assert ui('Errore ricezione') in text
results.append('UART receive error detected')
report={'type' :'16-bit client emulation, BIOS and UART registers simulated; no modem readiness required','binary_bytes':len(BINARY),'passed':results,'physical_test':False}
# A diagnostic rerun must not overwrite the shipped build verification report.
print(json.dumps(report,indent=2))
