"""Side-specific native fuel budgets: unequal headers must produce unequal exhaustion counts."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
paths = (ROOT / "bots/contenders/warband/tools/packing_probe.py", Path(__file__).with_name("probe.py"))
for index, path in enumerate(paths):
    spec = importlib.util.spec_from_file_location(f"fuel_driver_{index}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    header = [0] * 14
    header[3], header[7] = 100, 80
    stream = tuple(header + [75, 85, 95])
    assert module.fuel_summary(stream, "A") == (95, 100, 0)
    assert module.fuel_summary(stream, "B") == (95, 80, 2)
    assert module.fuel_summary(tuple(header), "B") == (0, 80, 0)
    for subject in ("", "C"):
        try:
            module.fuel_summary(stream, subject)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid side accepted")
print("both probe drivers: asymmetric fuel header regressions passed")
