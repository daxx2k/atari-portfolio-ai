from pathlib import Path
import sys,json,struct
R=Path(__file__).resolve().parents[1];O=R;P=R/'tests/client_uart.py'
sys.argv=[str(P),str(R/'portfolio/AI.COM')]
s=P.read_text().split('\nresults=[]')[0]
s=s.replace('rename_fail=False):', 'rename_fail=False, corrupt_packet=False):')
s=s.replace('received = deque()', 'received = deque(); radio_left=[0];state=[1,40];writes=[]')
s=s.replace('if ah == 1:\n', 'if ah == 2:\n                u.reg_write(UC_X86_REG_AX,0);return\n            if ah == 1:\n')
s=s.replace("if c == 4: stage_change('display')", "if radio_left[0]:\n                radio_left[0]-=1\n                if not radio_left[0]:stage_change('display')\n            elif c == 4: stage_change('display')")
s=s.replace("binary=last in",'''if last.startswith(b'@SETUP END '):received.extend(b'\\x02AISETUP.TXT applied.\\x04');return
                    if last in (b'/rfast',b'/rnfast',b'/rbfast',b'/rufast',b'/rdfast',b'/rpfast'):
                        if last==b'/rnfast':state[0]=state[0]%5+1
                        if last==b'/rbfast':state[0]=(state[0]-2)%5+1
                        if last==b'/rufast':state[1]=min(100,state[1]+5)
                        if last==b'/rdfast':state[1]=max(0,state[1]-5)
                        raw=bytes([2,state[0]])+struct.pack('<H',state[1]);crc=0xffff
                        for byte in raw:
                            crc^=byte
                            for _ in range(8):crc=(crc>>1)^((crc&1)*0xa001)
                        received.extend(b'PRD1'+raw+struct.pack('<H',crc^(1 if corrupt_packet else 0)));radio_left[0]=10;return
                    binary=last in''')
s=s.replace("elif stage == 'display' and inputs: c = 32;stage='input'", "elif stage == 'display' and inputs:\n                    if '/radio off: stop' in ''.join(written[-400:]):c=inputs.popleft()\n                    else:c=32;stage='input'")
s=s.replace("if not write_fail:files[", "writes.append(bytes(sent))\n                if not write_fail:files[")
s=s.replace("return bytes(sent), ''.join(written), screens", "return bytes(sent), ''.join(written), screens, writes")
n={'__file__':str(P),'struct':struct};exec(compile(s,'fast_radio_test','exec'),n);exercise=n['exercise']
fixture=b'timezone=Europe/London\r\nvoice=on\r\nrobot=on\r\nvolume=40\r\nradio_station=1\r\n'
checks=[]
files={b'A:\\AISETUP.TXT':fixture}
sent,text,screens,writes=exercise(b'/radio\r'+bytes([128,129,130,130,131,27])+b'/radio off\r',file_state=files)
assert b'/rbfast\n/rnfast\n/rufast\n/rufast\n/rdfast\n' in sent,sent
assert b'volume=45\r\n' in files[b'A:\\AISETUP.TXT'],files
assert len(writes)==1,writes
assert all(b'/rdfast\n' in x for x in writes),'Saved before key burst completed'
assert 'NIGHTWAVE PLAZA' in text and '45/100' in text,text[-800:]
checks.append('rapid keys use compact packets, wrap station1 to5 and back, save volume45 once on exit')
files={b'A:\\AISETUP.TXT':fixture}
sent,text,_,_=exercise(b'/radio\r'+bytes([129,129,129,129,27]),file_state=files)
assert b'radio_station=5\r\n' in files[b'A:\\AISETUP.TXT']
assert 'DATAWAVE FM' in text and 'NIGHTWAVE PLAZA' in text
checks.append('all five station labels and final station persist')
files={b'A:\\AISETUP.TXT':fixture}
_,text,_,writes=exercise(b'/radio\r'+bytes([130])*14+bytes([27]),file_state=files)
assert b'volume=100\r\n' in files[b'A:\\AISETUP.TXT'] and '100/100' in text
checks.append('volume100 renders and persists without overflow')
files={b'A:\\AISETUP.TXT':fixture}
_,text,_,writes=exercise(b'/radio\r',file_state=files,corrupt_packet=True)
assert 'Serial receive error' in text and not writes
checks.append('corrupt compact status rejected without overwriting card options')
files={b'A:\\AISETUP.TXT':fixture}
_,text,_,_=exercise(b'/config\rot5\rs/test\r',file_state=files)
assert b'radio_station=5\r\n' in files[b'A:\\AISETUP.TXT']
checks.append('card config editor accepts station5')
print(json.dumps({'test':'compact radio client emulation','passed':checks,'physical_timing_verified':False},indent=2))
