"""Exercise an installed Pocket Garden and its registry across a device restart."""
import argparse
import re
import time
from pathlib import Path
import serial

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',default='COM4');p.add_argument('--output',type=Path,default=Path('local/garden-acceptance.log'))
    args=p.parse_args();args.output.parent.mkdir(exist_ok=True,parents=True)
    with args.output.open('w',encoding='utf-8') as log:
        def emit(s):
            print(s,flush=True);log.write(s+'\n');log.flush()
        def connect():
            deadline=time.monotonic()+25
            while True:
                try:
                    c=serial.Serial(args.port,115200,timeout=.2,write_timeout=3);c.dtr=True;c.rts=False;time.sleep(.5);return c
                except serial.SerialException:
                    if time.monotonic()>deadline:raise
                    time.sleep(.5)
        def command(c,cmd,timeout=15):
            c.reset_input_buffer();c.write((cmd+'\n').encode());lines=[];deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                line=c.readline().decode('utf-8',errors='replace').strip()
                if line:lines.append(line);emit(line)
                if line.startswith('result:'):
                    if line!='result: ESP_OK':raise RuntimeError(cmd+': '+line)
                    return '\n'.join(lines)
            raise TimeoutError(cmd)
        def state(c,expected):
            time.sleep(.4);s=command(c,'status');assert f'runtime={expected}' in s,s;return s
        c=connect()
        try:
            command(c,'stop');state(c,0)
            assert 'com.example.garden' in command(c,'list')
            command(c,'apps_check',30)
            command(c,'run com.example.garden');state(c,2)
            for cmd in ['input motion','input a','input b','touch 200 260','touch 230 375','touch 160 320','touch 400 20','touch 200 150']:
                command(c,cmd);state(c,2)
            time.sleep(6);state(c,2)
            command(c,'touch 400 20');state(c,2)
            command(c,'touch 90 264');state(c,0)
            command(c,'run com.example.garden');state(c,2)
            command(c,'inputs');command(c,'environment')
            command(c,'stop');state(c,0)
            tests=command(c,'selftest',50)
            match=re.search(r'SELFTEST RESULT (\d+)/(\d+)',tests)
            assert match and match[1]==match[2],tests
            c.write(b'reboot\n');time.sleep(.8)
        finally:c.close()
        time.sleep(3);c=connect()
        try:
            state(c,0)
            assert 'com.example.garden' in command(c,'list')
            command(c,'apps_check',30)
            command(c,'run com.example.garden');state(c,2)
            command(c,'inputs');command(c,'environment')
        finally:c.close()
        emit('PASS Garden device: signed registry, WAMR timer/input/touch, stop to Home, selftest, reboot persistence, relaunch')

if __name__=='__main__':main()
