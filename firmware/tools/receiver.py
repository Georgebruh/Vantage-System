#!/usr/bin/env python3
"""Receives Vantage node uploads on the gateway computer and appends them to a CSV file."""

import argparse
import csv
import json
import os
from datetime import datetime, timedelta, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer

READING_FIELDS = ["seq", "mode", "storm", "temp_c", "rh", "p_hpa", "dist_mm", "lidar_mm", "level_mm",
                  "rain_tips", "rain_mm", "vbat", "ibat_ma", "vsol", "isol_ma"]
FIELDS = ["measured_at", "received_at", "node", "session"] + READING_FIELDS + ["rssi"]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--out", default="readings.csv")
    args = ap.parse_args()

    seen = set()
    if os.path.exists(args.out):
        with open(args.out, newline="") as f:
            seen = {(r["node"], r["session"], r["seq"]) for r in csv.DictReader(f)}

    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            if self.path != "/ingest":
                self.send_error(404)
                return
            try:
                body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
                node, session = str(body["node"]), str(body["session"])
                readings = body["readings"]
            except (ValueError, KeyError, TypeError):
                self.send_error(400)
                return

            now = datetime.now(timezone.utc).astimezone()
            rows = []
            for r in readings:
                key = (node, session, str(r.get("seq")))
                if key in seen:
                    continue
                seen.add(key)
                row = {k: r.get(k) for k in READING_FIELDS}
                row.update(node=node, session=session, rssi=body.get("rssi"),
                           received_at=now.isoformat(timespec="seconds"),
                           measured_at=(now - timedelta(seconds=r.get("age_s", 0))).isoformat(timespec="seconds"))
                rows.append(row)

            new_file = not os.path.exists(args.out)
            with open(args.out, "a", newline="") as f:
                w = csv.DictWriter(f, fieldnames=FIELDS)
                if new_file:
                    w.writeheader()
                w.writerows(rows)

            for row in rows:
                print(f"{row['measured_at']} {node} #{row['seq']} {row['mode']} dist={row['dist_mm']} "
                      f"rain={row['rain_tips']} T={row['temp_c']} vbat={row['vbat']}", flush=True)
            if body.get("brownouts") or body.get("crashes") or body.get("dropped"):
                print(f"  {node}: brownouts={body.get('brownouts')} crashes={body.get('crashes')} "
                      f"dropped={body.get('dropped')}", flush=True)

            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"ok":true}')

        def log_message(self, *_):
            pass

    print(f"Listening on port {args.port}, writing {args.out}", flush=True)
    HTTPServer(("0.0.0.0", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
