"""Internal slot-aware entry point for the actual retained native camp domain."""
from pathlib import Path
import runpy
import sys
sys.argv.extend(["--driver", str(Path(__file__).with_suffix(".mjs"))])
runpy.run_path(str(Path(__file__).with_name("native_oracle.py")), run_name="__main__")
