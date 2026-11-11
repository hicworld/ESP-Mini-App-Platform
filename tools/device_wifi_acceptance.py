"""Verify the real radio scan through the Garden settings entry; leaves Wi-Fi list open."""
import argparse,re,time
from pathlib import Path
import serial

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',default='COM4')
    p.add_argument('--output',type=Path,default=Path('local/wifi-acceptance.log'));a=p.parse_args()
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with serial.Serial(a.port,115200,timeout=.2,write_timeout=3) as c,a.output.open('w',encoding='utf-8') as log:
        c.dtr=True;c.rts=False;time.sleep(.5)
        def command(cmd):
            c.reset_input_buffer();c.write((cmd+'\n').encode());lines=[];end=time.monotonic()+15
            while time.monotonic()<end:
                line=c.readline().decode('utf-8',errors='replace').strip()
                if line:lines.append(line);log.write(line+'\n');log.flush()
                if line.startswith('result:'):
                    assert line=='result: ESP_OK',(cmd,line)
                    return '\n'.join(lines)
            raise TimeoutError(cmd)
        command('stop');time.sleep(.5);assert 'runtime=0' in command('status')
        for attempt in range(2):
            command('run com.example.garden');time.sleep(.5);assert 'runtime=2' in command('status')
            command('touch 400 20');time.sleep(.4);command('touch 90 264');time.sleep(.5)
            assert 'runtime=0' in command('status')
            deadline=time.monotonic()+20
            while True:
                response=command('wifi_list');match=re.search(r'WIFI SCAN busy=(\d+) count=(\d+) result=(\w+)',response);assert match,response
                if match[1]=='0':
                    assert match[3]=='ESP_OK',response
                    print(f'PASS device scan {attempt+1}: {match[2]} networks, no credentials submitted',flush=True);break
                assert time.monotonic()<deadline,'Scan did not finish';time.sleep(1)
        print('PASS Garden opens Wi-Fi list, radio scan completes twice; left on Wi-Fi selection screen',flush=True)
if __name__=='__main__':main()
