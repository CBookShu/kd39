# Build With vcpkg

## Prerequisites

This project uses **vcpkg Manifest Mode**.

You need:

- a working `VCPKG_ROOT`
- CMake `>= 3.24`
- Ninja in `PATH`
- a C++ toolchain for your host platform

The current manifest baseline is aligned to the local vcpkg checkout that is already available on this machine.

## Set `VCPKG_ROOT`

### PowerShell

```powershell
$env:VCPKG_ROOT = "E:/open/vcpkg"
```

### Bash

```bash
export VCPKG_ROOT=/path/to/vcpkg
```

## Configure and build

### Windows

Windows presets use the **Ninja generator** with MSVC `cl.exe`.

Because Ninja does not automatically set up the MSVC development environment,
you must activate it before running CMake. There are two ways to do this:

#### Option 1: Use the wrapper script (recommended)

```bat
scripts\build-windows.bat windows-debug
scripts\build-windows.bat windows-release
```

The script calls `vcvarsall.bat x64`, then runs `cmake --preset` and
`cmake --build --preset` in sequence. Extra arguments are forwarded:

```bat
scripts\build-windows.bat windows-debug --fresh -DKD39_ENABLE_ETCD=ON
```

#### Option 2: Open a VS Developer Command Prompt manually

Open **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu,
then run the preset commands directly:

```bat
cmake --preset windows-debug
cmake --build --preset build-windows-debug
ctest --preset test-windows-debug
```

Both options produce `compile_commands.json` in the build directory for
clangd and other tooling.

### Linux

```bash
cmake --preset linux-debug
cmake --build --preset build-linux-debug
ctest --preset test-linux-debug
```

```bash
cmake --preset linux-release
cmake --build --preset build-linux-release
ctest --preset test-linux-release
```

## clangd integration

A `.clangd` file at the project root points clangd to the Windows Debug
compilation database:

```yaml
CompileFlags:
  CompilationDatabase: build/windows-debug
```

After a successful configure, clangd will pick up include paths, defines,
and compiler flags from `build/windows-debug/compile_commands.json`.

## Optional manifest features

Core dependencies are always installed from `vcpkg.json`.

Optional features are enabled explicitly with `VCPKG_MANIFEST_FEATURES`.

### Tests

```bat
scripts\build-windows.bat windows-debug -DVCPKG_MANIFEST_FEATURES="tests" -DKD39_ENABLE_TESTS=ON
```

### etcd client SDK

```bat
scripts\build-windows.bat windows-debug -DKD39_ENABLE_ETCD=ON
```

Note:

- `etcd-cpp-apiv3` is fetched directly via CMake `FetchContent`, not from `vcpkg`
- the fetched version is pinned by `KD39_ETCD_CPP_APIV3_TAG` in [CMakeLists.txt](../CMakeLists.txt)
- the initial integration uses `KD39_ETCD_CORE_ONLY=ON`, which keeps the dependency surface smaller by avoiding the async runtime and `cpprestsdk`
- `OpenSSL`, `gRPC`, and `protobuf` are still supplied by `vcpkg`
- the shipped preset already includes `CMAKE_POLICY_VERSION_MINIMUM=3.5` to accommodate the upstream project on CMake 4.x

If you later want the async runtime from `etcd-cpp-apiv3`, you will need to revisit its extra dependency chain, especially `cpprestsdk`.

### Observability

```bat
scripts\build-windows.bat windows-debug -DVCPKG_MANIFEST_FEATURES="observability" -DKD39_ENABLE_OBSERVABILITY=ON
```

### Gateway networking extras

```bat
scripts\build-windows.bat windows-debug -DVCPKG_MANIFEST_FEATURES="gateway" -DKD39_ENABLE_GATEWAY=ON
```

## Host tools for proto generation

`protobuf` and `grpc` are declared twice in the manifest:

- as normal target dependencies for runtime libraries
- as `"host": true` dependencies for build-time tools

This ensures these tools are available on the build machine:

- `protoc`
- `grpc_cpp_plugin`

`cmake/Dependencies.cmake` and `cmake/ProtoGen.cmake` are already aligned with this host-tool setup.

## Output directories

With presets, build output is written under:

```text
build/<preset-name>/
```

The local manifest installation directory is:

```text
vcpkg_installed/
```

Both are ignored by `.gitignore`.
