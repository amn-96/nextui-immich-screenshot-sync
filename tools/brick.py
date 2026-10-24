#!/usr/bin/env python3
"""Drive the Brick over ssh for layout checks.

  tools/brick.py shot [name]       framebuffer -> build/shots/<name>.png
  tools/brick.py press KEY...      tap buttons (a b x y l1 r1 l2 r2 start select up down left right)
  tools/brick.py launch | quit     start / kill the pak
"""
import os, struct, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
env = dict(os.environ)
for line in open(os.path.join(ROOT, "local/brick.env")):
    key, _, value = line.strip().partition("=")
    if key: env[key] = value.strip("\"'")
env["SSHPASS"] = env["BRICK_PWD"]

def ssh(command, stdin=None):
    return subprocess.run(["sshpass", "-e", "ssh", "-q", f"{env['BRICK_USER']}@{env['BRICK_IP']}", command],
        input=stdin, env=env, capture_output=True, check=True).stdout

EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
# TRIMUI Player1 (event3) codes; SDL maps them to NextUI's JOY_* indices.
KEYS = {"b": 304, "a": 305, "y": 307, "x": 308, "l1": 310, "r1": 311, "select": 314, "start": 315}
HATS = {"left": (16, -1), "right": (16, 1), "up": (17, -1), "down": (17, 1)}
TRIGGERS = {"l2": 2, "r2": 5}

def event(kind, code, value):
    return struct.pack("<qqHHi", 0, 0, kind, code, value) + struct.pack("<qqHHi", 0, 0, EV_SYN, 0, 0)

def tap(name):
    if name in KEYS: down, up = event(EV_KEY, KEYS[name], 1), event(EV_KEY, KEYS[name], 0)
    elif name in HATS: down, up = event(EV_ABS, HATS[name][0], HATS[name][1]), event(EV_ABS, HATS[name][0], 0)
    else: down, up = event(EV_ABS, TRIGGERS[name], 255), event(EV_ABS, TRIGGERS[name], 0)
    return down, up

def press(names):
    script = ""
    for name in names:
        down, up = tap(name)
        script += f"printf '{''.join(f'\\\\x{b:02x}' for b in down)}' > /dev/input/event3; usleep 80000; " \
                  f"printf '{''.join(f'\\\\x{b:02x}' for b in up)}' > /dev/input/event3; usleep 250000; "
    ssh(script)

def shot(name):
    from PIL import Image
    raw = ssh("dd if=/dev/fb0 bs=4096 count=768 2>/dev/null")
    os.makedirs(os.path.join(ROOT, "build/shots"), exist_ok=True)
    path = os.path.join(ROOT, "build/shots", name + ".png")
    Image.frombytes("RGBA", (1024, 768), raw, "raw", "BGRA").convert("RGB").save(path)
    print(path)

if __name__ == "__main__":
    command, args = sys.argv[1], sys.argv[2:]
    if command == "shot": shot(args[0] if args else "shot")
    elif command == "press": press(args)
    elif command == "quit": ssh("killall immich.elf; true")
    elif command == "launch":
        ssh("cd /mnt/SDCARD/Tools/tg5040/Immich.pak && (LOGS_PATH=/mnt/SDCARD/.userdata/tg5040/logs "
            "SHARED_USERDATA_PATH=/mnt/SDCARD/.userdata/shared nohup ./launch.sh >/dev/null 2>&1 &)")
        time.sleep(2)
