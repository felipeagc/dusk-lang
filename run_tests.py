#!/usr/bin/env python

import os, subprocess

# Go to base dir
os.chdir(os.path.dirname(os.path.realpath(__file__)))

if not os.path.exists("./build"):
    subprocess.run(["cmake", "-Bbuild", os.getcwd()])

subprocess.run(["cmake", "--build", "build"])

if os.name == "nt":
    if os.path.exists("build/duskc.exe"):
        compiler_exe = "build/duskc.exe"
    elif os.path.exists("build/Debug/duskc.exe"):
        compiler_exe = "build/Debug/duskc.exe"
    elif os.path.exists("build/Release/duskc.exe"):
        compiler_exe = "build/Release/duskc.exe"
else:
    compiler_exe = "./build/duskc"


def run_proc(cmd_line):
    print("     Running:", cmd_line)
    return subprocess.run(cmd_line.split(" ")).returncode == 0


failed_tests = []
for filename in os.listdir("./tests/"):
    if not filename.endswith(".dusk"):
        continue

    test_name = os.path.splitext(filename)[0]
    print(f"  => Testing: {test_name}")
    in_path = f"tests/{test_name}.dusk"
    out_path = f"tests/out/{test_name}.spv"
    success = run_proc(f"{compiler_exe} {in_path} -o {out_path}")
    if test_name.startswith("valid"):
        if not success:
            failed_tests.append(test_name)
            continue

        success = run_proc(f"spirv-val {out_path}")
        if not success:
            failed_tests.append(test_name)
            continue
    elif test_name.startswith("invalid"):
        if success:
            failed_tests.append(test_name)
            continue


if len(failed_tests) > 0:
    print("Tests failed:")
    for f in failed_tests:
        print(f)
    exit(1)
else:
    print("All tests passed")
    exit(0)
