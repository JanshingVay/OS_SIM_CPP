#!/usr/bin/env python3
"""HTTP integration test for the OS simulator.

The script expects os_simulator to be running on http://127.0.0.1:8080 by
default. It uses only Python's standard library so it can run in a clean
course-design environment.
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request


def request_json(method, url, payload=None, timeout=3):
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def wait_server(base_url, seconds=15):
    deadline = time.time() + seconds
    last_error = None
    while time.time() < deadline:
        try:
            request_json("GET", f"{base_url}/status", timeout=1)
            return
        except Exception as exc:  # noqa: BLE001
            last_error = exc
            time.sleep(0.5)
    raise RuntimeError(f"server is not ready: {last_error}")


def check(name, condition):
    if not condition:
        raise AssertionError(name)
    print(f"[PASS] {name}")


def command(base_url, text):
    res = request_json("POST", f"{base_url}/command", {"command": text})
    check(f"command status: {text}", res.get("status") == "success")
    return res


def status(base_url):
    return request_json("GET", f"{base_url}/status")


def all_processes(snapshot):
    result = []
    for key in [
        "runningProcess",
        "readyQueue",
        "blockQueue",
        "suspendQueue",
        "zombieQueue",
        "finishedQueue",
    ]:
        value = snapshot.get(key)
        if isinstance(value, list):
            result.extend(value)
        elif isinstance(value, dict):
            result.append(value)
    return result


def find_pid(snapshot, name):
    for proc in all_processes(snapshot):
        if proc.get("name") == name:
            return proc.get("pid")
    raise AssertionError(f"cannot find process {name}")


def has_pid(snapshot, queue_name, pid):
    queue = snapshot.get(queue_name)
    if isinstance(queue, dict):
        return queue.get("pid") == pid
    return any(proc.get("pid") == pid for proc in queue or [])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8080/api")
    args = parser.parse_args()

    wait_server(args.base_url)
    boot = request_json("POST", f"{args.base_url}/boot", {})
    check("boot api returns success", boot.get("status") == "success")

    snap = status(args.base_url)
    check("system is booted", snap.get("booted") is True)
    check("boot creates idle/init processes", snap.get("processCount", 0) >= 2)

    suffix = str(int(time.time()))
    p1_name = f"api_p1_{suffix}"
    p2_name = f"api_p2_{suffix}"
    dirname = f"api_dir_{suffix}"
    filename = f"api_file_{suffix}.txt"

    command(args.base_url, f"create {p1_name} 8 4096 10")
    command(args.base_url, f"create {p2_name} 6 4096 6")
    snap = status(args.base_url)
    p1 = find_pid(snap, p1_name)
    p2 = find_pid(snap, p2_name)
    check("created processes appear in status", p1 != p2)

    command(args.base_url, "setsched hrrn")
    check("scheduler switched to HRRN", status(args.base_url).get("currentAlgo") == "HRRN")

    command(args.base_url, f"block {p1} 2 api_block")
    check("blocked process appears in block queue", has_pid(status(args.base_url), "blockQueue", p1))
    command(args.base_url, f"wakeup {p1}")
    check("wakeup removes process from block queue", not has_pid(status(args.base_url), "blockQueue", p1))

    command(args.base_url, f"suspend {p2}")
    check("suspended process appears in suspend queue", has_pid(status(args.base_url), "suspendQueue", p2))
    command(args.base_url, f"resume {p2}")
    check("resume removes process from suspend queue", not has_pid(status(args.base_url), "suspendQueue", p2))

    command(args.base_url, "setmem LRU")
    check("memory policy switched to LRU", status(args.base_url).get("replacementPolicy") == "LRU")
    command(args.base_url, f"access {p1} 0x1000")
    command(args.base_url, f"translate {p1} 0x1000")
    check("memory access count increased", status(args.base_url).get("memory_accesses", 0) >= 1)

    command(args.base_url, f"mkdir {dirname}")
    command(args.base_url, f"cd {dirname}")
    command(args.base_url, f"touch {filename}")
    command(args.base_url, f"write {filename} hello_from_api")
    cat = command(args.base_url, f"cat {filename}")
    check("file content can be read back", "hello_from_api" in cat.get("msg", ""))
    command(args.base_url, f"chmod {filename} ro")
    file_info = request_json("GET", f"{args.base_url}/file/{filename}")
    check("file readonly flag is visible", file_info.get("readonly") is True)
    command(args.base_url, f"chmod {filename} rw")
    command(args.base_url, "cd ..")

    command(args.base_url, "sem_create api_sem 1")
    command(args.base_url, f"P api_sem {p1}")
    command(args.base_url, "V api_sem")
    command(args.base_url, f"msg_send {p1} {p2} hello_ipc_api")
    msg = command(args.base_url, f"msg_read {p2}")
    check("ipc message can be read", "hello_ipc_api" in msg.get("msg", ""))

    command(args.base_url, f"io {p1} 1")
    device_snapshot = status(args.base_url).get("devices", [])
    check("device status is exposed", len(device_snapshot) >= 3)
    command(args.base_url, f"release {p1}")

    print("[OK] API integration test passed")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, RuntimeError, urllib.error.URLError) as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        sys.exit(1)
