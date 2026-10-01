#!/usr/bin/env python3
"""Read-only peripheral/RAM snapshots around deterministic UART input."""
import argparse, hashlib, json, pathlib, re, socket, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser()
ap.add_argument("--output",required=True)
ap.add_argument("--platform",help="Optional diagnostic platform overlay")
ap.add_argument("--isolate-timer",action="store_true")
ap.add_argument("--fixed-dma",action="store_true")
ap.add_argument("--verify",action="store_true")
ap.add_argument("--read-enable",action="store_true")
args=ap.parse_args()
if args.fixed_dma and not args.isolate_timer:
    ap.error("--fixed-dma requires --isolate-timer")
out=ROOT/args.output
out.mkdir(parents=True,exist_ok=False)
symbols={}
for line in subprocess.check_output(["arm-none-eabi-nm","-n","build/SensorTelemetry.elf"],cwd=ROOT,text=True).splitlines():
    fields=line.split()
    if len(fields)==3: symbols[fields[2]]=int(fields[0],16)
script=(ROOT/"renode/hood.resc").read_text()
if args.isolate_timer:
    platform=pathlib.Path("/home/hello/tools/renode/platforms/cpus/stm32f103.repl").read_text()
    if args.fixed_dma:
        platform=platform.replace("dma1: DMA.STM32G0DMA","dma1: DMA.HoodSTM32DMA")
        script="include @renode/HoodSTM32DMA.cs\n"+script
    original="UpdateInterrupt -> nvic@25 | dma1@5"
    assert platform.count(original)==1
    local=out/"isolated-platform.repl"
    local.write_text(platform.replace(original,"UpdateInterrupt -> nvic@25"))
    script=script.replace("@platforms/cpus/stm32f103.repl","@"+str(local))
if args.platform:
    script=script.replace("nvic Frequency",f"machine LoadPlatformDescription @{args.platform}\nnvic Frequency")
(out/"probe.resc").write_text(script)
(out/"manifest.json").write_text(json.dumps({
    "elf_sha256":hashlib.sha256((ROOT/"build/SensorTelemetry.elf").read_bytes()).hexdigest(),
    "probe_sha256":hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
    "model_sha256":hashlib.sha256((ROOT/"renode/HoodSTM32DMA.cs").read_bytes()).hexdigest()
        if args.fixed_dma else None,"arguments":vars(args)},indent=2))
log=(out/"console.log").open("w")
proc=subprocess.Popen(["/home/hello/tools/renode/renode","--disable-gui","--plain","--port","12345",str(out/"probe.resc")],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
def connect(port):
    for _ in range(100):
        try:return socket.create_connection(("127.0.0.1",port),timeout=2)
        except OSError:time.sleep(.1)
    raise TimeoutError(port)
mon=None;uart=None
try:
    mon=connect(12345);mon.settimeout(15)
    transcript=(out/"monitor.log").open("w")
    def prompt():
        data=b""
        while b"(smart-hood)" not in data:
            part=mon.recv(65536)
            if not part:raise EOFError()
            data+=part
        text=data.decode(errors="replace");transcript.write(text);transcript.flush()
        if "There was an error" in text:raise RuntimeError(text)
        return text
    prompt();uart=connect(12346)
    def command(text):
        mon.sendall((text+"\n").encode());return prompt()
    def word(address):
        text=command(f"sysbus ReadDoubleWord 0x{address:x}")
        values=re.findall(r"0x[0-9A-Fa-f]{8}\b",text)
        return int(values[-1],16)
    sent=bytearray()
    def snapshot(label):
        state={"label":label,"registers":{}}
        if args.read_enable:
            state["registers"]["CCR5"]=word(0x40020058)
        for name,address in {"NDTR5":0x4002005c,"CPAR5":0x40020060,
             "CMAR5":0x40020064,"DMA_ISR":0x40020000,"USART_SR":0x40013800,
             "USART_CR1":0x4001380c,"USART_CR3":0x40013814}.items():
            state["registers"][name]=word(address)
        for name in ("app_dma_events","app_dma_bytes","dma_cursor"):
            state[name]=word(symbols[name])
        memory=b"".join(word(symbols["dma_rx"]+n).to_bytes(4,"little") for n in range(0,256,4))
        state["dma_hex"]=memory.hex()
        (out/(label+".json")).write_text(json.dumps(state,indent=2))
        print(json.dumps(state),flush=True)
        if args.verify:
            if args.read_enable:assert state["registers"]["CCR5"]&1, label+": EN is clear"
            expected=bytearray(256)
            for n,b in enumerate(sent):expected[n%256]=b
            assert memory==expected, label+": DMA memory differs"
            assert state["registers"]["NDTR5"]==256-len(sent)%256, label+": NDTR differs"
            assert state["app_dma_bytes"]==len(sent), label+": delivered count differs"
    command('emulation RunFor "0.05"');snapshot("boot-no-input")
    command('emulation RunFor "0.2"');snapshot("wait-no-input")
    for index,data in enumerate((b"ABCDEFGH",b"ijklmnop",b"S,10,20,30,40,50\n",
                                bytes(range(33,128)),bytes(range(128,192)),
                                bytes(range(192,256)),b"WRAP1234")):
        uart.sendall(data);sent.extend(data);time.sleep(.03)
        command('emulation RunFor "0.05"');snapshot("chunk-"+str(index))
    command('emulation RunFor "0.2"');snapshot("final-idle")
    (out/"summary.json").write_text(json.dumps({"verified":args.verify,"sent_bytes":len(sent),
        "isolate_timer":args.isolate_timer,"fixed_dma":args.fixed_dma,"firmware_changed":False},indent=2))
    transcript.close()
finally:
    if mon:
        try:mon.sendall(b"quit\n")
        except OSError:pass
        mon.close()
    if uart:uart.close()
    try:proc.wait(timeout=5)
    except subprocess.TimeoutExpired:proc.terminate();proc.wait(timeout=5)
    log.close()
