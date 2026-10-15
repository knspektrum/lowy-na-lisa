#!/usr/bin/env bash
# Reads the panel's remote debug log (events the page at https://mxg.sh/lowy-na-lisa/
# sends as GET log?s=<session>&n=<seq>&d=<json lines>) from the nginx access log on la-casa.
#   software/ota/remote-log.sh            # last 200 events
#   software/ota/remote-log.sh 1000 abc12 # last 1000 events of session abc12
set -euo pipefail
n=${1:-200}
session=${2:-}
ssh -o BatchMode=yes la-casa "grep -h 'GET /lowy-na-lisa/log?' /opt/docker/data/swag/config/log/nginx/access.log" |
  python3 -c '
import json, re, sys, urllib.parse
n, want = int(sys.argv[1]), sys.argv[2]
out = []
for line in sys.stdin:
    m = re.search(r"\[([^\]]+)\] \"GET /lowy-na-lisa/log\?(\S+) HTTP[^\"]*\" \d+ \d+ \"[^\"]*\" \"([^\"]*)\"", line)
    if not m:
        continue
    when, query, ua = m.groups()
    q = urllib.parse.parse_qs(query)
    s = q.get("s", ["?"])[0]
    if want and s != want:
        continue
    for item in q.get("d", [""])[0].split("\n"):
        try:
            e = json.loads(item)
        except ValueError:
            e = {"ev": "raw", "msg": item}
        ev = e.pop("ev", "?")
        t = e.pop("t", "")
        out.append(f"{when[12:20]} {s} +{t}ms {ev} " + " ".join(f"{k}={v}" for k, v in e.items()))
print("\n".join(out[-n:]))
' "$n" "$session"
