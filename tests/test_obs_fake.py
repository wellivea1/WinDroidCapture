"""Loopback OBS protocol FAKES. These tests never connect to the user's OBS."""
import base64
import hashlib
import json
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest
from pathlib import Path

EXE = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path('build/awm-tests.exe').resolve()
sys.argv = [sys.argv[0]]
REQUESTS = ['GetVersion','GetVideoSettings','GetCurrentProgramScene','SetCurrentProgramScene',
 'GetStreamStatus','GetRecordStatus','GetReplayBufferStatus','GetVirtualCamStatus',
 'StartVirtualCam','StopVirtualCam','GetInputKindList','GetInputDefaultSettings',
 'CreateScene','RemoveScene','GetSceneItemList','GetSceneItemId','GetSceneItemTransform',
 'SetSceneItemTransform','SetSceneItemEnabled','CreateInput','RemoveInput','GetInputSettings',
 'SetInputSettings','GetInputPropertiesListPropertyItems']

def exact(conn, size):
    data=b''
    while len(data)<size:
        block=conn.recv(size-len(data))
        if not block: raise EOFError()
        data+=block
    return data

def frame(conn, obj, opcode=1, final=True):
    data=json.dumps(obj).encode() if not isinstance(obj,bytes) else obj
    size=len(data)
    conn.sendall(bytes([(0x80 if final else 0)|opcode,size]) + data if size<126
                 else bytes([(0x80 if final else 0)|opcode,126])+struct.pack('!H',size)+data)

def receive(conn):
    head=exact(conn,2);n=head[1]&127
    if n==126:n=struct.unpack('!H',exact(conn,2))[0]
    if n==127:n=struct.unpack('!Q',exact(conn,8))[0]
    if not head[1]&128:raise AssertionError('Client frame was not masked')
    mask=exact(conn,4);data=exact(conn,n)
    payload=bytes(v^mask[i%4] for i,v in enumerate(data))
    return head[0]&15,payload

class FakeObs:
    def __init__(self, mode='success'):
        self.mode=mode;self.calls=[];self.failure=None;self.current='FAKE original scene';self.virtual=False;self.settings={};self.scene='';self.enabled=False
        self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.port=self.listener.getsockname()[1];self.listener.listen(1)
        self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
    def run(self):
        try:
            conn,_=self.listener.accept()
            with conn:
                conn.settimeout(8);headers=b''
                while not headers.endswith(b'\r\n\r\n'):headers+=exact(conn,1)
                values=dict(line.split(': ',1) for line in headers.decode().split('\r\n')[1:] if ': ' in line)
                accept=base64.b64encode(hashlib.sha1((values['Sec-WebSocket-Key']+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
                if self.mode=='bad_upgrade':accept='invalid'
                conn.sendall(('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+'\r\n\r\n').encode())
                if self.mode=='bad_upgrade':return
                salt='ZmFrZS1zYWx0';challenge='ZmFrZS1jaGFsbGVuZ2U='
                hello={'op':0,'d':{'rpcVersion':1,'obsStudioVersion':'FAKE 32.2.2','obsWebSocketVersion':'FAKE 5.7.4','authentication':{'salt':salt,'challenge':challenge}}}
                if self.mode=='silent':time.sleep(4);return
                if self.mode=='invalid_json':frame(conn,b'{broken');return
                frame(conn,hello)
                op,payload=receive(conn);identified=json.loads(payload)
                secret=base64.b64encode(hashlib.sha256(('fake-password'+salt).encode()).digest()).decode()
                expected=base64.b64encode(hashlib.sha256((secret+challenge).encode()).digest()).decode()
                if identified['d'].get('authentication')!=expected or self.mode=='auth_rejected':frame(conn,struct.pack('!H',4009),opcode=8);return
                if self.mode=='bad_rpc':frame(conn,{'op':2,'d':{'negotiatedRpcVersion':99}});return
                frame(conn,{'op':2,'d':{'negotiatedRpcVersion':1}})
                while True:
                    op,payload=receive(conn)
                    if op==10:continue
                    request=json.loads(payload)['d'];kind=request['requestType'];data=request.get('requestData',{});self.calls.append((kind,data))
                    response={};code=100;result=True
                    if kind=='GetVersion':response={'availableRequests':[r for r in REQUESTS if not(self.mode=='unavailable_request' and r=='GetVideoSettings')]}
                    elif kind=='GetVideoSettings':response={'baseWidth':1920,'baseHeight':1080,'outputWidth':1280,'outputHeight':720,'fpsNumerator':30,'fpsDenominator':1}
                    elif kind in ['GetStreamStatus','GetRecordStatus']:response={'outputActive':self.mode=='busy_obs'}
                    elif kind=='GetReplayBufferStatus':result=False;code=604
                    elif kind=='GetVirtualCamStatus':response={'outputActive':self.virtual}
                    elif kind=='GetCurrentProgramScene':response={'currentProgramSceneName':self.current}
                    elif kind=='SetCurrentProgramScene':self.current=data['sceneName']
                    elif kind=='CreateScene':self.scene=data['sceneName']
                    elif kind=='GetInputKindList':response={'inputKinds':['window_capture']}
                    elif kind=='GetInputDefaultSettings':response={'defaultInputSettings':{'client_area':True,'cursor':True}}
                    elif kind=='CreateInput':self.settings=data['inputSettings'];response={'sceneItemId':7}
                    elif kind=='GetInputPropertiesListPropertyItems':
                        prop=data['propertyName']
                        value=2 if prop=='method' else 0 if prop=='priority' else 'FAKE-owned-window:SDL_app:fake-scrcpy.exe'
                        response={'propertyItems':[{'itemName':'FAKE enumerated option','itemValue':value,'itemEnabled':not(self.mode=='no_wgc' and prop=='method')}]}
                    elif kind=='SetInputSettings':self.settings.update(data['inputSettings'])
                    elif kind=='GetInputSettings':response={'inputSettings':self.settings}
                    elif kind=='StartVirtualCam':self.virtual=True
                    elif kind=='StopVirtualCam':self.virtual=False
                    elif kind=='SetSceneItemEnabled':self.enabled=data['sceneItemEnabled']
                    elif kind=='GetSceneItemTransform':response={'sceneItemTransform':{'sourceWidth':1920,'sourceHeight':1080}}
                    if self.mode=='record_during_output' and self.virtual and kind=='GetRecordStatus':response={'outputActive':True}
                    if self.mode=='user_changed_scene' and self.virtual and kind=='GetCurrentProgramScene':
                        self.current='FAKE new user scene';response={'currentProgramSceneName':self.current}
                    if self.mode=='cleanup_scene_failure' and self.virtual and not self.enabled and kind=='GetCurrentProgramScene':result=False;code=600
                    if self.mode=='unsafe_client_area' and kind=='GetInputSettings':response={'inputSettings':dict(self.settings,client_area=False)}
                    if self.mode=='request_failure' and kind=='GetVideoSettings':result=False;code=600
                    msg={'op':7,'d':{'requestType':kind,'requestId':request['requestId'],'requestStatus':{'result':result,'code':code,'comment':'FAKE status'},'responseData':response}}
                    if self.mode=='fragmented_ping':
                        frame(conn,b'ping',opcode=9)
                        encoded=json.dumps(msg).encode();middle=len(encoded)//2;frame(conn,encoded[:middle],final=False);frame(conn,encoded[middle:],opcode=0)
                    else:frame(conn,msg)
        except (EOFError,ConnectionError,OSError):pass
        except Exception as exc:self.failure=exc
        finally:self.listener.close()
    def invoke(self, output=False):
        args=[str(EXE),'--obs-fake-client',str(self.port),'fake-password']+(['output'] if output else [])
        result=subprocess.run(args,capture_output=True,text=True,encoding='utf-8',timeout=10)
        self.thread.join(5)
        if self.failure:raise self.failure
        return result

class ObsTests(unittest.TestCase):
    def test_auth_and_response(self):
        fake=FakeObs();r=fake.invoke();self.assertEqual(r.returncode,0,r.stdout);self.assertEqual(json.loads(r.stdout)['outputWidth'],1280)
    def test_fragmentation_and_ping(self):
        fake=FakeObs('fragmented_ping');r=fake.invoke();self.assertEqual(r.returncode,0,r.stdout)
    def test_auth_rejection(self):
        r=FakeObs('auth_rejected').invoke();self.assertNotEqual(r.returncode,0);self.assertIn('authentication',r.stdout.lower())
    def test_incompatible_rpc(self):
        r=FakeObs('bad_rpc').invoke();self.assertNotEqual(r.returncode,0);self.assertIn('unsupported RPC',r.stdout)
    def test_unavailable_request(self):
        fake=FakeObs('unavailable_request');r=fake.invoke();self.assertNotEqual(r.returncode,0);self.assertNotIn('GetVideoSettings',[n for n,_ in fake.calls])
    def test_request_failure(self):self.assertNotEqual(FakeObs('request_failure').invoke().returncode,0)
    def test_invalid_upgrade(self):self.assertNotEqual(FakeObs('bad_upgrade').invoke().returncode,0)
    def test_invalid_json(self):self.assertNotEqual(FakeObs('invalid_json').invoke().returncode,0)
    def test_silent_timeout(self):
        start=time.monotonic();r=FakeObs('silent').invoke();self.assertNotEqual(r.returncode,0);self.assertLess(time.monotonic()-start,6)
    def test_owned_lifecycle(self):
        fake=FakeObs();r=fake.invoke(True);self.assertEqual(r.returncode,0,r.stdout);self.assertFalse(fake.virtual);self.assertFalse(fake.enabled);self.assertEqual(fake.current,'FAKE original scene');names=[n for n,_ in fake.calls];self.assertNotIn('SetVideoSettings',names);self.assertNotIn('SetInputMute',names);self.assertEqual(len([n for n in names if n=='StartVirtualCam']),1)
    def test_existing_output_never_taken_over(self):
        fake=FakeObs('busy_obs');r=fake.invoke(True);self.assertNotEqual(r.returncode,0);self.assertNotIn('CreateScene',[n for n,_ in fake.calls]);self.assertNotIn('StopVirtualCam',[n for n,_ in fake.calls])
    def test_wgc_required(self):
        fake=FakeObs('no_wgc');r=fake.invoke(True);self.assertNotEqual(r.returncode,0);self.assertNotIn('StartVirtualCam',[n for n,_ in fake.calls])
    def test_new_recording_preserves_user_global_state(self):
        fake=FakeObs('record_during_output');r=fake.invoke(True);self.assertEqual(r.returncode,0,r.stdout);self.assertFalse(fake.enabled);self.assertTrue(fake.virtual);self.assertEqual(fake.current,fake.scene);self.assertNotIn('StopVirtualCam',[n for n,_ in fake.calls])
    def test_user_scene_change_is_not_overwritten(self):
        fake=FakeObs('user_changed_scene');r=fake.invoke(True);self.assertNotEqual(r.returncode,0);self.assertFalse(fake.enabled);self.assertTrue(fake.virtual);self.assertEqual(fake.current,'FAKE new user scene');self.assertNotIn('StopVirtualCam',[n for n,_ in fake.calls])
    def test_cleanup_query_failure_is_not_success(self):
        fake=FakeObs('cleanup_scene_failure');r=fake.invoke(True);self.assertNotEqual(r.returncode,0);self.assertFalse(fake.enabled);self.assertTrue(fake.virtual);self.assertNotIn('StopVirtualCam',[n for n,_ in fake.calls])
    def test_safe_capture_settings_are_checked(self):
        fake=FakeObs('unsafe_client_area');r=fake.invoke(True);self.assertNotEqual(r.returncode,0);self.assertNotIn('StartVirtualCam',[n for n,_ in fake.calls])

if __name__=='__main__':
    print('SIMULATED OBS TESTS — LOOPBACK FAKES, NO PHYSICAL CAMERA/MICROPHONE EVIDENCE',flush=True)
    unittest.main(verbosity=2)
