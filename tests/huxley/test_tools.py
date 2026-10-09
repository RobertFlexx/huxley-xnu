#!/usr/bin/env python3
# Copyright (c) 2026 Huxley contributors. SPDX-License-Identifier: BSD-2-Clause
"""Test diagnostic control flow; fixtures do not prove compilation or booting.

All output is isolated under /tmp. Tool discovery, compiler results and object
headers are mocked where needed to exercise blocked-result handling. The only
executed subprocesses belong to the process-group timeout test.
"""

from contextlib import redirect_stdout
from copy import deepcopy
import importlib.util
import io
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch


TOOL_PATH = Path(__file__).resolve().parents[2] / "tools/huxley/hxnu.py"
SPEC = importlib.util.spec_from_file_location("hxnu_diagnostic_tests", TOOL_PATH)
hxnu = importlib.util.module_from_spec(SPEC)
old_bytecode_setting = sys.dont_write_bytecode
try:
    sys.dont_write_bytecode = True
    SPEC.loader.exec_module(hxnu)
finally:
    sys.dont_write_bytecode = old_bytecode_setting
REAL_POPEN = subprocess.Popen


class DiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(self.enterContext(TemporaryDirectory(prefix="hxnu-tools-test-", dir="/tmp")))
        self.output = self.root / "output"
        self.enterContext(patch.object(hxnu, "ROOT", self.root))
        self.enterContext(patch.object(hxnu, "OUTPUT", self.output))
        self.enterContext(patch.object(hxnu, "source_metadata", return_value={
            "source_revision": "diagnostic-test-fixture",
            "scope": "Diagnostic control-flow fixture; no compiler or firmware executed",
        }))
        self.enterContext(patch.object(hxnu, "capture", return_value="diagnostic fixture version"))
        self.enterContext(patch.object(hxnu.subprocess, "Popen",
            side_effect=AssertionError("Only the timeout test may execute subprocesses")))
        self.enterContext(redirect_stdout(io.StringIO()))

    def build_args(self):
        return SimpleNamespace(arch="x86_64", config="development", jobs=2,
                               sdk="macosx", machine=None, timeout=1)

    def read_json(self, path):
        return json.loads(path.read_text())

    def test_darwin_resources_and_build_arguments(self):
        values = {"hw.memsize": str(32 * 1024**3), "hw.physicalcpu": "8", "hw.logicalcpu": "16"}

        def sysctl(command, **_kwargs):
            self.assertEqual(command[:2], ["/usr/sbin/sysctl", "-n"])
            return values[command[2]] + "\n"

        with patch.object(hxnu.platform, "system", return_value="Darwin"), \
             patch.object(hxnu.subprocess, "check_output", side_effect=sysctl) as discovery, \
             patch.object(hxnu.os, "sysconf", side_effect=AssertionError("Darwin sysconf used")):
            resources = hxnu.host_resources()
        self.assertEqual(discovery.call_count, 3)
        self.assertEqual(resources["memory_bytes"], 32 * 1024**3)
        self.assertEqual(resources["physical_cpus"], 8)
        self.assertEqual(resources["logical_cpus"], 16)

        def unexecuted_build(command, directory, _name, **_kwargs):
            (directory / "make.log").write_text("Diagnostic fixture: make NOT RUN\n")
            return {"status": "NOT RUN", "command": command}

        with patch.object(hxnu, "host_resources", return_value=resources), \
             patch.object(hxnu, "execute", side_effect=unexecuted_build) as execute:
            self.assertEqual(hxnu.build(self.build_args()), 1)
        command = execute.call_args.args[0]
        self.assertIn("SYSCTL_HW_PHYSICALCPU=8", command)
        self.assertIn("SYSCTL_HW_LOGICALCPU=16", command)
        self.assertIn("MEMORY_SIZE=34359738368", command)
        self.assertIn("MAKEJOBS=--jobs=2", command)

    def test_linux_online_topology_and_unknown_physical_count(self):
        topology = self.root / "cpu-topology"
        for number, core, online in ((0, 0, None), (1, 0, "1"), (2, 1, "1"), (3, 2, "0")):
            cpu = topology / ("cpu" + str(number))
            (cpu / "topology").mkdir(parents=True)
            (cpu / "topology/physical_package_id").write_text("0\n")
            (cpu / "topology/core_id").write_text(str(core) + "\n")
            if online is not None:
                (cpu / "online").write_text(online + "\n")
        counts = {"SC_PHYS_PAGES": 1024**2, "SC_PAGE_SIZE": 4096}

        def routed_path(name):
            return topology if name == "/sys/devices/system/cpu" else Path(name)

        with patch.object(hxnu.platform, "system", return_value="Linux"), \
             patch.object(hxnu.os, "sysconf", side_effect=counts.__getitem__), \
             patch.object(hxnu.os, "cpu_count", return_value=4), \
             patch.object(hxnu, "Path", side_effect=routed_path):
            resources = hxnu.host_resources()
            self.assertEqual(resources["memory_bytes"], 4 * 1024**3)
            self.assertEqual(resources["physical_cpus"], 2)
            self.assertEqual(resources["logical_cpus"], 3)
            # Negative topology IDs must not be counted as a discovered core.
            (topology / "cpu2/topology/core_id").write_text("-1\n")
            unavailable = hxnu.host_resources()
        self.assertIsNone(unavailable["physical_cpus"])
        self.assertEqual(unavailable["logical_cpus"], 4)

    def test_missing_compiler_replaces_stale_success(self):
        summary = self.output / "tests/summary.json"
        summary.parent.mkdir(parents=True)
        summary.write_text('{"status":"PASS","scope":"stale fixture"}\n')
        with patch.object(hxnu.shutil, "which", return_value=None), \
             patch.object(hxnu, "execute") as execute:
            self.assertEqual(hxnu.test(SimpleNamespace(cc="missing-fixture-compiler")), 2)
        execute.assert_not_called()
        result = self.read_json(summary)
        self.assertEqual(result["status"], "BLOCKED")
        self.assertEqual(result["missing"], ["compiler missing-fixture-compiler"])
        self.assertEqual(result["results"], [])

    def test_resource_failure_replaces_stale_build_success(self):
        manifest = self.output / "kernel-x86_64-development/result.json"
        manifest.parent.mkdir(parents=True)
        manifest.write_text('{"status":"PASS","scope":"stale fixture"}\n')
        with patch.object(hxnu, "host_resources", side_effect=ValueError("unavailable fixture")), \
             patch.object(hxnu, "execute") as execute:
            self.assertEqual(hxnu.build(self.build_args()), 2)
        execute.assert_not_called()
        result = self.read_json(manifest)
        self.assertEqual(result["status"], "BLOCKED")
        self.assertIn("unavailable fixture", result["missing"][0])

    def test_final_marker_results_agree_and_history_survives_rerun(self):
        directory = self.output / "firmware"
        firmware_code = self.root / "diagnostic-code.fd"
        firmware_vars = self.root / "diagnostic-vars.fd"
        firmware_code.write_bytes(b"Diagnostic fixture, not UEFI firmware")
        firmware_vars.write_bytes(b"Diagnostic variable fixture")
        args = SimpleNamespace(ovmf_code=firmware_code, ovmf_vars=firmware_vars, cycles=1, timeout=1)
        attempts = []
        recorded = []
        original_record = hxnu.record_result

        def fixture_process(command, **kwargs):
            code = 0
            if command[0] == "fixture-clang":
                Path(command[command.index("-o") + 1]).write_bytes(b"Synthetic object; compiler NOT RUN")
            elif command[0] == "fixture-lld-link":
                # Supply only the header fields used by image validation so
                # this test can reach the serial acceptance branch.
                image = bytearray(256)
                image[:2] = b"MZ"
                struct.pack_into("<I", image, 0x3C, 0x80)
                image[0x80:0x84] = b"PE\0\0"
                struct.pack_into("<H", image, 0x84, 0x8664)
                struct.pack_into("<H", image, 0x80 + 24 + 68, 10)
                output = next(arg[len("/out:"):] for arg in command if arg.startswith("/out:"))
                Path(output).write_bytes(image)
            elif command[0] == "fixture-qemu-system-x86_64":
                attempts.append(None)
                serial = command[command.index("-serial") + 1]
                self.assertTrue(serial.startswith("file:"))
                marker = "serial" if len(attempts) == 1 else "memory-map"
                Path(serial[len("file:"):]).write_text("HXNU firmware: " + marker + " PASS\n")
                code = 33  # Successful simulated exit alone must not satisfy acceptance.
            else:
                self.fail("Unexpected diagnostic fixture command: " + command[0])
            kwargs["stdout"].write("Diagnostic fixture %d; no compiler or firmware executed\n" % len(attempts))
            return SimpleNamespace(wait=lambda timeout: code)

        def record(result, output_directory, name):
            if name.startswith("qemu-"):
                recorded.append(deepcopy(result))
            return original_record(result, output_directory, name)

        def attempt():
            with patch.object(hxnu.shutil, "which", side_effect=lambda name: "fixture-" + name), \
                 patch.object(hxnu.subprocess, "Popen", side_effect=fixture_process), \
                 patch.object(hxnu, "record_result", side_effect=record):
                self.assertEqual(hxnu.firmware_test(args), 1)
            summary = self.read_json(directory / "summary.json")
            self.assertEqual(summary["status"], "FAIL")
            result = self.read_json(directory / "qemu-0.json")
            self.assertEqual(result["exit_code"], 33)
            self.assertEqual(result["status"], "FAIL")
            self.assertFalse(all(result["markers"].values()))
            history_json = (self.root / result["attempt_log"]).with_suffix(".json")
            self.assertEqual(result, self.read_json(history_json))
            serial = self.root / result["attempt_serial_log"]
            self.assertEqual(serial.read_bytes(), (self.root / result["serial_log"]).read_bytes())
            return history_json, serial

        first, first_serial = attempt()
        original = first.read_bytes()
        original_log = first.with_suffix(".log").read_bytes()
        original_serial = first_serial.read_bytes()
        second, second_serial = attempt()
        # Recording before acceptance would add a premature PASS snapshot.
        self.assertEqual(len(recorded), 2)
        self.assertTrue(all(result["status"] == "FAIL" and "markers" in result for result in recorded))
        self.assertNotEqual(first, second)
        self.assertEqual(first.read_bytes(), original)
        self.assertEqual(first.with_suffix(".log").read_bytes(), original_log)
        self.assertNotEqual(first.with_suffix(".log").read_bytes(), second.with_suffix(".log").read_bytes())
        self.assertEqual(first_serial.read_bytes(), original_serial)
        self.assertNotEqual(first_serial.read_bytes(), second_serial.read_bytes())

    def test_missing_llvm_nm_prevents_passing_summary(self):
        def discover(name):
            return None if name == "llvm-nm" else "fixture-" + name

        def compiler_fixture(command, directory, name, **_kwargs):
            # Admit synthetic headers only to reach the required inspection
            # branch. Native compilation and execution remain NOT RUN.
            if name.startswith("platform-"):
                cpu = 0x0100000C if "arm64" in name else 0x01000007
                Path(command[-1]).write_bytes(struct.pack("<4I", 0xFEEDFACF, cpu, 0, 1))
                return {"status": "PASS", "scope": "Synthetic header fixture; compiler NOT RUN"}
            return {"status": "NOT RUN", "scope": "Native compiler NOT RUN"}

        with patch.object(hxnu.shutil, "which", side_effect=discover), \
             patch.object(hxnu, "execute", side_effect=compiler_fixture):
            self.assertEqual(hxnu.test(SimpleNamespace(cc="fixture-cc")), 2)
        summary = self.read_json(self.output / "tests/summary.json")
        self.assertEqual(summary["status"], "PARTIAL")
        blocked = [result for result in summary["results"] if result["status"] == "BLOCKED"]
        self.assertEqual(len(blocked), 4)
        self.assertTrue(all(result["missing"] == ["llvm-nm"] for result in blocked))

    @unittest.skipUnless(os.name == "posix" and Path("/proc/self/stat").is_file(),
                         "Linux /proc required to distinguish terminated child from zombie")
    def test_timeout_terminates_parent_and_child_processes(self):
        directory = self.output / "timeout"
        code = ("import subprocess,sys,time; "
                "child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(5)']); "
                "print(child.pid,flush=True); time.sleep(5)")
        with patch.object(hxnu.subprocess, "Popen", new=REAL_POPEN):
            result = hxnu.execute([sys.executable, "-c", code], directory, "timeout", timeout=1)
        self.assertEqual(result["status"], "FAIL")
        self.assertIsNone(result["exit_code"])
        self.assertLess(result["elapsed_seconds"], 4)
        child_pid = int((directory / "timeout.log").read_text().splitlines()[0])

        def child_running():
            try:
                return (Path("/proc") / str(child_pid) / "stat").read_text().split()[2] != "Z"
            except FileNotFoundError:
                return False

        try:
            deadline = time.monotonic() + 2
            while child_running() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertFalse(child_running(), "Timeout left a live descendant")
        finally:
            if child_running():
                os.kill(child_pid, signal.SIGKILL)


if __name__ == "__main__":
    unittest.main()
