#!/usr/bin/env python3
"""Replay pinned progression SQL only on a network-isolated disposable DB copy."""
import datetime
import hashlib
import json
import pathlib
import subprocess
import sys

container, source, destination = sys.argv[1:]
assert container.startswith("z790-reconcile-db-"), "Refusing non-lab target"
source, destination = pathlib.Path(source), pathlib.Path(destination)

def query(text, timeout=180):
    result = subprocess.run(["docker", "exec", "-i", container, "bash", "-c",
        'export MYSQL_PWD="$(cat /run/secrets/mysql-root)"; exec mysql -uroot -N -B acore_world'],
        input=text, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stderr[:4000])
    return result.stdout

def checksums():
    tables = query("SHOW TABLES;").splitlines()
    result = query("CHECKSUM TABLE " + ",".join("`"+name+"`" for name in tables) + " EXTENDED;")
    return dict(line.split("\t", 1) for line in result.splitlines())

report = {"startedAtUTC": datetime.datetime.now(datetime.timezone.utc).isoformat(),
          "target": container, "productionWrites": False, "files": []}
report["before"] = checksums()
print("BEFORE_CHECKSUMS", len(report["before"]), flush=True)
for index, path in enumerate(sorted(source.glob("*.sql")), 1):
    record = {"file": path.name, "sha1": hashlib.sha1(path.read_bytes()).hexdigest()}
    try:
        query(path.read_text(encoding="utf-8"))
        record["status"] = "applied"
    except Exception as exc:
        record.update(status="failed", error=str(exc))
        report["files"].append(record)
        break
    report["files"].append(record)
    if index % 25 == 0:
        print("FILES_APPLIED", index, flush=True)
report["after"] = checksums()
report["changedTables"] = [name for name in sorted(set(report["before"]) | set(report["after"]))
                          if report["before"].get(name) != report["after"].get(name)]
report["finishedAtUTC"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
report["allFilesApplied"] = len(report["files"]) == len(list(source.glob("*.sql"))) and all(
    item["status"] == "applied" for item in report["files"])
destination.write_text(json.dumps(report, indent=2))
print(json.dumps({key: report[key] for key in ["target", "allFilesApplied", "changedTables"]}), flush=True)
sys.exit(0 if report["allFilesApplied"] else 1)
