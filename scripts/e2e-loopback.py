#!/usr/bin/env python3
"""Two real mono processes on loopback: pairing, reconnect, file transfer, path
traversal and control-panel auth. Run after `cargo build --release -p mono-cli`."""
import subprocess, os, time, json, re, urllib.request, tempfile, shutil, sys
T=tempfile.mkdtemp(prefix="monoe2e")
BIN=os.environ.get("MONO_BIN", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "target", "release", "mono"))
def start(name):
    env=dict(os.environ, MONO_HOME=f"{T}/{name}")
    p=subprocess.Popen([BIN,"--no-browser"],env=env,stdout=open(f"{T}/{name}.log","w"),stderr=subprocess.STDOUT,text=True)
    time.sleep(1.2); line=open(f"{T}/{name}.log").readline()
    m=re.search(r"http://127.0.0.1:(\d+)/\?k=(\w+)",line); assert m,line
    return p,int(m[1]),m[2]
def call(port,key,path,method="GET",body=None,headers=None,host=None):
    h={"X-Mono-Key":key, **(headers or {})}
    if host: h["Host"]=host
    r=urllib.request.Request(f"http://127.0.0.1:{port}{path}",data=body,method=method,headers=h)
    try:
        with urllib.request.urlopen(r,timeout=20) as x: return x.status, json.loads(x.read() or b"{}")
    except urllib.error.HTTPError as e: return e.code, (e.read().decode()[:200])
ph,hp,hk=start("host"); pg,gp,gk=start("guest")
try:
    # security
    print("no key:",call(hp,"bad","/api/state")[0], "| bad host:",call(hp,hk,"/api/state",host="evil.com")[0])
    c=json.loads(json.dumps(call(hp,hk,"/api/state")[1]["config"]))
    c.update(role="host",bind="127.0.0.1:24810",edge="right",download_dir=f"{T}/host-in")
    print("host save:",call(hp,hk,"/api/config","POST",json.dumps(c).encode()))
    time.sleep(1.5)
    s=call(hp,hk,"/api/state")[1]; code=s["code"]; print("host state:",s["state"],"| code ok:",bool(re.fullmatch(r"[0-9a-f]{64}:[0-9a-f]{32}",code or "")))
    g=dict(c,role="guest",host_addr="127.0.0.1:24810",pairing_code=code,download_dir=f"{T}/guest-in")
    print("bad code:",call(gp,gk,"/api/config","POST",json.dumps(dict(g,pairing_code="abc")).encode())[0])
    print("guest save:",call(gp,gk,"/api/config","POST",json.dumps(g).encode()))
    for _ in range(20):
        time.sleep(0.5)
        a=call(hp,hk,"/api/state")[1]; b=call(gp,gk,"/api/state")[1]
        if a["connected"] and b["connected"]: break
    print("connected:",a["connected"],b["connected"],a["state"],"/",b["state"])
    data=os.urandom(5*1024*1024); 
    print("upload host->guest:",call(hp,hk,"/api/upload","POST",data,{"X-Filename":"r%C3%A7apor%20.bin"}))
    print("upload guest->host:",call(gp,gk,"/api/upload","POST",b"hello",{"X-Filename":"../../evil.txt"}))
    time.sleep(1)
    gin=os.listdir(f"{T}/guest-in"); hin=os.listdir(f"{T}/host-in"); print("guest got:",gin,"host got:",hin)
    print("content equal:",open(f"{T}/guest-in/{gin[0]}","rb").read()==data, "| traversal contained:", not os.path.exists(f"{T}/../evil.txt"))
    # reconnect: restart guest side by re-saving host (drops conn), expect auto reconnect
    call(gp,gk,"/api/config","POST",json.dumps(g).encode()); time.sleep(3)
    print("after re-apply:",call(hp,hk,"/api/state")[1]["connected"],call(gp,gk,"/api/state")[1]["connected"])
    print("host log:",call(hp,hk,"/api/state")[1]["log"][-4:])
finally:
    ph.kill(); pg.kill(); shutil.rmtree(T,ignore_errors=True)
