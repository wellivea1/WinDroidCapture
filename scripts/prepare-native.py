"""Prepare official upstream headers and MSVC import libs; no media is captured."""
from pathlib import Path
import re, subprocess, json, hashlib
root=Path(__file__).resolve().parent.parent
out=root/'build-native'/'deps'
out.mkdir(parents=True,exist_ok=True)
def write_if_changed(path,text):
    if not path.exists() or path.read_text(encoding='utf-8') != text:
        path.write_text(text,encoding='utf-8',newline='\n')
ff=root/'tools'/'ffmpeg-9.0.2'
sd=root/'tools'/'SDL3-3.4.18'
sc=root/'tools'/'scrcpy-5.0.1'
for p in (ff,sd,sc):
    if not p.is_dir(): raise SystemExit(f'Missing official source/development package: {p}')
write_if_changed(ff/'libavutil'/'avconfig.h','#ifndef AVUTIL_AVCONFIG_H\n#define AVUTIL_AVCONFIG_H\n#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 1\n#endif\n')
for name in ('avcodec-63','avformat-63','avutil-61','swresample-7'):
    dll=root/'tools'/'scrcpy-win64-v5.0.1'/f'{name}.dll'
    exports=subprocess.check_output(['dumpbin','/nologo','/exports',str(dll)],text=True)
    symbols=re.findall(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\w+)',exports,re.M)
    if not symbols: raise SystemExit(f'No DLL exports: {dll}')
    deffile=out/f'{name}.def'
    write_if_changed(deffile,f'LIBRARY "{name}.dll"\nEXPORTS\n'+'\n'.join(symbols)+'\n')
    importlib=out/f'{name}.lib'
    if not importlib.exists() or importlib.stat().st_mtime < deffile.stat().st_mtime:
        subprocess.run(['lib','/nologo','/machine:x64',f'/def:{deffile}',f'/out:{importlib}'],check=True,stdout=subprocess.DEVNULL)
config='''#define SCRCPY_VERSION "5.0.1"
#define PREFIX "."
#define PORTABLE 1
#define DEFAULT_LOCAL_PORT_RANGE_FIRST 27183
#define DEFAULT_LOCAL_PORT_RANGE_LAST 27199
#define HAVE_STRDUP 1
'''
write_if_changed(out/'config.h',config)
meson=(sc/'app'/'meson.build').read_text()
sources=re.findall(r"'(src/[^']+\.c)'",meson.split('feature_test_macros')[0])
sources+=['src/util/command.c','src/sys/win/file.c','src/sys/win/process.c']
sources=[str(sc/'app'/s).replace('\\','/') for s in sources if s not in ('src/recorder.c','src/server.c')]
# Derived client source only: each managed Android server owns a session-named
# remote JAR, preventing a probe/cleanup from replacing stock scrcpy's file.
server_source=(sc/'app/src/server.c').read_text(encoding='utf-8')
server_changes={
    '#define SC_DEVICE_SERVER_PATH "/data/local/tmp/scrcpy-server.jar"':'#define SC_DEVICE_SERVER_PATH "/data/local/tmp/awm-scrcpy-%08x.jar"',
    'push_server(struct sc_intr *intr, const char *serial) {':'push_server(struct sc_intr *intr, const char *serial, uint32_t scid) {',
    '    bool ok = sc_adb_push(intr, serial, server_path, SC_DEVICE_SERVER_PATH, 0);':'    char remote[100];\n    snprintf(remote, sizeof(remote), SC_DEVICE_SERVER_PATH, (unsigned) scid);\n    bool ok = sc_adb_push(intr, serial, server_path, remote, 0);',
    '    cmd[count++] = "CLASSPATH=" SC_DEVICE_SERVER_PATH;':'    char classpath[128];\n    snprintf(classpath, sizeof(classpath), "CLASSPATH=" SC_DEVICE_SERVER_PATH, (unsigned) params->scid);\n    cmd[count++] = classpath;',
    '    ok = push_server(&server->intr, serial);':'    ok = push_server(&server->intr, serial, params->scid);',
}
for old,new in server_changes.items():
    if server_source.count(old)!=1: raise SystemExit('Pinned upstream server-path patch anchor changed')
    server_source=server_source.replace(old,new,1)
write_if_changed(out/'server-awm.c',server_source)
sources.append(str(out/'server-awm.c').replace('\\','/'))
write_if_changed(out/'scrcpy-sources.cmake','set(AWM_SCRCPY_SOURCES\n'+'\n'.join(f'  "{s}"' for s in sources)+'\n)\n')
manifest={'upstream_scrcpy':'5.0.1','ffmpeg':'9.0.2','SDL':'3.4.18','export_protocol':3,
          'patch':'native/scrcpy/recorder.c + Windows C compatibility option parser + session-owned Android server path',
          'runtime_hashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (root/'tools'/'scrcpy-win64-v5.0.1').glob('*.dll')}}
camera_server=root/'build-native'/'awm-camera-server.json'
if camera_server.exists(): manifest['awm_camera_server']=json.loads(camera_server.read_text(encoding='utf-8-sig'))
write_if_changed(out/'native-dependencies.json',json.dumps(manifest,indent=2)+'\n')
print('Prepared matching FFmpeg 9.0.2 headers/imports and upstream scrcpy client source list.')
