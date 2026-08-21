# ProjucerG

ProjucerG is an unofficial fork of the JUCE Projucer focused on restoring the
legacy GUI Editor that was available in JUCE 7 and deprecated/removed from the
JUCE 8 Projucer.

This project is not an official JUCE project and is not endorsed by Raw Material
Software Limited.

## Current Status

The `master` branch currently contains the JUCE 9.0.0 based version.

Implemented changes include:

- Restored the legacy GUI Editor source files from JUCE 7.
- Reconnected the GUI Editor document type and menus in the JUCE 8 Projucer.
- Added the GUI Editor enable/disable preference.
- Added "New GUI Component" support.
- Updated GUI Editor font code to use JUCE 8 `FontOptions`.
- Fixed a legacy GUI Editor resizing bug where right-edge and bottom-right
  resizing could snap back immediately.
- Verified macOS Debug builds and Projucer `--resave` stability.
- Added iPad device and iOS Simulator build/install support.

## Branches

Planned branch structure:

- `master`: current JUCE 9.0.0 based branch.
- `juce8`: JUCE 8 based maintenance branch, if split from `master`.
- `juce9`: JUCE 9 based branch, if split from `master`.

## Repository Layout

- `Projucer/`: the modified Projucer source tree.
- `design.md`: Japanese design and implementation notes.
- `JUCE7_JUCE8_modules_diff.md`: Japanese notes on JUCE module differences.
- `JUCE7_JUCE8_Projucer_diff.md`: Japanese notes on Projucer differences.

The local JUCE checkout directories used during development are intentionally
not tracked by git.

## Custom LookAndFeel

In addition to the built-in `LookAndFeel_V1`-`V4` choices, ProjucerG can offer
header-only LookAndFeel classes that are bundled with Projucer itself, selectable
as a "Custom LookAndFeel". Whichever one is selected gets copied into
`JuceLibraryCode/` automatically every time the project is saved/exported, so the
generated project builds standalone with no extra setup.

### Using a Custom LookAndFeel in a project

1. Open the project in Projucer and pick a class (e.g. `IfwTabbedLookAndFeel`) from
   the **"Custom LookAndFeel"** dropdown in Project Settings. Leaving it as
   `<None>` changes nothing.
2. Once selected, that class is added to the choices for the project-wide
   **"Default LookAndFeel"** setting and for each component's **"LookAndFeel"**
   property in the GUI Editor. Use it like any other LookAndFeel choice from there.
3. On save, `JuceLibraryCode/<ClassName>.h` is generated automatically and the
   generated code `#include`s it. No manual file copying or search path setup is
   required.

### Adding a new Custom LookAndFeel

Custom LookAndFeels are driven by a small registry baked into Projucer itself as
BinaryData. Adding a new one requires editing this repository's Projucer source:

1. Put the new header-only LookAndFeel class (e.g. `MyLookAndFeel.h`) in
   `Projucer/Source/BinaryData/Templates/`.
2. Add a FILE entry for it to the `BinaryData > Templates` group in
   `Projucer/Projucer.jucer`, matching the existing `IfwTabbedLookAndFeel.h` entry
   (`compile="0" resource="1"`).
3. Add an entry to the registry array in
   `Projucer/Source/Project/jucer_CustomLookAndFeels.h`:

   ```cpp
   CustomLookAndFeelInfo { "MyLookAndFeel", "MyLookAndFeel.h", "MyLookAndFeel_h" }
   ```

   `binaryDataResourceName` follows Projucer's BinaryData naming convention: the
   filename with its `.` replaced by `_`.
4. In both `Projucer/Source/ComponentEditor/jucer_JucerDocument.cpp` and
   `Projucer/Source/ComponentEditor/Components/jucer_ComponentTypeHandler.cpp`,
   add an `#include` for the new header next to the existing
   `IfwTabbedLookAndFeel.h` include, and add one line to each file's
   `createCustomLookAndFeel()`:

   ```cpp
   if (type == "MyLookAndFeel") return std::make_unique<MyLookAndFeel>();
   ```

   (This is the spot that constructs the real C++ type for the GUI Editor's live
   preview, so it can't be fully data-driven from the registry alone — one line per
   class is still needed.)
5. Rebuild Projucer, then use that freshly built Projucer to resave
   `Projucer.jucer` so `JuceLibraryCode/BinaryData.cpp/.h` picks up the new
   resource:

   ```sh
   xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
     -scheme "Projucer - App" -configuration Debug -quiet build

   Projucer/Builds/MacOSX/build/Debug/Projucer.app/Contents/MacOS/Projucer \
     --resave Projucer/Projucer.jucer
   ```

From then on, `MyLookAndFeel` shows up in the "Custom LookAndFeel" dropdown for any
project, and selecting and saving it copies the header automatically as described
above.

## Build Requirements

This branch expects a JUCE 9.0.0 checkout next to this repository. The current
development setup used:

```text
../JUCE-9.0.0
```

The Projucer project file is:

```text
Projucer/Projucer.jucer
```

The `.jucer` file is the source of truth for generated project files. After
editing it, regenerate exporters with:

```sh
Projucer/Builds/MacOSX/build/Debug/Projucer.app/Contents/MacOS/Projucer --resave Projucer/Projucer.jucer
```

On macOS, the Debug app can be built with:

```sh
xcodebuild -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
  -scheme "Projucer - App" \
  -configuration Debug \
  -derivedDataPath Projucer/Builds/MacOSX/DerivedData \
  -quiet build
```

## Running on iPad / iOS

### Requirements

- macOS and Xcode with an iOS SDK that can build for iPadOS 17.0 or later. The
  deployment target of this project is iOS 17.0.
- A JUCE 9.0.0 checkout at `../JUCE-9.0.0` relative to this repository.
- iOS LLVM/Clang and OpenSSL build artifacts at the paths referenced by the
  generated Xcode project:

```text
~/Documents/src/PocClangIOS/build-ios/llvm-ios.a
~/Documents/src/PocClangIOS/build-ios-simulator/llvm-ios-simulator.a
~/Documents/src/PocClangIOS/build-rt-ios/lib/darwin/libclang_rt.ios.a
~/Documents/src/PocClangIOS/build-rt-ios/lib/darwin/libclang_rt.iossim.a
~/Documents/src/PocSignIOS/openssl-ios/lib/libssl.a
~/Documents/src/PocSignIOS/openssl-ios/lib/libcrypto.a
~/Documents/src/PocSignIOS/openssl-ios-simulator/lib/libssl.a
~/Documents/src/PocSignIOS/openssl-ios-simulator/lib/libcrypto.a
```

If these dependencies are stored elsewhere, update the Xcode flags in the
`.jucer` file and the cache values in `OnDeviceBuild/CMakeLists.txt`.

Build the iOS static libraries after preparing the SDK and dependencies:

```sh
# Physical iPad
cmake -S OnDeviceBuild -B OnDeviceBuild/build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build OnDeviceBuild/build-ios --config Debug

# iOS Simulator (Apple Silicon Mac)
cmake -S OnDeviceBuild -B OnDeviceBuild/build-ios-simulator -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DONDEVICE_IOS_SIMULATOR=ON
cmake --build OnDeviceBuild/build-ios-simulator --config Debug
```

### Files to place on the iPad

Launch Projucer on the iPad once. The **On My iPad > Projucer** folder in the
Files app corresponds to the app's `Documents` directory. Copy the following
files there:

```text
iPhoneOS.sdk.zip
OnDeviceSigning/
  development.p12
  development.password         # or password.txt
  development.mobileprovision
```

- The device SDK archive must be named `iPhoneOS.sdk.zip`. It should contain the
  contents of an Xcode `iPhoneOS*.sdk` directory.
- `OnDeviceSigning/` must contain a `.p12` (or `.modern.p12`), its password file,
  and a `.mobileprovision`. For `development.p12`, use either
  `development.password` or the shared `password.txt`.
- The provisioning profile must be a development profile containing the target
  iPad UDID and the app's bundle identifier. Use your own Apple Developer Team
  ID and provisioning profile, and update `iosDevelopmentTeamID` in
  `Projucer/Projucer.jucer` accordingly.
- Do not add SDK archives or signing files to the repository. They may contain
  private or sensitive material and should remain in the app's `Documents`
  directory.

### Simulator SDK and one-click installation

Create the Simulator SDK archive with:

```sh
./scripts/create_ios_simulator_sdk_zip.sh
```

The default output is
`OnDeviceBuild/build-artifacts/iPhoneSimulator.sdk.zip`. Copy that archive to
the Projucer app's `Documents` directory in the Simulator. Signing files are
not required for Simulator builds.

Build and launch `Projucer - App` from
`Projucer/Builds/iOS/Projucer.xcodeproj` once with the `iphonesimulator` SDK.
The post-build step starts `scripts/simulator_install_bridge.py` on the host
Mac. The bridge listens on `127.0.0.1:38472` and invokes `xcrun simctl`, so the
Simulator and Xcode must be running on that Mac. If it is not started
automatically, run this from the repository root:

```sh
/usr/bin/python3 scripts/simulator_install_bridge.py
```

### Build and install

To build the Xcode project directly:

```sh
# Physical iPad (signing required)
xcodebuild -project Projucer/Builds/iOS/Projucer.xcodeproj \
  -target 'Projucer - App' -configuration Debug -sdk iphoneos \
  -destination 'generic/platform=iOS' build

# Simulator (signing is disabled)
xcodebuild -project Projucer/Builds/iOS/Projucer.xcodeproj \
  -target 'Projucer - App' -configuration Debug -sdk iphonesimulator \
  CODE_SIGNING_ALLOWED=NO build
```

Launch Projucer, open a `.jucer` project, select the iOS exporter, and press
**Build & Install** to save, compile, link, and install in one operation.

- On a physical iPad, approve the installation prompt on the iPad after signing.
- On the Simulator, the host bridge runs `simctl install` and `simctl launch`
  after the build. The Simulator app cannot invoke `simctl` directly, so the
  bridge must be running.
- The current bundle identifier is `tokyo.studio-r.juce.theprojucer`. If this
  is distributed as a different app, keep the `.jucer`, provisioning profile,
  and entitlements in sync.

### Troubleshooting

- If `iPhoneOS.sdk.zip` or `iPhoneSimulator.sdk.zip` is missing, check the exact
  filename and the Projucer app's `Documents` directory on the iPad/Simulator.
- For `info-output-file registered more than once`, make sure an old Projucer
  instance is not running and rebuild the iOS app from the latest source.
- For undefined symbols such as `UNUserNotificationCenter` or `UTType`,
  regenerate the latest Xcode project and rebuild so the weak framework
  settings are applied.
- For `Simulator install bridge unavailable`, check that the Python bridge is
  running on the Mac and that TCP port `38472` is not occupied by another
  process.

## iPad / iOSで動かすための準備

### 必要な環境

- macOS と Xcode（iPadOS 17.0 以降をビルドできる iOS SDK）。このプロジェクトの
  deployment target は iOS 17.0。
- リポジトリの隣に `../JUCE-9.0.0` を配置する。
- iOS 用の LLVM/Clang と OpenSSL のビルド成果物を、現在の Xcode プロジェクトが
  参照する以下の場所に配置する。別の場所を使う場合は `.jucer` の Xcode flags と
  `OnDeviceBuild/CMakeLists.txt` のキャッシュ値を変更する。

```text
~/Documents/src/PocClangIOS/build-ios/llvm-ios.a
~/Documents/src/PocClangIOS/build-ios-simulator/llvm-ios-simulator.a
~/Documents/src/PocClangIOS/build-rt-ios/lib/darwin/libclang_rt.ios.a
~/Documents/src/PocClangIOS/build-rt-ios/lib/darwin/libclang_rt.iossim.a
~/Documents/src/PocSignIOS/openssl-ios/lib/libssl.a
~/Documents/src/PocSignIOS/openssl-ios/lib/libcrypto.a
~/Documents/src/PocSignIOS/openssl-ios-simulator/lib/libssl.a
~/Documents/src/PocSignIOS/openssl-ios-simulator/lib/libcrypto.a
```

`OnDeviceBuild` の iOS 用静的ライブラリは、必要な SDK と上記の依存物を用意した
あとで次のように生成する。

```sh
# iPad 実機用
cmake -S OnDeviceBuild -B OnDeviceBuild/build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build OnDeviceBuild/build-ios --config Debug

# iOS Simulator 用（Apple Silicon Mac）
cmake -S OnDeviceBuild -B OnDeviceBuild/build-ios-simulator -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DONDEVICE_IOS_SIMULATOR=ON
cmake --build OnDeviceBuild/build-ios-simulator --config Debug
```

### iPad の Files に置くファイル

Projucer を iPad で一度起動すると、Files の **この iPad 内 > Projucer** が
アプリ内の `Documents` に対応する。次の構成でファイルをコピーする。

```text
iPhoneOS.sdk.zip
OnDeviceSigning/
  development.p12
  development.password         # または password.txt
  development.mobileprovision
```

- 実機用 SDK はファイル名を必ず `iPhoneOS.sdk.zip` にする。Xcode の
  `iPhoneOS*.sdk` を zip 化したものを使用する。
- `OnDeviceSigning/` には `.p12`（または `.modern.p12`）、対応するパスワード
  ファイル、`.mobileprovision` を置く。`development.p12` のパスワードは
  `development.password` または共通の `password.txt` に書く。
- `.mobileprovision` は対象 iPad の UDID とアプリの bundle identifier を含む開発用
  profile にする。Apple Developer の Team ID と provisioning profile は各自のものを
 使い、`Projucer/Projucer.jucer` の `iosDevelopmentTeamID` も自分の Team ID に変更する。
- `iPhoneOS.sdk.zip` と署名ファイルには秘密情報が含まれる場合があるため、リポジトリ
  には追加しない。アプリの `Documents` にだけコピーする。

### Simulator 用 SDK とワンクリック実行

Simulator 用 SDK は次のスクリプトで作成できる。

```sh
./scripts/create_ios_simulator_sdk_zip.sh
```

既定の出力先は `OnDeviceBuild/build-artifacts/iPhoneSimulator.sdk.zip`。生成した zip を
Simulator 上の Projucer の `Documents` に置く。Simulator では署名ファイルは不要。

Xcode で `Projucer/Builds/iOS/Projucer.xcodeproj` の `Projucer - App` を
`iphonesimulator` SDK で一度ビルドして起動すると、ホスト Mac 上で
`scripts/simulator_install_bridge.py` が自動起動する。このブリッジは
`127.0.0.1:38472` で `xcrun simctl` を呼び出すためのものなので、Simulator と
Xcode が動いている Mac 上で実行する。自動起動しない場合は、リポジトリのルートで
次を実行する。

```sh
/usr/bin/python3 scripts/simulator_install_bridge.py
```

### ビルドとインストール

Xcode プロジェクトを直接ビルドする場合は、次を使う。

```sh
# iPad 実機（署名が必要）
xcodebuild -project Projucer/Builds/iOS/Projucer.xcodeproj \
  -target 'Projucer - App' -configuration Debug -sdk iphoneos \
  -destination 'generic/platform=iOS' build

# Simulator（署名不要）
xcodebuild -project Projucer/Builds/iOS/Projucer.xcodeproj \
  -target 'Projucer - App' -configuration Debug -sdk iphonesimulator \
  CODE_SIGNING_ALLOWED=NO build
```

Projucer アプリを起動して `.jucer` を開き、iOS exporter を選んで
**Build & Install** を押せば、保存・コンパイル・リンク・インストールまでを一度に
実行できる。

- iPad 実機では署名後にインストール確認が表示される。iPad 側で許可する。
- Simulator ではビルド完了後、ホストブリッジが `simctl install` と `simctl launch`
  を実行する。Simulator アプリから `simctl` を直接呼ぶことはできないため、ブリッジが
  起動している必要がある。
- bundle identifier は現在 `tokyo.studio-r.juce.theprojucer`。別のアプリとして
  配布する場合は `.jucer`、provisioning profile、関連する entitlements の値を揃える。

### よくあるエラー

- `iPhoneOS.sdk.zip` / `iPhoneSimulator.sdk.zip` が見つからない場合は、ファイル名と
  iPad/Simulator 側の Projucer `Documents` の配置を確認する。
- `info-output-file registered more than once` が出る場合は、古い Projucer を起動して
  いないか確認し、最新ソースから iOS アプリを再ビルドする。
- `UNUserNotificationCenter` や `UTType` の undefined symbol は、最新の Xcode project
  を再生成・再ビルドして weak framework の設定を反映する。
- `Simulator install bridge unavailable` の場合は、Mac 上で Python ブリッジが起動して
  いるか、TCP ポート `38472` が別プロセスに占有されていないか確認する。

## License

ProjucerG contains code derived from the JUCE Framework and the JUCE Projucer.
JUCE is copyright Raw Material Software Limited and its contributors.

Unless you have a separate commercial JUCE licence from Raw Material Software
Limited that permits your intended use, the JUCE-derived code in this repository
is distributed under the GNU Affero General Public License version 3.

See [LICENSE.md](LICENSE.md) for details.
