import os

Import("env")

version_file = os.path.join(env["PROJECT_DIR"], "firmware\\version.txt")

with open(version_file, "r") as f:
    build_num = int(f.read().strip())

build_num += 1

with open(version_file, "w") as f:
    f.write(str(build_num))

env.Append(CPPDEFINES=[("FW_BUILD", build_num)])
print(f"[eDrum] Build number: {build_num}")