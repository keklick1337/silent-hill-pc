#!/usr/bin/env python3
"""Check that a release package can start on a machine that has nothing of the build.

The set of libraries a package needs is read from the binaries themselves, not from a list kept by hand: every
executable and library in the package is opened, its imports are followed (the PE import and delay-import tables on
Windows, DT_NEEDED on Linux), and each imported library must either be in the package or belong to the operating
system. tools/ci/package.sh uses the same walk to decide what to copy, and runs the full check on the archive it
has written.

  check_package.py missing <dir> <linux|windows>
      print the libraries the binaries of <dir> import that are neither in <dir> nor the system's, one per line
      ("<library>\t<imported by>")

  check_package.py check <archive or dir> <linux|windows> [--run]
      the full check: required files, no disc-derived content, the import walk, on Linux run paths that are
      $ORIGIN only. --run also starts the game from the unpacked package with --version and --help (Windows:
      under wine) in a directory that holds nothing else. Exit code 0 only when everything holds.
"""

import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile

# Libraries of Windows itself (lower case, without ".dll"). Everything else an exe imports must be in the package.
WINDOWS_SYSTEM = {
    "advapi32", "avrt", "bcrypt", "bcryptprimitives", "cfgmgr32", "comctl32", "comdlg32", "crypt32", "d3d11",
    "d3d12", "d3d9", "dbghelp", "dinput8", "dnsapi", "dsound", "dwmapi", "dwrite", "dxgi", "gdi32", "glu32", "hid",
    "imm32", "iphlpapi", "kernel32", "ksuser", "mmdevapi", "msvcrt", "mswsock", "ncrypt", "netapi32", "ntdll",
    "ole32", "oleaut32", "opengl32", "powrprof", "propsys", "psapi", "rpcrt4", "secur32", "setupapi", "shcore",
    "shell32", "shlwapi", "ucrtbase", "user32", "userenv", "uxtheme", "version", "winhttp", "wininet", "winmm",
    "ws2_32", "wsock32", "xinput1_3", "xinput1_4", "xinput9_1_0",
}
WINDOWS_SYSTEM_PREFIXES = ("api-ms-win-", "ext-ms-")

# Libraries every Linux desktop has: the C library and its parts, the compiler runtime, and the OpenGL entry
# points, which belong to the graphics driver and must never be shipped.
LINUX_SYSTEM = re.compile(
    r"^(ld-linux-x86-64\.so\.2|lib(c|m|dl|pthread|rt|util|resolv)\.so\.\d+|libgcc_s\.so\.1|libstdc\+\+\.so\.6"
    r"|lib(GL|OpenGL|GLX|EGL|GLdispatch)\.so\.\d+)$")

# What the first run writes into gamedata/ from a disc image, and the formats of the disc: never in a package.
FORBIDDEN_PATHS = ("gamedata/raw", "gamedata/convert", "gamedata/tables", "gamedata/version.cfg", "gamedata/save",
                   "gamedata.legacy", "data", "logs", ".savegames")
FORBIDDEN_SUFFIXES = (".cue", ".iso", ".img", ".str", ".xa", ".tim", ".glb", ".vab", ".dmo", ".bin")
# The upscaler's network weights are ncnn .bin files (tools/ncnn/build.sh), not a disc's.
MODEL_DIR = "upscale-models/"

REQUIRED = {
    "common": ["config.cfg", "gamecontrollerdb.txt", "LICENSE.txt", "COPYRIGHT.md", "README.md", "gamedata/game.json",
               "assets/icon/app_128.png", "fonts/JetBrainsMonoNL-Regular.ttf", "upscale-models/realesrgan-x4plus.param",
               "upscale-models/realesrgan-x4plus.bin", "licenses/LICENSE-ncnn.txt", "licenses/LICENSE-Real-ESRGAN.txt"],
    "windows": ["SilentHillPC.exe", "sh-import.exe", "sh-upscale.exe", "software_gl/opengl32.dll",
                "licenses/LICENSE-Mesa-MIT.txt"],
    "linux": ["SilentHillPC", "sh-import", "sh-upscale", "install-desktop.sh", "silent-hill-pc.desktop.in"],
}


def fail(message):
    print("check_package: " + message, file=sys.stderr)
    sys.exit(1)


def pe_imports(path):
    """Names of the DLLs a PE file imports (import and delay-import tables), or None when it is not a PE file."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 0x40 or data[:2] != b"MZ":
        return None
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        return None
    sections_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    directories = optional + (112 if magic == 0x20B else 96)
    sections = []
    table = optional + optional_size
    for i in range(sections_count):
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, table + i * 40 + 8)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset))

    def offset(rva):
        for address, size, raw in sections:
            if address <= rva < address + size:
                return raw + rva - address
        return None

    def text(rva):
        at = offset(rva)
        if at is None:
            return None
        return data[at:data.index(b"\0", at)].decode("ascii", "replace")

    names = []
    # (index of the data directory, size of one descriptor, where the name's RVA sits in it)
    for index, size, name_at in ((1, 20, 12), (13, 32, 4)):
        rva = struct.unpack_from("<I", data, directories + index * 8)[0]
        at = offset(rva) if rva else None
        while at is not None and at + size <= len(data) and any(data[at:at + size]):
            name = text(struct.unpack_from("<I", data, at + name_at)[0])
            if name:
                names.append(name)
            at += size
    return names


def elf_dynamic(path):
    """(needed libraries, run path entries) of an ELF file, or None when it is not one."""
    with open(path, "rb") as f:
        if f.read(4) != b"\x7fELF":
            return None
    out = subprocess.run(["readelf", "-d", path], capture_output=True, text=True, check=False).stdout
    needed = re.findall(r"\(NEEDED\)\s+Shared library: \[(.*?)\]", out)
    run_paths = []
    for entry in re.findall(r"\((?:RPATH|RUNPATH)\)\s+Library r(?:un)?path: \[(.*?)\]", out):
        run_paths += [p for p in entry.split(":") if p]
    return needed, run_paths


def binaries(root, platform):
    """Every executable and library of the package: (path, imported names, run paths)."""
    found = []
    for directory, _, files in os.walk(root):
        for name in sorted(files):
            path = os.path.join(directory, name)
            if os.path.islink(path):
                continue
            if platform == "windows":
                if not name.lower().endswith((".exe", ".dll")):
                    continue
                imports = pe_imports(path)
                if imports is None:
                    fail(f"{os.path.relpath(path, root)} is not a PE file")
                found.append((path, imports, []))
            else:
                dynamic = elf_dynamic(path)
                if dynamic is not None:
                    found.append((path, dynamic[0], dynamic[1]))
    return found


def is_system(name, platform):
    if platform == "windows":
        base = name.lower()
        base = base[:-4] if base.endswith(".dll") else base
        return base in WINDOWS_SYSTEM or base.startswith(WINDOWS_SYSTEM_PREFIXES)
    return bool(LINUX_SYSTEM.match(name))


def missing_imports(root, platform):
    """Imports that are neither a file of the package nor a system library: [(library, imported by)]."""
    found = binaries(root, platform)
    # A library is found beside the binary that asks for it: Windows looks in the exe's directory, and the Linux
    # binaries carry the run path $ORIGIN. Windows file names do not tell upper case from lower.
    key = (lambda n: n.lower()) if platform == "windows" else (lambda n: n)
    have = {(os.path.dirname(path), key(os.path.basename(path))) for path, _, _ in found}
    missing = []
    for path, imports, _ in found:
        for name in imports:
            if (os.path.dirname(path), key(name)) in have or is_system(name, platform):
                continue
            missing.append((name, os.path.relpath(path, root)))
    return missing


def unpack(archive, into):
    if archive.endswith(".zip"):
        with zipfile.ZipFile(archive) as z:
            z.extractall(into)
    else:
        with tarfile.open(archive) as t:
            if hasattr(tarfile, "tar_filter"):
                t.extractall(into, filter="tar")
            else:
                t.extractall(into)
    entries = os.listdir(into)
    if len(entries) != 1 or not os.path.isdir(os.path.join(into, entries[0])):
        fail(f"{archive} must hold exactly one top-level directory, found {entries}")
    return os.path.join(into, entries[0])


def run_smoke(root, platform):
    """Start the game from the package: --version and --help print the engine version and exit 0."""
    exe = os.path.join(root, "SilentHillPC.exe" if platform == "windows" else "SilentHillPC")
    command = [exe]
    env = dict(os.environ)
    if platform == "windows":
        wine = shutil.which("wine") or shutil.which("wine64")
        if not wine:
            fail("--run needs wine for a Windows package")
        command = [wine, exe]
        env.setdefault("WINEDEBUG", "-all")
    else:
        # Only what the package brings and the system has: no library path of the build machine.
        env.pop("LD_LIBRARY_PATH", None)
    for option in ("--version", "--help"):
        result = subprocess.run(command + [option], cwd=root, env=env, capture_output=True, text=True, timeout=300,
                                check=False)
        if result.returncode != 0:
            fail(f"{' '.join(command)} {option} exited with {result.returncode}:\n{result.stdout}{result.stderr}")
        # A windows-subsystem exe has no console under wine unless its output is a pipe, which it is here.
        if "Silent Hill PC" not in result.stdout:
            fail(f"{option} did not print the engine version:\n{result.stdout}{result.stderr}")
        print(f"check_package: {option}: {result.stdout.strip().splitlines()[-1 if option == '--version' else 0]}")


def check(target, platform, run):
    with tempfile.TemporaryDirectory(prefix="sh-package-check-") as scratch:
        root = target if os.path.isdir(target) else unpack(target, scratch)
        errors = []

        for rel in REQUIRED["common"] + REQUIRED[platform]:
            if not os.path.isfile(os.path.join(root, rel)):
                errors.append(f"missing {rel}")
        licenses = os.path.join(root, "licenses")
        if not os.path.isdir(licenses) or len(os.listdir(licenses)) < 8:
            errors.append("licenses/ is missing or nearly empty")

        for directory, dirs, files in os.walk(root):
            for name in dirs + files:
                rel = os.path.relpath(os.path.join(directory, name), root).replace(os.sep, "/")
                if rel in FORBIDDEN_PATHS or (name.lower().endswith(FORBIDDEN_SUFFIXES) and
                                              not rel.startswith(MODEL_DIR)):
                    errors.append(f"disc-derived or run-time content in the package: {rel}")

        found = binaries(root, platform)
        for name, importer in missing_imports(root, platform):
            errors.append(f"{importer} imports {name}, which is neither in the package nor a system library")
        for path, _, run_paths in found:
            for entry in run_paths:
                if entry != "$ORIGIN" and not entry.startswith("$ORIGIN/"):
                    errors.append(f"{os.path.relpath(path, root)} has the run path {entry} (only $ORIGIN may be there)")
        if platform == "linux":
            # A run path is not inherited: every binary that asks for a library of the package must itself say
            # $ORIGIN, or the loader takes the system's library of that name (or none).
            here = {(os.path.dirname(path), os.path.basename(path)) for path, _, _ in found}
            for path, imports, run_paths in found:
                packaged = [name for name in imports if (os.path.dirname(path), name) in here]
                if packaged and "$ORIGIN" not in run_paths:
                    errors.append(f"{os.path.relpath(path, root)} imports {', '.join(packaged)} of the package "
                                  "but has no $ORIGIN run path")
            for path, _, _ in found:
                if not os.path.basename(path).startswith("lib") and not os.access(path, os.X_OK):
                    errors.append(f"{os.path.relpath(path, root)} is not executable")
            if not os.access(os.path.join(root, "install-desktop.sh"), os.X_OK):
                errors.append("install-desktop.sh is not executable")

        if errors:
            for error in errors:
                print("check_package: " + error, file=sys.stderr)
            sys.exit(1)

        bundled = sorted(os.path.relpath(path, root) for path, _, _ in found)
        system = sorted({name for _, imports, _ in found for name in imports if is_system(name, platform)},
                        key=str.lower)
        print(f"check_package: binaries in the package: {', '.join(bundled)}")
        print(f"check_package: system libraries they ask for: {', '.join(system)}")
        if run:
            run_smoke(root, platform)
        print(f"check_package: {os.path.basename(target.rstrip('/'))} is complete")


def main(argv):
    if len(argv) >= 4 and argv[1] == "missing" and argv[3] in ("linux", "windows"):
        for name, importer in missing_imports(argv[2], argv[3]):
            print(f"{name}\t{importer}")
        return
    if len(argv) >= 4 and argv[1] == "check" and argv[3] in ("linux", "windows"):
        check(argv[2], argv[3], "--run" in argv[4:])
        return
    sys.stderr.write(__doc__)
    sys.exit(2)


if __name__ == "__main__":
    main(sys.argv)
