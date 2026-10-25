"""Compile and run the actual firmware decoder/math on the host with sanitizers."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def test_decoder_and_compass(tmp_path):
    binary = tmp_path / "native"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(ROOT), str(ROOT / "tests/native.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)


def test_streaming_receiver_and_sensor_lifecycle(tmp_path):
    binary = tmp_path / "receiver"
    include = tmp_path / "include" / "esphome"
    include.mkdir(parents=True)
    (include / "components").symlink_to(ROOT / "components", target_is_directory=True)
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(ROOT / "tests/stubs"), "-I", str(ROOT),
        "-I", str(include.parent),
        str(ROOT / "tests/receiver.cpp"),
        str(ROOT / "components/micronet/micronet.cpp"),
        str(ROOT / "components/micronet_cc1101/micronet_cc1101.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
