"""Verify real codec writes, mute, effect-only output, and lifecycle stop. Listening is manual."""
import argparse,re,time
from pathlib import Path
import serial

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',default='COM4');p.add_argument('--output',type=Path,default=Path('local/audio-acceptance.log'));a=p.parse_args()
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with serial.Serial(a.port,115200,timeout=.2,write_timeout=3) as c,a.output.open('w',encoding='utf-8') as log:
        c.dtr=True;c.rts=False;time.sleep(.5)
        def command(cmd):
            c.reset_input_buffer();c.write((cmd+'\n').encode());lines=[];deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                line=c.readline().decode('utf-8',errors='replace').strip()
                if line:lines.append(line);log.write(line+'\n');log.flush()
                if line.startswith('result:'):
                    assert line=='result: ESP_OK',(cmd,line);return '\n'.join(lines)
            raise TimeoutError(cmd)
        def audio():
            text=command('audio_status');print(text,flush=True)
            values=dict((k,int(v)) for k,v in re.findall(r'(worker|codec|music|volume|blocks)=(-?\d+)',text));assert values['codec']==0,text;return values
        def touch(x,y):command(f'touch {x} {y}');time.sleep(.4)
        command('stop');time.sleep(.5);command('run com.example.garden');time.sleep(3)
        first=audio();assert first['worker']==1 and first['music'] in (1,2) and first['volume']==55
        time.sleep(1);assert audio()['blocks']>first['blocks']
        touch(400,20);touch(300,314);touch(300,314);assert audio()['volume']==0
        before=audio()['blocks'];time.sleep(.7);assert audio()['blocks']==before,'Mute still writes audio'
        touch(300,314);touch(300,314);touch(100,314);assert audio()['music']==0
        touch(40,150);before=audio()['blocks'];touch(60,375);time.sleep(.3);assert audio()['blocks']>before,'Effect-only output missing'
        for cmd in ['input b','touch 220 375','input motion','input a']:
            command(cmd);time.sleep(.6);assert 'runtime=2' in command('status')
        command('stop');time.sleep(.5);before=audio();assert before['music']==0
        time.sleep(.7);assert audio()['blocks']==before['blocks'],'Stop did not silence app'
        command('run com.example.garden');time.sleep(.6);assert audio()['music'] in (1,2)
        print('PASS real codec writes, volume mute, effect-only playback, input hooks, stop and restart. Human listening still required.',flush=True)
if __name__=='__main__':main()
