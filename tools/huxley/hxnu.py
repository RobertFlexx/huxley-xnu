#!/usr/bin/env python3
# Copyright (c) 2026 Huxley contributors. SPDX-License-Identifier: BSD-2-Clause
"""Execute HXNU checks and the inherited build, retaining commands and logs."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "BUILD" / "huxley"
SERIAL_CASES = (
    "legacy", "zero", "overflow", "above-clock", "non-divisor", "9600",
    "low-valid", "missing", "auto-legacy", "auto-mmio", "auto-pcie",
    "auto-missing", "existing-overrides",
)


def capture(command):
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=15, check=False)
    return result.stdout.strip()


def source_metadata():
    diff = capture(["git", "diff", "--binary", "HEAD"])
    # Include new source files in the identity; ignored build artifacts are excluded.
    digest = hashlib.sha256(diff.encode())
    for name in capture(["git", "ls-files", "--others", "--exclude-standard"]).splitlines():
        path = ROOT / name
        if path.is_file():
            digest.update(name.encode())
            digest.update(path.read_bytes())
    return {
        "source_revision": capture(["git", "rev-parse", "HEAD"]),
        "source_status": capture(["git", "status", "--porcelain=v1"]),
        "working_changes_sha256": digest.hexdigest(),
        "host": platform.platform(),
        "python": sys.version,
        "tool_environment": {key: os.environ[key] for key in
                             ("SDKROOT", "DEVELOPER_DIR", "TOOLCHAINS", "KDKROOT",
                              "RC_DARWIN_KERNEL_VERSION", "SOURCE_DATE_EPOCH") if key in os.environ},
    }


def record_result(result, directory, name):
    """Keep the named result and immutable attempt record in agreement."""
    data = json.dumps(result, indent=2) + "\n"
    (ROOT / result["attempt_log"]).with_suffix(".json").write_text(data)
    (directory / (name + ".json")).write_text(data)
    print(f"{result['status']}: {name} ({result['log']})", flush=True)


def execute(command, directory, name, timeout=60, environment=None, expected_exit=0,
            defer_record=False):
    directory.mkdir(parents=True, exist_ok=True)
    log = directory / (name + ".log")
    start = time.monotonic()
    with log.open("w") as stream:
        try:
            process = subprocess.Popen(command, cwd=ROOT, stdout=stream,
                                       stderr=subprocess.STDOUT, start_new_session=True,
                                       env={**os.environ, **(environment or {})})
        except FileNotFoundError as error:
            stream.write(str(error) + "\n")
            code, status = None, "BLOCKED"
        else:
            try:
                code = process.wait(timeout=timeout)
                status = "PASS" if code == expected_exit else "FAIL"
            except subprocess.TimeoutExpired:
                # Stop make's children as well as the parent on a finite deadline.
                import signal
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass  # The process exited between wait's deadline and kill.
                process.wait()
                code, status = None, "FAIL"
                stream.write("\nExecution timeout\n")
    result = {"command": command, "cwd": str(ROOT), "status": status,
              "exit_code": code, "elapsed_seconds": time.monotonic() - start,
              "log": str(log.relative_to(ROOT)), "environment_overrides": environment or {},
              "expected_exit_code": expected_exit}
    history = directory / "history"
    history.mkdir(exist_ok=True)
    attempt = name + "-" + str(time.time_ns())
    history_log = history / (attempt + ".log")
    shutil.copyfile(log, history_log)
    result["attempt_log"] = str(history_log.relative_to(ROOT))
    if not defer_record:
        record_result(result, directory, name)
    return result


def doctor(_args):
    tools = {}
    for name in ("clang", "clang++", "ld64.lld", "llvm-readobj", "llvm-nm", "make", "mig",
                 "xcrun", "xcodebuild", "qemu-system-x86_64", "qemu-system-aarch64"):
        path = shutil.which(name)
        tools[name] = {"path": path}
        if path and name not in ("mig", "xcrun", "xcodebuild"):
            tools[name]["version"] = capture([path, "--version"]).splitlines()[0]
    if tools["xcrun"]["path"] and not tools["mig"]["path"]:
        candidate = capture([tools["xcrun"]["path"], "--sdk", "macosx", "--find", "mig"])
        if Path(candidate).is_file():
            tools["mig"]["path"] = candidate
    firmware = sorted(str(path) for location in ("/usr/share/OVMF", "/usr/share/ovmf")
                      for path in Path(location).glob("*CODE*.fd"))
    blockers = [f"missing {name}" for name in ("xcrun", "mig") if not tools[name]["path"]]
    if tools["xcrun"]["path"]:
        sdk = capture([tools["xcrun"]["path"], "--sdk", "macosx", "--show-sdk-path"])
        if not sdk or not Path(sdk).is_dir():
            blockers.append("macosx SDK not resolved")
    report = {**source_metadata(), "tools": tools, "ovmf_code_candidates": firmware,
              "upstream_build_status": "BLOCKED" if blockers else "NOT RUN",
              "upstream_build_blockers": blockers,
              "boot_status": "NOT RUN: no HXNU loader or kernel image supplied"}
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / "doctor.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 2 if blockers else 0


def test(args):
    directory = OUTPUT / "tests"
    directory.mkdir(parents=True, exist_ok=True)
    compiler = shutil.which(args.cc)
    if compiler is None:
        report = {**source_metadata(), "status": "BLOCKED",
                  "missing": ["compiler " + args.cc], "results": [],
                  "scope": "Host platform tests and protocol objects, not full kernel builds or boots"}
        (directory / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"BLOCKED: compiler {args.cc} missing")
        return 2
    results = [execute([sys.executable, str(ROOT / "tests/huxley/test_tools.py"), "-v"],
                       directory, "run-tools")]
    # These checks allocate no heap objects. LSan cannot run in ptrace sandboxes;
    # ASan bounds/lifetime checks and UBSan remain enabled and fatal.
    sanitizer_environment = {"ASAN_OPTIONS": "detect_leaks=0:halt_on_error=1",
                             "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    flags = [compiler, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    binary = directory / "test_efi_memory_map"
    results.append(execute(flags + ["-D_DEFAULT_SOURCE", "-Ipexpert",
        "tests/huxley/test_efi_memory_map.c", "-o", str(binary)], directory, "compile-efi"))
    if results[-1]["status"] == "PASS":
        results.append(execute([str(binary)], directory, "run-efi",
                               environment=sanitizer_environment))
    binary = directory / "test_serial"
    # These two warnings already exist in the inherited, unmodified serial driver.
    results.append(execute(flags + ["-Wno-unused-function", "-Wno-unused-but-set-variable",
        "-Itests/huxley/include", "tests/huxley/test_serial.c", "pexpert/i386/pe_serial.c",
        "-o", str(binary)], directory, "compile-serial"))
    if results[-1]["status"] == "PASS":
        for case in SERIAL_CASES:
            results.append(execute([str(binary), case], directory, "serial-" + case,
                                   environment=sanitizer_environment))
    parser_object = directory / "bootargs.o"
    # Darwin's inherited get_range_bounds uses a different int64_t typedef than
    # Linux libc. Suppress its pointer warning only in this fixture object.
    results.append(execute(flags + ["-Wno-incompatible-pointer-types", "-Itests/huxley/include",
        "-Ipexpert", "-c", "pexpert/gen/bootargs.c", "-o", str(parser_object)],
        directory, "compile-boot-parser-native"))
    binary = directory / "test_boot_args"
    if results[-1]["status"] == "PASS":
        results.append(execute(flags + ["-Wno-unused-function", "-Wno-unused-but-set-variable",
            "-Itests/huxley/include", "-Ipexpert", "tests/huxley/test_boot_args.c",
            "pexpert/i386/pe_bootargs.c", "pexpert/i386/pe_serial.c", str(parser_object),
            "-o", str(binary)], directory, "compile-boot-args"))
        if results[-1]["status"] == "PASS":
            results.append(execute([str(binary)], directory, "run-boot-args",
                                   environment=sanitizer_environment))
    # LLVM builds the actual protocol header for both ABIs without an Apple SDK.
    clang = shutil.which("clang")
    llvm_nm = shutil.which("llvm-nm")
    if clang:
        for arch, cpu in (("x86_64", 0x01000007), ("arm64", 0x0100000C)):
            for headers, extra in (("compiler", []), ("kernel", ["-DKERNEL", "-IEXTERNAL_HEADERS", "-Ibsd"])):
                name = "platform-" + headers + "-" + arch
                artifact = directory / (name + ".o")
                command = [clang, "-target", arch + "-apple-darwin", "-std=c11",
                           "-ffreestanding", "-O2", "-Wall", "-Wextra", "-Werror"] + extra
                command += ["-Ipexpert", "-c", "tests/huxley/compile_platform.c", "-o", str(artifact)]
                results.append(execute(command, directory, name))
                if results[-1]["status"] == "PASS":
                    import struct
                    magic, actual_cpu, _, filetype = struct.unpack("<4I", artifact.read_bytes()[:16])
                    valid = magic == 0xFEEDFACF and actual_cpu == cpu and filetype == 1
                    results.append({"status": "PASS" if valid else "FAIL",
                                    "check": "Mach-O relocatable object " + name,
                                    "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest()})
                    if llvm_nm:
                        symbol_name = name + "-undefined-symbols"
                        result = execute([llvm_nm, "--undefined-only", "--format=posix", str(artifact)],
                                         directory, symbol_name, defer_record=True)
                        if result["status"] == "PASS" and (ROOT / result["log"]).read_text().strip():
                            result["status"] = "FAIL"
                        result["check"] = "Protocol object has no undefined symbols"
                        record_result(result, directory, symbol_name)
                        results.append(result)
                    else:
                        results.append({"status": "BLOCKED", "check": "Undefined symbols " + name,
                                        "missing": ["llvm-nm"]})
    else:
        results.append({"status": "BLOCKED", "check": "Clang Mach-O compile: missing clang"})
    summary = {**source_metadata(), "compiler": capture([compiler, "--version"]).splitlines()[0],
               "results": results,
               "status": result_status(results),
               "scope": "Host platform tests and protocol objects, not full kernel builds or boots"}
    (directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(summary["status"] + ": platform checks; no kernel boot claimed")
    return 0 if summary["status"] == "PASS" else (1 if summary["status"] == "FAIL" else 2)


def result_status(results):
    statuses = {result["status"] for result in results}
    if statuses == {"PASS"}:
        return "PASS"
    if "FAIL" in statuses:
        return "FAIL"
    return "BLOCKED" if statuses == {"BLOCKED"} else "PARTIAL"


def host_resources():
    """Discover resources without presenting logical CPUs as physical cores."""
    if platform.system() == "Darwin":
        values = {}
        for field, key in (("memory_bytes", "hw.memsize"), ("physical_cpus", "hw.physicalcpu"),
                           ("logical_cpus", "hw.logicalcpu")):
            values[field] = int(subprocess.check_output(["/usr/sbin/sysctl", "-n", key],
                                text=True, stderr=subprocess.PIPE, timeout=15).strip())
        values["discovery"] = "Darwin sysctl"
    else:
        values = {"memory_bytes": os.sysconf("SC_PHYS_PAGES") * os.sysconf("SC_PAGE_SIZE"),
                  "logical_cpus": os.cpu_count(), "physical_cpus": None,
                  "discovery": "sysconf and os.cpu_count; physical count unavailable"}
        if platform.system() == "Linux":
            try:
                topology = []
                for cpu in Path("/sys/devices/system/cpu").glob("cpu[0-9]*"):
                    if not cpu.name[3:].isdigit():
                        continue
                    online = cpu / "online"
                    if online.exists() and online.read_text().strip() != "1":
                        continue
                    package = int((cpu / "topology/physical_package_id").read_text().strip())
                    core = int((cpu / "topology/core_id").read_text().strip())
                    if package < 0 or core < 0:
                        raise ValueError("CPU topology unavailable")
                    topology.append((package, core))
                if topology:
                    values.update(logical_cpus=len(topology), physical_cpus=len(set(topology)),
                                  discovery="sysconf and Linux online CPU topology")
            except (OSError, ValueError):
                pass  # The manifest reports the unavailable physical count explicitly.
    for field in ("memory_bytes", "logical_cpus", "physical_cpus"):
        if field != "physical_cpus" and values[field] is None:
            raise ValueError("unavailable host resource: " + field)
        if values[field] is not None and values[field] <= 0:
            raise ValueError("invalid discovered host resource: " + field)
    return values


def build(args):
    directory = OUTPUT / ("kernel-" + args.arch + "-" + args.config)
    directory.mkdir(parents=True, exist_ok=True)
    configuration = {key: value for key, value in vars(args).items() if key != "function"}
    blocked = None
    if args.arch == "arm64" and not args.machine:
        blocked = "ARM64 requires an explicit inherited machine, e.g. --machine VMAPPLE"
    else:
        try:
            resources = host_resources()
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            blocked = "host resource discovery: " + str(error)
    if blocked:
        report = {**source_metadata(), "status": "BLOCKED", "missing": [blocked],
                  "configuration": configuration}
        (directory / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print("BLOCKED: " + blocked)
        return 2
    command = ["make", "-j" + str(args.jobs), "all", "MAKEJOBS=--jobs=" + str(args.jobs),
               "ARCH_CONFIGS=" + args.arch.upper(),
               "KERNEL_CONFIGS=" + args.config.upper(), "SDKROOT=" + args.sdk,
               "OBJROOT=" + str(directory / "obj"), "SYMROOT=" + str(directory / "sym"),
               "DSTROOT=" + str(directory / "dst"), "MEMORY_SIZE=" + str(resources["memory_bytes"]),
               "KERNEL_BUILDS_IN_PARALLEL=1"]
    for field, variable in (("physical_cpus", "SYSCTL_HW_PHYSICALCPU"),
                            ("logical_cpus", "SYSCTL_HW_LOGICALCPU")):
        if resources[field] is not None:
            command.append(variable + "=" + str(resources[field]))
    if args.machine:
        command.append("MACHINE_CONFIGS=" + args.machine)
    result = execute(command, directory, "make", timeout=args.timeout)
    report = {**source_metadata(), **result, "configuration": configuration,
              "host_resources": resources}
    (directory / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    if result["status"] != "PASS":
        print("\n".join((directory / "make.log").read_text().splitlines()[-12:]))
    return 0 if result["status"] == "PASS" else 1


def firmware_test(args):
    directory = OUTPUT / "firmware"
    directory.mkdir(parents=True, exist_ok=True)
    required = ("clang", "lld-link", "qemu-system-x86_64")
    missing = [name for name in required if shutil.which(name) is None]
    missing += [str(path) for path in (args.ovmf_code, args.ovmf_vars) if not path.is_file()]
    if missing:
        report = {**source_metadata(), "status": "BLOCKED", "missing": missing}
        (directory / "summary.json").write_text(json.dumps(report, indent=2) + "\n")
        print("BLOCKED: firmware probe needs " + ", ".join(missing))
        return 2
    results = []
    common = [shutil.which("clang"), "-target", "x86_64-pc-windows-msvc", "-std=c11",
              "-ffreestanding", "-mno-red-zone", "-O2", "-Wall", "-Wextra", "-Werror",
              "-Itests/huxley/include", "-Ipexpert", "-c"]
    probe = directory / "probe.obj"
    serial = directory / "serial.obj"
    results.append(execute(common + ["tests/huxley/firmware_probe.c", "-o", str(probe)],
                           directory, "compile-probe"))
    results.append(execute(common + ["-Wno-unused-function", "-Wno-unused-but-set-variable",
                           "pexpert/i386/pe_serial.c", "-o", str(serial)], directory, "compile-serial"))
    bootargs = directory / "pe_bootargs.obj"
    parser = directory / "bootargs.obj"
    results.append(execute(common + ["pexpert/i386/pe_bootargs.c", "-o", str(bootargs)],
                           directory, "compile-boot-args"))
    results.append(execute(common + ["pexpert/gen/bootargs.c", "-o", str(parser)],
                           directory, "compile-boot-parser"))
    disk = directory / "esp"
    image = disk / "EFI" / "BOOT" / "BOOTX64.EFI"
    image.parent.mkdir(parents=True, exist_ok=True)
    if all(result["status"] == "PASS" for result in results):
        results.append(execute([shutil.which("lld-link"), "/subsystem:efi_application",
            "/entry:efi_main", "/nodefaultlib", "/timestamp:0", "/out:" + str(image),
            str(probe), str(serial), str(bootargs), str(parser)],
            directory, "link-probe"))
    if all(result["status"] == "PASS" for result in results):
        data = image.read_bytes()
        import struct
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        machine = struct.unpack_from("<H", data, pe_offset + 4)[0]
        subsystem = struct.unpack_from("<H", data, pe_offset + 24 + 68)[0]
        valid = data[:2] == b"MZ" and data[pe_offset:pe_offset + 4] == b"PE\0\0"
        valid = valid and machine == 0x8664 and subsystem == 10
        results.append({"check": "X64 EFI PE/COFF image", "status": "PASS" if valid else "FAIL",
                        "sha256": hashlib.sha256(data).hexdigest()})
    if all(result["status"] == "PASS" for result in results):
        for cycle in range(args.cycles):
            # Disposable NVRAM and FAT directory: no host disk/boot configuration is used.
            variables = directory / "variables.fd"
            shutil.copyfile(args.ovmf_vars, variables)
            temporary = directory / "tmp"
            temporary.mkdir(exist_ok=True)
            serial_log = directory / ("serial-" + str(cycle) + ".log")
            command = [shutil.which("qemu-system-x86_64"), "-machine", "q35", "-accel", "tcg",
                "-cpu", "Nehalem", "-smp", "1", "-m", "512M", "-display", "none",
                "-monitor", "none", "-nic", "none", "-no-reboot", "-serial", "file:" + str(serial_log),
                "-drive", "if=pflash,format=raw,readonly=on,file=" + str(args.ovmf_code.resolve()),
                "-drive", "if=pflash,format=raw,file=" + str(variables),
                "-drive", "format=raw,snapshot=on,file=fat:" + str(disk),
                "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04"]
            result = execute(command, directory, "qemu-" + str(cycle), timeout=args.timeout,
                             expected_exit=33, environment={"TMPDIR": str(temporary)}, defer_record=True)
            output = serial_log.read_text(errors="replace") if serial_log.is_file() else ""
            markers = ("HXNU firmware: serial PASS", "HXNU firmware: memory-map PASS",
                       "HXNU firmware: reject-zero PASS")
            result["serial_log"] = str(serial_log.relative_to(ROOT))
            result["markers"] = {marker: marker in output for marker in markers}
            if not all(result["markers"].values()) or "HXNU firmware: FAIL" in output:
                result["status"] = "FAIL"
            if serial_log.is_file():
                attempt_serial = (ROOT / result["attempt_log"]).with_suffix(".serial.log")
                shutil.copyfile(serial_log, attempt_serial)
                result["attempt_serial_log"] = str(attempt_serial.relative_to(ROOT))
            record_result(result, directory, "qemu-" + str(cycle))
            results.append(result)
            if result["status"] != "PASS":
                break
    summary = {**source_metadata(), "results": results,
               "status": "PASS" if all(r["status"] == "PASS" for r in results) else "FAIL",
               "qemu": capture([shutil.which("qemu-system-x86_64"), "--version"]).splitlines()[0],
               "firmware_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                                   for path in (args.ovmf_code, args.ovmf_vars)},
               "scope": "OVMF test application: real UART and EFI map; HXNU kernel never loaded"}
    (directory / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(summary["status"] + ": QEMU firmware probe; no HXNU kernel boot claimed")
    return 0 if summary["status"] == "PASS" else 1


def positive(value):
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return parsed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("doctor").set_defaults(function=doctor)
    tests = commands.add_parser("test")
    tests.add_argument("--cc", default="cc")
    tests.set_defaults(function=test)
    kernel = commands.add_parser("build")
    kernel.add_argument("--arch", choices=("x86_64", "arm64"), default="x86_64")
    kernel.add_argument("--config", choices=("debug", "development", "release"), default="development")
    kernel.add_argument("--sdk", default=os.environ.get("SDKROOT", "macosx"))
    kernel.add_argument("--machine")
    kernel.add_argument("--jobs", type=positive, default=min(os.cpu_count() or 1, 4))
    kernel.add_argument("--timeout", type=positive, default=1200)
    kernel.set_defaults(function=build)
    firmware = commands.add_parser("firmware-test")
    firmware.add_argument("--ovmf-code", type=Path, default=Path("/usr/share/OVMF/OVMF_CODE_4M.fd"))
    firmware.add_argument("--ovmf-vars", type=Path, default=Path("/usr/share/OVMF/OVMF_VARS_4M.fd"))
    firmware.add_argument("--timeout", type=positive, default=40)
    firmware.add_argument("--cycles", type=positive, default=1)
    firmware.set_defaults(function=firmware_test)
    args = parser.parse_args()
    return args.function(args)


if __name__ == "__main__":
    sys.exit(main())
