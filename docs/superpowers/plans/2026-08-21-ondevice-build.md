# On-Device Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** iPad の Projucer でビルドボタンを押すと、開いているプロジェクトがコンパイル・署名され、この実機にインストールされる。

**Architecture:** LLVM/clang/lld/zsign は `OnDeviceBuild` 静的ライブラリに閉じる（公開ヘッダにそれらの型を出さない）。Mac は同一ライブラリをリンクした CLI で検証する。Projucer 本体はマニフェスト exporter と iOS の薄い UI だけを持ち、macOS アプリには LLVM をリンクしない。

**Tech Stack:** C++17, Objective-C++, CMake, in-process clang/lld, zsign, OpenSSL 3, JUCE 9 Projucer

**Spec:** `docs/superpowers/specs/2026-08-21-ondevice-build-design.md`

## Global Constraints

- すべての回答・ユーザー向け文言は日本語でよいが、コード・識別子・コミットメッセージは英語。
- `fork`/`exec` しない。clang ドライバも `codesign` も `ld` 実行ファイルも使わない。
- 公開ヘッダに LLVM / clang / lld / zsign の型を出さない。
- コンパイラ引数は `~/Documents/src/PocClangIOS/src/PocClang.cpp` をコピーする。再発明しない。
- 成果物の成功判定は `ondevice::MachOInfo::isIOSArm64Object()` / `isIOSArm64Executable()`（platform == 2）。ファイル生成だけでは失敗。
- macOS の Projucer GUI にビルドボタンを足さない。ホスト入口は CLI のみ。
- LLVM は vendoring しない。default で `~/Documents/src/PocClangIOS/build-host` と `build-ios/llvm-ios.a` を使う。
- GPL コードを取り込まない。zsign は MIT。`zsign.cpp`（main）と `iowin32.c` はビルドしない。
- プッシュしない。ユーザーが明示するまで `main` にマージしない。
- 作業ディレクトリは `/Users/ring2/Documents/src/Projucer8`。ブランチ `feature/ipad-support`。
- 実装中にサブエージェントを spawn しない（controller がレビューする）。

## File map

| Path | Responsibility |
|---|---|
| `OnDeviceBuild/CMakeLists.txt` | lib + host CLI + tests |
| `OnDeviceBuild/include/OnDeviceBuild/*.h` | 公開 API |
| `OnDeviceBuild/src/*.cpp` | Compiler, Linker, Signer, MachOCheck, ZipStore, BundleBuilder, Engine |
| `OnDeviceBuild/src/BuildRunner.mm` | 並列コンパイル |
| `OnDeviceBuild/third_party/zsign/` | vendored zsign sources |
| `OnDeviceBuild/host/main.cpp` | CLI |
| `OnDeviceBuild/tests/` | ホストテスト |
| `Projucer/Source/ProjectSaving/jucer_ProjectExport_OnDevice.h` | manifest exporter |
| `Projucer/Source/OnDevice/jucer_OnDeviceBuildController.*` | iOS UI |
| `Projucer/Source/OnDevice/jucer_LoopbackServer.*` | installer |

---

### Task 1: Scaffold OnDeviceBuild and MachOCheck

**Files:**
- Create: `OnDeviceBuild/CMakeLists.txt`
- Create: `OnDeviceBuild/include/OnDeviceBuild/MachOCheck.h`
- Create: `OnDeviceBuild/src/MachOCheck.cpp`
- Create: `OnDeviceBuild/tests/test_macho.cpp`
- Create: `OnDeviceBuild/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `~/Documents/src/PocClangIOS/src/MachOCheck.{h,cpp}` (copy, wrap in `namespace ondevice`)
- Produces: `ondevice::inspectMachO`, `ondevice::MachOInfo` as in the spec

- [ ] **Step 1: Copy MachOCheck into the new layout**

Copy the PoC files. Put the declarations in `OnDeviceBuild/include/OnDeviceBuild/MachOCheck.h` under `namespace ondevice`. The cpp includes that public header, not a private twin. `isIOSArm64Object` / `isIOSArm64Executable` の判定式は PoC と同じ（`platform == 2`）。

- [ ] **Step 2: Write CMake that builds a test executable without LLVM**

```cmake
cmake_minimum_required(VERSION 3.20)
project(OnDeviceBuild C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(OnDeviceBuildMachO STATIC src/MachOCheck.cpp)
target_include_directories(OnDeviceBuildMachO PUBLIC include)

add_executable(ondevice-test-macho tests/test_macho.cpp)
target_link_libraries(ondevice-test-macho PRIVATE OnDeviceBuildMachO)
```

Task 1 では LLVM をリンクしない。後続タスクが同じ `CMakeLists.txt` を拡張する。

- [ ] **Step 3: Write `tests/test_macho.cpp`**

The test must:

1. Fail clearly if a path does not parse.
2. Inspect a **host** Mach-O that exists on every Mac (the test binary itself via `argv[0]` or `/bin/ls`): `parsed && is64Bit`. It must **not** report `isIOSArm64Executable()` for a macOS binary (`platform` is 1 or missing).
3. If `ONDEVICE_FIXTURE_IOS_EXEC` env is set, inspect that file and require `isIOSArm64Executable()`. Do not require the env for a default pass — default pass is (2).

Print `describe()` on failure.

- [ ] **Step 4: Build and run**

```
cmake -S OnDeviceBuild -B OnDeviceBuild/build-host -DCMAKE_BUILD_TYPE=Release
cmake --build OnDeviceBuild/build-host --target ondevice-test-macho
./OnDeviceBuild/build-host/ondevice-test-macho
```

Expected: exit 0.

- [ ] **Step 5: Commit**

```
git add OnDeviceBuild
git commit -m "feat: add OnDeviceBuild MachO inspector and host test"
```

---

### Task 2: In-process clang compiler

**Files:**
- Create: `OnDeviceBuild/include/OnDeviceBuild/Compiler.h`
- Create: `OnDeviceBuild/src/Compiler.cpp`
- Modify: `OnDeviceBuild/CMakeLists.txt`
- Create: `OnDeviceBuild/tests/test_compile.cpp`
- Create: `OnDeviceBuild/tests/fixtures/uikit.mm`

**Interfaces:**
- Consumes: MachOCheck; `~/Documents/src/PocClangIOS/src/PocClang.cpp` (copy the cc1 flag construction verbatim)
- Produces: `ondevice::compileToObject`, `ondevice::getResidentMemoryBytes`

- [ ] **Step 1: Public header**

Exact struct fields from the spec `CompileRequest` / `CompileResult`. No LLVM includes.

- [ ] **Step 2: Copy `PocClang.cpp` into `Compiler.cpp`**

Keep the search-path order comment and the flag list. Wrap in `namespace ondevice`. Change includes to the public header.

- [ ] **Step 3: CMake — find LLVM/Clang from the PoC host build**

```cmake
set(ONDEVICE_LLVM_DIR "$ENV{HOME}/Documents/src/PocClangIOS/build-host"
    CACHE PATH "LLVM build dir with LLVMConfig.cmake")
find_package(LLVM REQUIRED CONFIG PATHS ${ONDEVICE_LLVM_DIR} NO_DEFAULT_PATH)
find_package(Clang REQUIRED CONFIG PATHS ${ONDEVICE_LLVM_DIR} NO_DEFAULT_PATH)

add_library(OnDeviceBuild STATIC src/MachOCheck.cpp src/Compiler.cpp)
target_include_directories(OnDeviceBuild PUBLIC include
    PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(OnDeviceBuild PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(OnDeviceBuild PRIVATE -fno-rtti)
target_link_libraries(OnDeviceBuild PRIVATE
    LLVMAArch64CodeGen LLVMAArch64AsmParser LLVMAArch64Desc
    LLVMAArch64Info LLVMAArch64Utils
    clangCodeGen clangFrontend clangSerialization clangDriver
    clangParse clangSema clangAnalysis clangEdit clangAST
    clangLex clangBasic clangSupport)
```

Tests that include only public headers must **not** be compiled with `-fno-rtti` if they don't include LLVM. Link them to `OnDeviceBuild`.

- [ ] **Step 4: Fixture and test**

`tests/fixtures/uikit.mm`:

```objc
#import <UIKit/UIKit.h>
int juce_ondevice_marker() { return (int) sizeof (UIView); }
```

`test_compile.cpp` reads env:

- `ONDEVICE_SYSROOT` — required for this test. If unset, print skip message and **exit 0** only when also `ONDEVICE_ALLOW_SKIP=1`; otherwise **exit 1** with the env name. For the implementer's run, locate the SDK: prefer `~/Documents/src/PocLinkIOS` extracted SDK if present, else `xcrun --sdk iphoneos --show-sdk-path`.
- `ONDEVICE_RESOURCE_DIR` — `${ONDEVICE_LLVM_DIR}/lib/clang/22` (the dir that contains `include/stddef.h`). Confirm `include/stddef.h` exists; if clang version folder is not 22, glob `lib/clang/*/include/stddef.h` and use that parent.

Compile to a temp `.o`. Then `inspectMachO` and require `isIOSArm64Object()`. Print diagnostics and fail if clang reports `'UIKit/UIKit.h' file not found` — that means iframework flags are wrong, do not skip.

- [ ] **Step 5: Build and run**

```
cmake -S OnDeviceBuild -B OnDeviceBuild/build-host -DCMAKE_BUILD_TYPE=Release
cmake --build OnDeviceBuild/build-host --target ondevice-test-compile
ONDEVICE_SYSROOT=$(xcrun --sdk iphoneos --show-sdk-path) \
ONDEVICE_RESOURCE_DIR=... \
./OnDeviceBuild/build-host/ondevice-test-compile
```

Expected: PASS, object is arm64 iOS MH_OBJECT.

- [ ] **Step 6: Commit**

```
git add OnDeviceBuild
git commit -m "feat: in-process clang -cc1 compiler for iOS objects"
```

---

### Task 3: In-process lld linker and compiler-rt

**Files:**
- Create: `OnDeviceBuild/include/OnDeviceBuild/Linker.h`
- Create: `OnDeviceBuild/src/Linker.cpp`
- Modify: `OnDeviceBuild/CMakeLists.txt`
- Create: `OnDeviceBuild/tests/test_link.cpp`

**Interfaces:**
- Consumes: Compiler, MachOCheck; `~/Documents/src/PocLinkIOS/src/PocLink.cpp`
- Produces: `ondevice::linkObjects` — must pass `-platform_version ios <min> <sdk>` and `builtinsArchive`

- [ ] **Step 1: Copy PocLink into Linker.cpp / Linker.h as spec'd**

If `canRunAgain` is false, still return the diagnostics; callers refuse further builds.

- [ ] **Step 2: Link lld archives**

Host:

```
${ONDEVICE_LLVM_DIR}/lib/liblldMachO.a
${ONDEVICE_LLVM_DIR}/lib/liblldCommon.a
```

lld has no imported CMake target in this LLVM build (PoC comment). Use absolute paths. Keep `-fno-rtti` on `Linker.cpp`.

- [ ] **Step 3: Test**

Compile the UIKit fixture from Task 2 plus a tiny `int main() { return juce_ondevice_marker(); }` (or call from main in the same file). Link with:

- sysroot from `xcrun --sdk iphoneos --show-sdk-path`
- frameworks `UIKit Foundation`
- libraries `System c++`
- builtins `$HOME/Documents/src/PocClangIOS/build-rt-ios/lib/darwin/libclang_rt.ios.a`

Require `inspectMachO(output).isIOSArm64Executable()`. If the test fails with `___divdc3`, the builtins path is wrong — fix it, do not remove the archive.

- [ ] **Step 4: Build, run, commit**

```
git add OnDeviceBuild
git commit -m "feat: in-process lld Mach-O linker with compiler-rt"
```

---

### Task 4: Bundle, zip, sign, IPA layout

**Files:**
- Create: `OnDeviceBuild/include/OnDeviceBuild/BundleBuilder.h`
- Create: `OnDeviceBuild/src/BundleBuilder.cpp`
- Create: `OnDeviceBuild/include/OnDeviceBuild/Signer.h`
- Create: `OnDeviceBuild/src/Signer.cpp`
- Create: `OnDeviceBuild/third_party/zsign/` (copy sources, not `zsign` build trees)
- Modify: `OnDeviceBuild/CMakeLists.txt`
- Create: `OnDeviceBuild/tests/test_ipa.cpp`
- Create: `OnDeviceBuild/src/Pkcs12.cpp`
- Create: `OnDeviceBuild/include/OnDeviceBuild/Pkcs12.h`

**Interfaces:**
- Consumes: `~/Documents/src/PocSignIOS/src/PocSign.cpp`, zsign-ios.cmake patterns
- Produces: `writeAppBundle`, `signAppFolder`, `extractZip`, `archiveFolder`, `reencodePkcs12Aes`

- [ ] **Step 1: BundleBuilder**

```cpp
struct BundleRequest {
    std::string appFolder;          // ends in .app
    std::string executablePath;     // linked binary to copy in
    std::string bundleId;
    std::string name;
    std::string minimumOSVersion = "17.0";
};
bool writeAppBundle (const BundleRequest&, std::string& error);
```

Write Info.plist with the keys in the spec §8 / tech doc 4.4. Copy executable as `CFBundleExecutable`. `chmod 0755` the executable.

- [ ] **Step 2: Vendor zsign and wrap Signer**

Copy `PocSignIOS/zsign/src` into `OnDeviceBuild/third_party/zsign/src`. Build as static `zsign` lib: glob `*.cpp` `common/*.cpp` zlib and minizip `.c`, exclude `zsign.cpp` and `iowin32.c`.

Host OpenSSL: `find_package(OpenSSL REQUIRED)` (Homebrew). Do not use iOS openssl-ios on the host.

Signer: copy `PocSign.cpp` behaviour including stdout capture to `$TMPDIR`, never next to the `.app`.

- [ ] **Step 3: IPA packaging helper**

```cpp
bool writeIpa (const std::string& appFolder,
               const std::string& ipaPath,
               std::string& error);
```

Create a temp parent with `Payload/<appName>.app` (copy or move), zip **the parent**, output `ipaPath` **outside** that parent. After writing, open the zip and require at least one entry starting with `Payload/`.

- [ ] **Step 4: PKCS#12 reencode**

`reencodePkcs12Aes` uses OpenSSL APIs (not `system("openssl")`). If input already uses AES, writing an AES output still counts as success.

Test: use `~/Documents/src/PocFullChain/assets/identities-modern.p12` if present (already AES) — round-trip must succeed. Optionally also test a Keychain-style RC2 p12 if one is on disk; if not, skip that case with a printed note (exit 0).

- [ ] **Step 5: `test_ipa.cpp`**

Without signing: write a dummy executable (the linked iOS binary from Task 3, or a copied file), `writeAppBundle`, `writeIpa`, list zip entries, fail if any entry is not under `Payload/` or if the executable bit is lost inside the copied app (check `stat` on the file in the app folder before zip).

With signing: if `ONDEVICE_P12` and `ONDEVICE_PROVISION` exist (default the PocFullChain assets), sign the bundle then zip. Failure of sign when assets exist is a test failure.

- [ ] **Step 6: Commit**

```
git add OnDeviceBuild
git commit -m "feat: app bundle, zsign wrapper, IPA Payload layout"
```

---

### Task 5: Engine, ZipStore, host CLI

**Files:**
- Create: `OnDeviceBuild/include/OnDeviceBuild/Engine.h`
- Create: `OnDeviceBuild/src/Engine.cpp`
- Create: `OnDeviceBuild/src/BuildRunner.mm`
- Create: `OnDeviceBuild/include/OnDeviceBuild/ZipStore.h`
- Create: `OnDeviceBuild/src/ZipStore.cpp`
- Create: `OnDeviceBuild/host/main.cpp`
- Create: `OnDeviceBuild/tests/test_zipstore.cpp`
- Modify: `OnDeviceBuild/CMakeLists.txt`

**Interfaces:**
- Consumes: compile/link/bundle/sign APIs
- Produces: `ondevice::buildSignedIpa`, `ondevice::ZipStore`, `ondevice-build-host`

- [ ] **Step 1: ZipStore**

Copy `PocLinkIOS/src/ZipStore.{h,cpp}`. Sentinels for SDK: `usr/lib/libSystem.tbd` and a path that contains `UIKit.framework`. Stamp = size + mtime of the zip, stored next to the extract root. `isExtracted()` is false if stamp mismatches.

Test: create a tiny zip, extract, assert sentinels, second `ensureExtracted` does not rewrite (compare mtime of a sentinel), then rewrite the zip with different size and assert re-extract.

- [ ] **Step 2: BuildRunner**

Port `PocJuceIOS/src/BuildRunner.mm` to call `ondevice::compileToObject`. Parse manifest JSON. Prefer JUCE `var`/`JSON::parse` **only if** this library already links juce; it must not. Use a small JSON reader: nlohmann is not in the repo — parse with `NSJSONSerialization` in the `.mm` (Foundation is on Mac and iOS). C sources: no `-std=gnu++17`.

Progress callback on a serial queue. Blocks capture shared state by pointer.

Default threads in Engine if `threads <= 0`: `min(4, processorCount/2)` at least 1.

- [ ] **Step 3: Engine.cpp**

`buildSignedIpa`:

1. Parse manifest (`name`, `bundleId`, `sources`, `defines`, `includes`, `frameworks`, `libraries`, `minimumOSVersion`).
2. `compileManifest`.
3. `linkObjects` with builtins and frameworks from manifest.
4. If `!canRunAgain`, set `linkerCanRunAgain = false` and fail.
5. `writeAppBundle` using the linked binary.
6. If p12/provision non-empty: `signAppFolder` then `writeIpa`. If empty: skip sign, still write IPA (unsigned) so `--skip-sign` works.
7. Inspect the executable with `isIOSArm64Executable()` before declaring success.

- [ ] **Step 4: Host CLI**

Flags from spec §9. `--skip-sign` leaves p12 empty. Exit 1 if MachO check fails even when the pipeline returned success. Print compile seconds and peak RSS.

- [ ] **Step 5: Integration run**

Run the CLI on `~/Documents/src/PocJuceIOS/stage/manifest.json` with `--skip-sign` if the stage tree exists, `--root` that stage directory, sysroot/resource/builtins as in earlier tasks, `--threads 2`. Expect `isIOSArm64Executable`. If the full 31 TU build is too slow for a unit test, add `tests/fixtures/mini-manifest.json` with one `.mm` and run that in `ondevice-test-engine` (new test binary). Still **manually run** the CLI once on the JuceHello stage if present and paste the last 20 log lines in the report.

- [ ] **Step 6: Commit**

```
git add OnDeviceBuild
git commit -m "feat: on-device build engine and host CLI"
```

---

### Task 6: OnDeviceExporter

**Files:**
- Create: `Projucer/Source/ProjectSaving/jucer_ProjectExport_OnDevice.h`
- Modify: `Projucer/Source/ProjectSaving/jucer_ProjectExporter.cpp` (register type + `createExporterFromSettings`)
- Modify: `Projucer/Source/ProjectSaving/jucer_ProjectExporter.h` if a new virtual is required
- Modify: `Projucer/Source/Project/Modules/jucer_Modules.cpp` (`addLibsToExporter`: iOS frameworks when `isiOS()` even if not Xcode)
- Modify: `Projucer/CMakeLists.txt` (header is include-only; cpp already compiles exporter.cpp)
- Modify: `Projucer/Projucer.jucer` (add the new header to the file list, compile=0)
- Create: `OnDeviceBuild/tests/test_manifest_language.cpp` only if you extract language picking; otherwise test via a small host helper in Projucer — **preferred:** keep language picking as a free function in the exporter header:

```cpp
static String onDeviceLanguageForFile (const File& f, bool compileAsObjC);
```

`.c` → `c`; `.mm` or compileAsObjC → `objective-c++`; else `c++`.

**Interfaces:**
- Consumes: existing exporter source/module enumeration
- Produces: `ONDEVICE_IOS` exporter writing `manifest.json`

- [ ] **Step 1: Exporter class**

Follow `MakefileProjectExporter` shape (small) not Xcode. Identifier `ONDEVICE_IOS`. `isiOS() true`, `isXcode() false`, `usesMMFiles() true`. `getNewLineString` `\n`. `supportsTargetType`: GUIApp and ConsoleApp only for this slice. `canLaunchProject` / `launchProject`: `#if JUCE_IOS` true / call controller; else false.

`create()`: after modules have been applied (same time other exporters write), collect:

- project compile files from groups (`shouldBeCompiled`)
- module compile units already added as exporter compile files if that is how others see them — iterate `getAllGroups()` / the exporter's list of files to compile the same way Android's `addCompileUnits` walks items. If a compiled file is `.mm` or the module unit `isCompiledForObjC`, language `objective-c++`.

Defines: exporter + project + `JUCE_MODULE_AVAILABLE_*` already on the exporter after `addDefinesToExporter`. Dump the preprocessor def map used for iOS.

Includes: module paths + generated `JuceLibraryCode` relative to project folder.

Frameworks: after the Modules.cpp change, read whatever list the exporter stores. Give `OnDeviceExporter` a `StringArray iosFrameworks` analogous to Xcode's `xcodeFrameworks`. `addLibsToExporter` should fill it when `isiOS()`:

Change the Xcode-only branch so iOS frameworks/libs are also applied when `exporter.isiOS()` by dynamic_cast to `OnDeviceExporter` **or** better: add `virtual StringArray* getiOSFrameworksList()` default nullptr, Xcode and OnDevice return their member. Avoid a second `isXcode` clone. Simplest allowed implementation: in `addLibsToExporter`, `if (exporter.isiOS())` add iOSFrameworks tokens to a new virtual `void addIosFrameworks (const StringArray&)`. Implement on Xcode (existing field) and OnDevice.

Always include `UIKit` and `Foundation` if missing.

Write JSON to `getTargetFolder().getChildFile("manifest.json")`. Pretty-print optional.

- [ ] **Step 2: Registration**

`getExporterTypeInfos()`: add On-Device (reuse `export_xcode_svg` icon). `tryCreatingExporter` tag list: add `OnDeviceProjectExporter`. `getCurrentPlatformExporterTypeInfos` on `JUCE_IOS`: On-Device first, then Xcode iOS.

`canProjectBeLaunched`: on `JUCE_IOS` include `ONDEVICE_IOS`.

- [ ] **Step 3: Language helper test**

A tiny test does not need the whole Projucer. Put `onDeviceLanguageForFile` as an inline in a small `OnDeviceBuild/include`? **No** — keep it next to the exporter. Test by compiling a 20-line `tests/test_language.cpp` **in OnDeviceBuild** that duplicates the three-way mapping in one function `ondevice::languageForSource(path, compileAsObjC)` used by **both** BuildRunner (already gets language from JSON) and documented for the exporter to call. Implement `languageForSource` in OnDeviceBuild public header as inline:

```cpp
inline const char* languageForSource (const std::string& path, bool compileAsObjC) {
  auto ends = [&](const char* ext) {
    return path.size() >= strlen(ext) && path.compare(path.size()-strlen(ext), std::string::npos, ext)==0;
  };
  if (ends(".c")) return "c";
  if (compileAsObjC || ends(".mm") || ends(".m")) return "objective-c++";
  return "c++";
}
```

Exporter must call the same rules (reimplement with JUCE `File` is OK if tests cover the CMake function and a comment says keep in sync — **prefer** the exporter to emit using those exact rules in a shared comment plus a unit test file `OnDeviceBuild/tests/test_language.cpp`).

- [ ] **Step 4: Header button copy on iOS**

In `HeaderComponent::updateExporterButton`, if selected exporter identifier is `ONDEVICE_IOS`, set button tooltip/name to `Build & Install`. Do not add a second button.

- [ ] **Step 5: Build Mac Projucer (or at least compile the exporter TU)**

`Projucer/CMakeLists.txt` does not list the new header (included from cpp). Ensure `jucer_ProjectExporter.cpp` still compiles. Configure the existing Projucer cmake build if one exists under `Projucer/Builds` or document the command you ran. Do not link OnDeviceBuild into macOS Projucer.

- [ ] **Step 6: Commit**

```
git add Projucer OnDeviceBuild/tests/test_language.cpp OnDeviceBuild/include/OnDeviceBuild/Language.h
git commit -m "feat: On-Device exporter writes compile manifest"
```

---

### Task 7: iPad BuildController, progress, installer

**Files:**
- Create: `Projucer/Source/OnDevice/jucer_OnDeviceBuildController.h`
- Create: `Projucer/Source/OnDevice/jucer_OnDeviceBuildController.mm`
- Create: `Projucer/Source/OnDevice/jucer_LoopbackServer.h`
- Create: `Projucer/Source/OnDevice/jucer_LoopbackServer.m`
- Modify: `Projucer/Source/ProjectSaving/jucer_ProjectExport_OnDevice.h` `launchProject`
- Modify: `Projucer/Projucer.jucer` `XCODE_IPHONE` linker flags + files + resource copy
- Modify: `Projucer/CMakeLists.txt` so iOS builds link `OnDeviceBuild` when `CMAKE_SYSTEM_NAME` is iOS
- Copy assets: clang-resource, libclang_rt.ios.a, backloop.p12 into a documented bundle location (CMake POST_BUILD like PocClangIOS)

**Interfaces:**
- Consumes: `ondevice::buildSignedIpa`, ZipStore, LoopbackServer from PoC
- Produces: iOS UI flow in spec §4

- [ ] **Step 1: LoopbackServer**

Copy `PocJuceIOS/app/LoopbackServer.{h,m}` almost verbatim. Bundle `backloop.p12` from `PocFullChain/assets`. IPA zip served at `/payload.ipa`, plist at `/manifest.plist`. itms-services URL uses `https://<uuid>.backloop.dev:<port>/manifest.plist`.

ipa を `Payload/` 親から作るのは Engine 側の責務。ここでは成果物パスを配信するだけ。

- [ ] **Step 2: BuildController**

Singleton or component-owned object. `start(ProjectExporter&)`:

1. `idleTimerDisabled = YES`
2. Show a modal-ish panel (AlertWindow or child component on the project window) with a log `TextEditor` (read-only) and RSS/time labels
3. Register background notification → append one log line
4. Resolve Documents, ZipStore SDK, signing files, resource dir from `[NSBundle mainBundle]`
5. Read `manifest.json` from exporter target folder (save already ran)
6. Background thread: `buildSignedIpa` with progress lines marshalled to message thread
7. On success: start LoopbackServer, `openURL` itms-services
8. Always restore idle timer

Missing assets: do not start clang; log which files are missing.

`launchProject` on the exporter calls this. Return true if the controller accepted the job.

- [ ] **Step 3: Wire Xcode iOS project**

`XCODE_IPHONE` in `Projucer.jucer`:

- add the new `.mm`/`.m`/`.h` files (`compile=1` for mm/m)
- `extraLinkerFlags` / library path to `OnDeviceBuild` iOS archive **and** `llvm-ios.a` if the static lib is not relocatable-complete

CMake path (primary for a reproducible iOS lib):

```
cmake -S OnDeviceBuild -B OnDeviceBuild/build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DONDEVICE_LLVM_IOS_ARCHIVE=$HOME/Documents/src/PocClangIOS/build-ios/llvm-ios.a \
  -DONDEVICE_OPENSSL_IOS=$HOME/Documents/src/PocSignIOS/openssl-ios
```

Produce `libOnDeviceBuild.a`. Point the jucer iOS exporter at it. If the `.jucer` cannot express all LLVM include dirs because engine TUs are not in Projucer, **do not add Compiler.cpp to the jucer**.

POST_BUILD on iOS Projucer: copy clang resource includes and `libclang_rt.ios.a` into the app bundle `clang-resource/` with layout `<dir>/include` and `<dir>/lib/darwin/`.

- [ ] **Step 4: Host verification still passes**

Re-run Task 2/3/5 tests. They must still pass.

- [ ] **Step 5: Commit**

```
git add Projucer OnDeviceBuild
git commit -m "feat: iPad build-and-install controller and loopback installer"
```

Human-only leftover (report, do not block commit): installd tap + app launch on device.

---

## Self-review

Spec coverage:

| Spec section | Task |
|---|---|
| §1 CX button / progress / idle / itms | 7 |
| §3 library split, no LLVM in macOS Projucer | 1–5, 6 |
| §5 APIs | 1–5 |
| §6 manifest exporter | 6 |
| §7 cc1 flags | 2 (copy PoC) |
| §8 bundle/sign/ipa | 4 |
| §9 host CLI | 5 |
| §11 host tests | 1–6 |
| PKCS12 reencode | 4 |
| ZipStore stamps | 5 |

Type names: `ondevice::compileToObject`, `linkObjects`, `buildSignedIpa`, `inspectMachO` used consistently.
