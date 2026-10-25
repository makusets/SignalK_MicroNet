"""Exercise the user workflow with only YAML/secrets outside the checkout."""
from pathlib import Path
import re
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("optional_sensors,frequency", [(False,"915.915"),(False,"869.840"),(True,"915.915")])
def test_github_yaml_generates_without_local_checkout(tmp_path, optional_sensors, frequency):
    config = (ROOT / "examples/micronet-signalk.yaml").read_text()
    config = re.sub(r'(^  radio_frequency: )"[^"]+"', rf'\g<1>"{frequency}"', config, count=1, flags=re.MULTILINE)
    if optional_sensors:
        config = config.replace("# packages:", "packages:")
        config = config.replace("#   gps:", "  gps:").replace("#   compass:", "  compass:")
    (tmp_path / "micronet-signalk.yaml").write_text(config)
    (tmp_path / "secrets.yaml").write_text((ROOT / "examples/secrets.yaml.example").read_text())
    assert {p.name for p in tmp_path.iterdir()} == {"micronet-signalk.yaml", "secrets.yaml"}
    result = subprocess.run([
        sys.executable, "-m", "esphome", "compile", "--only-generate",
        str(tmp_path / "micronet-signalk.yaml"),
    ], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
