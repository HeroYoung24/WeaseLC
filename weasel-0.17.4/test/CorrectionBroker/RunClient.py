"""Run the compiled synthetic client without opening a console window."""
import argparse
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--client", required=True)
parser.add_argument("--server", required=True)
parser.add_argument("--root", required=True)
parser.add_argument("--output", required=True)
parser.add_argument("--mode", choices=["on", "off"], required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parents[2]
client, server, output = (Path(value).resolve() for value in (args.client, args.server, args.output))
for executable in (client, server):
    if not executable.is_relative_to(source / "output" / "BrokerTests") or executable.name != "BrokerCheck.exe":
        raise RuntimeError("Only the compiled synthetic broker test is allowed.")
if not output.is_relative_to(source / "output" / "BrokerTests") or output.exists():
    raise RuntimeError("Use a fresh report inside output/BrokerTests.")
result = subprocess.run([str(client), "--client", str(server), str(output), args.root, args.mode],
                        creationflags=subprocess.CREATE_NO_WINDOW, timeout=15, check=False)
raise SystemExit(result.returncode)
