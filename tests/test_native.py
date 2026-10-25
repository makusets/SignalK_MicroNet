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


def test_standalone_master_and_pairing(tmp_path):
    binary = tmp_path / "master"
    include = tmp_path / "include" / "esphome"
    include.mkdir(parents=True)
    (include / "components").symlink_to(ROOT / "components", target_is_directory=True)
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(ROOT / "tests/stubs"), "-I", str(ROOT), "-I", str(include.parent),
        str(ROOT / "tests/master.cpp"), str(ROOT / "components/micronet_master/master.cpp"),
        str(ROOT / "components/micronet/micronet.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)


def test_cpp_interfaces_against_real_esphome_headers(tmp_path):
    """Check actual ESPHome C++ APIs too; behavioral tests use platform doubles."""
    import esphome

    include = tmp_path / "include" / "esphome"
    (include / "core").mkdir(parents=True)
    (include / "components").mkdir()
    for component in (ROOT / "components").iterdir():
        if component.is_dir() and not component.name.startswith("_"):
            (include / "components" / component.name).symlink_to(component, target_is_directory=True)
    defines = ["#pragma once", '#include "esphome/core/macros.h"']
    defines += [f"#define {name}" for name in (
        "USE_HOST", "ESPHOME_THREAD_MULTI_ATOMICS", "USE_SENSOR",
        "USE_BINARY_SENSOR", "USE_TEXT_SENSOR", "USE_BUTTON",
    )]
    defines += ["#define ESPHOME_LOG_LEVEL 3", "#define ESPHOME_COMPONENT_COUNT 64"]
    defines += [f"#define ESPHOME_ENTITY_{kind}_COUNT 64" for kind in (
        "SENSOR", "BINARY_SENSOR", "TEXT_SENSOR", "BUTTON",
    )]
    (include / "core/defines.h").write_text("\n".join(defines) + "\n")
    subprocess.run([
        "g++", "-std=c++20", "-fsyntax-only", "-I", str(include.parent),
        "-I", str(Path(esphome.__file__).resolve().parent.parent),
        *[str(ROOT / "components" / name / file) for name, file in (
            ("micronet", "micronet.cpp"), ("micronet_cc1101", "micronet_cc1101.cpp"),
            ("micronet_master", "master.cpp"), ("lsm303dlhc", "lsm303dlhc.cpp"),
        )],
    ], check=True)
