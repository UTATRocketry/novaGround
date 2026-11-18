#!/usr/bin/env python3
"""Helpers for nova-pi.sh, kept in their own file so they can be read, linted
and tested like any other Python.

    nova-pi-tools.py snapshot json|human|report
        One snapshot of this Pi. json and human print it; report POSTs it to
        the Nova Console (/api/pi-report). Paths come from the environment
        (PROD_DIR, DEV_DIR, ENV_DIR, PI_ENV, STATE_ROOT, DEPLOYED_FILE,
        LEGACY_UNIT, REPORT_VERSION), which nova-pi.sh sets, so tests can point
        them at a scratch directory.
    nova-pi-tools.py metrics
        Read-only system metrics for this Pi as JSON: per-core CPU counters,
        load, memory, temperature, vcgencmd get_throttled, the running
        instance's PID/CPU/RSS, /proc/diskstats and clock sync. It only reads
        /proc, /sys and runs read-only commands; it never writes or uses sudo.
        PROC_ROOT and SYS_ROOT (default /proc, /sys) let tests use fixtures.
    nova-pi-tools.py release <console> <tag|stable>
        Ask the Nova Console which novaGround commit a release pins. Prints
        "<tag> <commit>".
    nova-pi-tools.py console-url <value>
        A NOVA_CONSOLE value as a base URL: "host" and "host:port" become
        http://host:8080 and http://host:port, like NOVA_BROKER and
        NOVA_BACKEND; a full URL is kept as it is.

Standard library only: this runs on a stock Pi.
"""
import json
import os
import pwd
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

# The prod Nova Console's port (ConsolePort in ops/nova.settings.example.ps1).
DEFAULT_CONSOLE_PORT = 8080


def console_url(value):
    """NOVA_CONSOLE as a base URL with no trailing slash, or None if unset."""
    value = (value or "").strip()
    if not value:
        return None
    if "://" not in value:
        if ":" not in value:
            value += ":%d" % DEFAULT_CONSOLE_PORT
        value = "http://" + value
    return value.rstrip("/")


def run(cmd, timeout=5):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout).stdout.strip()
    except Exception:
        return ""


def read_env(path):
    if not os.path.isfile(path):
        return None
    out = {}
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            out[key.strip()] = value.strip().strip('"').strip("'")
    return out


def read_json(path):
    try:
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    except Exception:
        return None


def as_owner(cmd, path):
    if os.geteuid() == 0:
        try:
            owner = pwd.getpwuid(os.stat(path).st_uid).pw_name
        except Exception:
            owner = None
        if owner and owner != "root":
            return ["sudo", "-u", owner] + cmd
    return cmd


def git(d, *args):
    return run(["git", "-c", "safe.directory=" + d, "-C", d] + list(args))


def health(backend):
    """Does this environment's backend answer, seen from the Pi?"""
    if not backend:
        return None
    base = backend if "://" in backend else "http://" + backend
    try:
        with urllib.request.urlopen(base.rstrip("/") + "/health", timeout=2) as resp:
            return {"ok": 200 <= resp.status < 400, "detail": "HTTP %d" % resp.status}
    except urllib.error.HTTPError as exc:
        return {"ok": False, "detail": "HTTP %d" % exc.code}
    except Exception as exc:
        reason = getattr(exc, "reason", exc)
        return {"ok": False, "detail": reason if isinstance(reason, str) else type(reason).__name__}


E = os.environ


def snapshot(mode):
    DIRS = {"prod": E["PROD_DIR"], "dev": E["DEV_DIR"]}
    pi = read_env(E["PI_ENV"]) or {}
    snap = {
        "version": int(E["REPORT_VERSION"]),
        "name": pi.get("NOVA_PI_NAME") or None,
        "target": pi.get("NOVA_TARGET") or None,
        "hostname": socket.gethostname(),
        "time": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "legacyUnit": os.path.isfile(E["LEGACY_UNIT"]),
        "active": None,
        "bench": None,
        "deployed": None,
        "instances": {},
    }

    for name in run(as_owner(["tmux", "list-sessions", "-F", "#S"], DIRS["prod"])).splitlines():
        if name in ("NovaGround-dev", "NovaGround-prod"):
            snap["bench"] = name.split("-", 1)[1]
        elif name == "NovaGround" and not snap["bench"]:
            snap["bench"] = "unknown"

    console = None
    for env, d in DIRS.items():
        cfg = read_env(os.path.join(E["ENV_DIR"], "novaGround-%s.env" % env))
        unit = "novaGround@%s.service" % env
        state = run(["systemctl", "is-active", unit]) or "unknown"
        inst = {
            "state": state,
            "enabled": run(["systemctl", "is-enabled", unit]) or "unknown",
            "configured": cfg is not None,
            "broker": (cfg or {}).get("NOVA_BROKER") or None,
            "backend": (cfg or {}).get("NOVA_BACKEND") or None,
            "checkout": None,
            "built": None,
            "lastUpload": read_json(os.path.join(E["STATE_ROOT"], env, "last_upload.json")),
        }
        inst["backendHealth"] = health(inst["backend"])
        if os.path.isdir(os.path.join(d, ".git")):
            branch = git(d, "rev-parse", "--abbrev-ref", "HEAD")
            dirty = git(d, "status", "--porcelain", "--untracked-files=no")
            inst["checkout"] = {
                "dir": d,
                "commit": git(d, "rev-parse", "HEAD") or None,
                "branch": "(detached)" if branch == "HEAD" else (branch or None),
                "dirty": len([l for l in dirty.splitlines() if l.strip()]),
            }
            if snap["target"]:
                inst["built"] = os.access(os.path.join(d, "build", snap["target"]), os.X_OK)
        if state == "active":
            snap["active"] = env
        if not console and cfg and cfg.get("NOVA_CONSOLE") and env == "prod":
            console = console_url(cfg["NOVA_CONSOLE"])
        snap["instances"][env] = inst
    if not console:
        console = console_url((read_env(os.path.join(E["ENV_DIR"], "novaGround-dev.env")) or {}).get("NOVA_CONSOLE"))
    snap["console"] = console

    if snap["legacyUnit"] and run(["systemctl", "is-active", "novaGround.service"]) == "active":
        snap["active"] = "legacy"

    deployed = read_env(E["DEPLOYED_FILE"])
    if deployed:
        snap["deployed"] = {"release": deployed.get("release"), "commit": deployed.get("commit"),
                            "time": deployed.get("deployed")}

    if mode == "json":
        print(json.dumps(snap, indent=2))
        sys.exit(0)

    if mode == "report":
        if not snap["name"]:
            sys.exit("no NOVA_PI_NAME in %s - run: sudo ./nova-pi.sh install" % E["PI_ENV"])
        if not console:
            sys.exit("no NOVA_CONSOLE in the env files yet - Nova.ps1 writes it on 'use' or 'deploy'")
        req = urllib.request.Request(console.rstrip("/") + "/api/pi-report",
                                     data=json.dumps(snap).encode("utf-8"),
                                     headers={"Content-Type": "application/json"}, method="POST")
        try:
            urllib.request.urlopen(req, timeout=4).read()
        except Exception as exc:  # noqa: BLE001
            sys.exit("could not report to %s: %s" % (console, exc))
        sys.exit(0)

    # human
    G, Y, R, C, X = "\033[32m", "\033[33m", "\033[31m", "\033[36m", "\033[0m"
    print()
    print("novaGround on %s - %s (%s)" % (snap["hostname"], snap["name"] or "unnamed Pi", snap["target"] or "no target"))
    print("-" * 70)
    active = snap["active"]
    if active == "legacy":
        print(Y + "    running      the old novaGround.service - migrate: sudo ./nova-pi.sh install" + X)
    elif active:
        print(G + "    running      novaGround@%s" % active + X)
    else:
        print(R + "    running      nothing" + X)
    if snap["bench"]:
        print(Y + "    bench        a run.sh session (%s) is running" % snap["bench"] + X)
    if snap["deployed"]:
        d = snap["deployed"]
        print("    release      %s (%s, deployed %s)" % (d["release"], (d["commit"] or "")[:7], d["time"]))
    for env, inst in snap["instances"].items():
        co = inst["checkout"] or {}
        code = "%s %s" % ((co.get("commit") or "-------")[:7], co.get("branch") or "")
        if co.get("dirty"):
            code += " +%d uncommitted" % co["dirty"]
        if inst["built"] is False:
            code += " (not built)"
        colour = G if inst["state"] == "active" else ""
        print(colour + "    %-12s %-9s %-10s %s" % (env, inst["state"], "(" + inst["enabled"] + ")", code) + (X if colour else ""))
        if not inst["configured"]:
            print(Y + "                 no settings file - Nova.ps1 writes it on 'use %s'" % env + X)
        else:
            h = inst["backendHealth"] or {}
            hc = G if h.get("ok") else R
            print("                 backend %s %s%s%s" % (inst["backend"], hc, "answers" if h.get("ok") else "NO ANSWER (%s)" % h.get("detail"), X))
        up = inst["lastUpload"]
        if up:
            uc = G if up.get("ok") else R
            print("                 last upload %s %s%s%s" % (up.get("time"), uc, "OK" if up.get("ok") else "FAILED", X))
    print("    console      %s" % (console or "(not set yet)"))
    print()


PROC = E.get("PROC_ROOT", "/proc")
SYS = E.get("SYS_ROOT", "/sys")


def read_text(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return fh.read()
    except Exception:
        return None


def cpu_times(text):
    """/proc/stat -> {"total": [...], "cores": {"cpu0": [...]}} (jiffies)."""
    out = {"total": None, "cores": {}}
    for line in (text or "").splitlines():
        parts = line.split()
        if not parts or not parts[0].startswith("cpu"):
            continue
        vals = [int(v) for v in parts[1:] if v.isdigit()]
        if parts[0] == "cpu":
            out["total"] = vals
        else:
            out["cores"][parts[0]] = vals
    return out


def meminfo(text):
    out = {}
    for line in (text or "").splitlines():
        key, _, rest = line.partition(":")
        fields = rest.split()
        if fields and fields[0].isdigit():
            out[key] = int(fields[0])  # kB
    keep = ("MemTotal", "MemFree", "MemAvailable", "Buffers", "Cached", "SwapTotal", "SwapFree")
    return {k: out[k] for k in keep if k in out}


def diskstats(text):
    out = {}
    for line in (text or "").splitlines():
        f = line.split()
        if len(f) >= 14 and f[2].startswith(("mmcblk", "sd", "nvme")):
            out[f[2]] = {"reads": int(f[3]), "sectorsRead": int(f[5]), "writes": int(f[7]),
                         "sectorsWritten": int(f[9]), "ioMs": int(f[12])}
    return out


def key_values(text):
    out = {}
    for line in (text or "").splitlines():
        key, sep, value = line.partition("=")
        if sep:
            out[key.strip()] = value.strip()
    return out


def instance_pid(pi):
    """PID of the running novaGround/novaThermo/novaMock: systemd's MainPID, else pgrep."""
    for env in ("prod", "dev"):
        pid = run(["systemctl", "show", "-p", "MainPID", "--value", "novaGround@%s.service" % env])
        if pid.isdigit() and int(pid) > 0:
            return int(pid), env
    for name in ("novaGround", "novaThermo", "novaMock"):
        pid = (run(["pgrep", "-x", name]).splitlines() or [""])[0]
        if pid.isdigit():
            return int(pid), "bench"
    return None, None


def process_info(pid):
    if pid is None:
        return None
    stat = read_text("%s/%d/stat" % (PROC, pid))
    status = read_text("%s/%d/status" % (PROC, pid))
    if stat is None:
        return None
    # comm may contain spaces, so split after its closing parenthesis.
    rest = stat.rsplit(")", 1)[-1].split()
    info = {"pid": pid, "utimeTicks": int(rest[11]), "stimeTicks": int(rest[12]),
            "threads": int(rest[17]), "startTicks": int(rest[19])}
    for line in (status or "").splitlines():
        if line.startswith("VmRSS:"):
            info["rssKb"] = int(line.split()[1])
    return info


def metrics():
    stat = cpu_times(read_text(PROC + "/stat"))
    load = (read_text(PROC + "/loadavg") or "").split()
    temp = read_text(SYS + "/class/thermal/thermal_zone0/temp")
    pid, via = instance_pid(None)
    timedate = key_values(run(["timedatectl", "show"]))
    out = {
        "version": 1,
        "hostname": socket.gethostname(),
        "time": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "clockTicksPerSec": os.sysconf("SC_CLK_TCK") if hasattr(os, "sysconf") else 100,
        "uptimeSec": float((read_text(PROC + "/uptime") or "0").split()[0]),
        "cpu": stat,
        "load": [float(v) for v in load[:3]] if len(load) >= 3 else None,
        "memory": meminfo(read_text(PROC + "/meminfo")),
        "tempC": int(temp) / 1000.0 if temp and temp.strip().lstrip("-").isdigit() else None,
        "throttled": key_values(run(["vcgencmd", "get_throttled"])).get("throttled"),
        "instance": dict(process_info(pid) or {}, via=via) if pid else None,
        "disks": diskstats(read_text(PROC + "/diskstats")),
        "clock": {k: timedate.get(k) for k in ("NTPSynchronized", "NTP", "Timezone", "TimeUSec")},
    }
    chrony = run(["chronyc", "tracking"]) if run(["which", "chronyc"]) else ""
    out["clock"]["chrony"] = chrony or None
    print(json.dumps(out, indent=2))


def release(console, which):
    try:
        with urllib.request.urlopen(f"{console_url(console)}/api/releases/{which}", timeout=5) as resp:
            data = json.load(resp)
    except Exception as exc:  # noqa: BLE001
        sys.exit(f"could not ask {console} about '{which}': {exc}")
    commit = ((data.get("pins") or {}).get("pi/novaGround") or {}).get("commit")
    if not commit:
        sys.exit(f"release '{which}' pins no novaGround commit")
    print(data.get("tag", which), commit)


def main(argv):
    if len(argv) == 3 and argv[1] == "snapshot" and argv[2] in ("json", "human", "report"):
        snapshot(argv[2])
    elif len(argv) == 2 and argv[1] == "metrics":
        metrics()
    elif len(argv) == 4 and argv[1] == "release":
        release(argv[2], argv[3])
    elif len(argv) == 3 and argv[1] == "console-url":
        print(console_url(argv[2]) or "")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
