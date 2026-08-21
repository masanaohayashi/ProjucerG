# オンデバイスビルド 設計文書

作成日: 2026-08-21

根拠: `docs/ondevice-build-integration.md`（技術検証済み、実装未着手）。

## 1. 目的

iPad 上の Projucer で、開いている `.jucer` プロジェクトを **コンパイルし、
リンクし、署名し、この実機へインストールする**。Mac は関与しない。

成功条件: ユーザーがヘッダのボタンを押し、インストールをタップしたあと、
生成アプリが起動する。`installd` が黙って拒否しないこと、AMFI が起動時に
通ることまで含む。ファイルができただけでは成功ではない。

### CX 要件（必須）

1. On-Device exporter を選んだ状態で、ヘッダボタンが「ビルドしてインストール」になる。
2. 押すと進捗（どの TU が終わったか、経過時間、ピーク RSS）が見える。
3. ビルド中はアイドルタイマーを止める。背面に入ったらユーザーに知らせる。
4. インストールは `itms-services` 経由。タップが必要であることを隠さない。

## 2. 対象と非対象

**対象（「動く」）**

- ホストと実機で同一のエンジンコード
- Mac 上の CLI ハーネス（開発・自動テスト用。Mac Projucer の UI は触らない）
- `OnDeviceExporter` が `manifest.json` を書く
- コンパイル / リンク / `.app` / 署名 / `.ipa`
- iOS SDK zip の取り込みと展開
- 証明書・profile のファイル持ち込み（Documents）
- PKCS#12 の AES 再エンコード（import 時、ホストでも実機でも）
- 前面維持、進捗、バックグラウンド検出
- GUI Application ターゲット（JUCE モジュールの unity TU）

**対象外（「使える」以降）**

- 増分ビルド
- 診断のソース行ジャンプ
- Keychain 保管、Developer Services からの証明書取得
- キャンセル UI の本実装（並列度は既定値 `min(4, コア数/2)` で固定してよい。表示はする）
- プラグインターゲット（AUv3 等）
- デバッガ
- Mac Projucer のビルドボタン
- LLVM のこのリポジトリ内でのゼロからのクロスビルド（既存 `PocClangIOS` 成果物を使う）

## 3. 全体構造

```
OnDeviceBuild/                  独立 CMake。公開ヘッダに LLVM / zsign 型を出さない
  include/OnDeviceBuild/
  src/                          PoC から移植。-fno-rtti
  third_party/zsign/            MIT。zsign.cpp と iowin32.c はビルドしない
  host/ondevice-build-host      Mac CLI
  tests/

Projucer/
  OnDeviceExporter              既存 exporter の兄弟。LLVM 非依存
  iOS のみ:
    BuildController             ボタン → エンジン → 進捗 UI
    Installer / LoopbackServer  itms-services
```

macOS 用 Projucer **アプリ** に LLVM をリンクしない。
iOS 用 Projucer は `libOnDeviceBuild.a` をリンクする。エンジンの TU は
Projucer.jucer のコンパイル対象に入れない（JUCE は RTTI、LLVM は -fno-rtti）。

iPad の既存ビルド経路は `Projucer.jucer` の `XCODE_IPHONE` である。
`libOnDeviceBuild.a` をその extra linker flags / library search path で拾う。

## 4. ユーザーフロー（iPad）

1. プロジェクトを開く。exporter に **On-Device** を追加・選択する。
2. ヘッダボタン（既存の Save and Open in IDE）のラベルを
   **Build & Install** にする。`canLaunchProject()` は iOS で true。
3. 保存（既存 `saveAndOpenInIDE`）→ exporter が `manifest.json` を書く。
4. `BuildController` が資材を確認する。

   | 資材 | 場所 | 完了判定 |
   |---|---|---|
   | iOS SDK zip | Documents の `iPhoneOS.sdk.zip` | 展開先に `usr/lib/libSystem.tbd` と `UIKit.framework` がある。zip の size+mtime スタンプが一致 |
   | clang resource | アプリバンドル `clang-resource/`（`<dir>/include` が検索される） | `stddef.h` |
   | compiler-rt | バンドル `clang-resource/lib/darwin/libclang_rt.ios.a` | ファイル存在 |
   | 証明書 | Documents/OnDeviceSigning/*.p12 | import 時に AES-256-CBC へ再エンコードして隣に `*.modern.p12` を書く |
   | profile | Documents/OnDeviceSigning/*.mobileprovision | 存在 |

   足りなければ進捗パネルに不足を書いてビルドしない。

5. エンジンがコンパイル → リンク → BundleBuilder → 署名 → IPA。
6. Loopback HTTPS（`*.backloop.dev` + バンドルした `backloop.p12`）で
   `/manifest.plist` と `/payload.ipa` を出し、`itms-services://` を `openURL`。
7. ユーザーがインストールをタップする。

Xcode exporter を選んでいるときは今どおりボタン無効（この端末に Xcode はない）。

## 5. 公開 API（エンジン）

LLVM / lld / zsign の型は `.cpp` の中だけ。

```cpp
namespace ondevice {

struct CompileRequest {
    std::string sourcePath;
    std::string outputPath;
    std::string triple = "arm64-apple-ios17.0";
    std::string resourceDir;
    std::string sysroot;
    std::string minimumOSVersion = "17.0";
    std::vector<std::string> extraArgs;
};

struct CompileResult {
    bool success = false;
    std::string diagnostics;
    unsigned long long outputBytes = 0;
};

CompileResult compileToObject (const CompileRequest&);
unsigned long long getResidentMemoryBytes();

struct LinkRequest {
    std::vector<std::string> objectFiles;
    std::string outputPath;
    std::string sysroot;
    std::string architecture = "arm64";
    std::string minimumOSVersion = "17.0";
    std::string sdkVersion = "17.0";
    std::vector<std::string> libraries { "System", "c++" };
    std::vector<std::string> frameworks;
    std::string builtinsArchive;
    std::vector<std::string> extraArgs;
};

struct LinkResult {
    bool success = false;
    bool canRunAgain = true;
    std::string diagnostics;
    unsigned long long outputBytes = 0;
};

LinkResult linkObjects (const LinkRequest&);

struct SignRequest {
    std::string appFolder;
    std::string p12Path;
    std::string password;
    std::string provisionPath;
    std::string bundleId;
};

struct SignResult { bool success = false; std::string log; };

SignResult signAppFolder (const SignRequest&);
bool extractZip (const std::string& zipFile, const std::string& outputFolder);
bool archiveFolder (const std::string& folder, const std::string& zipFile);

// PKCS#12 を AES-256-CBC に書き直す。すでに現代的ならコピーでよい。
bool reencodePkcs12Aes (const std::string& inPath,
                        const std::string& outPath,
                        const std::string& password,
                        std::string& error);

struct MachOInfo {
    bool parsed = false;
    bool is64Bit = false;
    bool isArm64 = false;
    bool isObjectFile = false;
    bool isExecutable = false;
    bool hasBuildVersion = false;
    unsigned platform = 0;          // PLATFORM_IOS == 2
    std::string platformName;
    std::string minOSVersion;
    unsigned long long fileSize = 0;
    std::string error;
    bool isIOSArm64Object() const;
    bool isIOSArm64Executable() const;
    std::string describe() const;
};

MachOInfo inspectMachO (const std::string& path);

struct EngineRequest {
    std::string projectRoot;
    std::string manifestJson;       // 中身。パスではない
    std::string workDirectory;
    std::string sysroot;
    std::string resourceDir;
    std::string builtinsArchive;
    std::string p12Path;
    std::string p12Password;
    std::string provisionPath;
    int threads = 1;
    std::function<void (const std::string& line)> onProgress;
};

struct EngineResult {
    bool success = false;
    std::string appFolder;
    std::string ipaPath;
    std::string failureMessage;
    double compileSeconds = 0;
    unsigned long long peakResidentBytes = 0;
    bool linkerCanRunAgain = true;
};

EngineResult buildSignedIpa (const EngineRequest&);

} // namespace ondevice
```

`canRunAgain == false` のあとは以降のビルドを拒否する。

Installer は ObjC で、iOS だけコンパイルする。公開は
`LoopbackServer` と `openInstallURL` に留める。

## 6. マニフェスト

exporter が `Builds/OnDevice/manifest.json` に書く。PoC と同じ形:

```json
{
  "name": "JuceHello",
  "bundleId": "tokyo.studio-r.jucehello",
  "minimumOSVersion": "17.0",
  "defines":  ["JUCE_MODULE_AVAILABLE_juce_core=1"],
  "includes": ["JuceLibraryCode", ".../modules"],
  "sources":  [{"file": "Source/Main.cpp", "language": "c++"}],
  "frameworks": ["UIKit", "Foundation"],
  "libraries":  ["System", "c++"]
}
```

言語は `c` / `c++` / `objective-c++`。C ソースに `-std=gnu++17` を渡さない。

ソース一覧・defines・includes・frameworks は既存の
`LibraryModule::addSettingsForModuleToExporter` とプロジェクトファイル列挙が
既に計算している。`addLibsToExporter` はいま `isXcode()` のときだけ
iOS frameworks を足すので、`isiOS()` なら Xcode でなくても足す。

`OnDeviceExporter`:
- identifier `ONDEVICE_IOS`
- display name `On-Device`
- target folder `OnDevice`
- `isiOS() == true`, `isXcode() == false`
- `usesMMFiles() == true`
- `canLaunchProject()` は `#if JUCE_IOS`
- `create()` が manifest を書く
- `launchProject()` は iOS で `BuildController` を起動。それ以外は false

## 7. コンパイラ引数

`-cc1` はドライバの仕事をしない。欠落は原因を指さないエラーになる。
確定構成は PoC `PocClang.cpp` を **コピー** する（再発明しない）:

- `-triple arm64-apple-ios17.0` `-emit-obj` `-resource-dir` `-isysroot`
- 検索順: libc++ v1 → `/usr/local/include` → resource-dir/include → `/usr/include` → iframework ×3
- `-target-cpu apple-a7` `-target-abi darwinpcs` と NEON 他の `-target-feature`
- `-fgnuc-version=4.2.1` `-fdefine-target-os-macros` `-fobjc-runtime=ios-17.0`
- `-fobjc-exceptions -fblocks -fexceptions -fcxx-exceptions`

リンク: `-platform_version ios <min> <sdk>` 必須。無いと iOS バイナリにならない。
`-syslibroot`、`-lSystem -lc++`、`-framework`、`libclang_rt.ios.a` の絶対パス。

## 8. Bundle / 署名 / IPA

Info.plist 最低キーは技術資料 4.4。実行ファイルは `0755`。

署名は zsign。ログは `dup2` で `$TMPDIR` に取る。`.app` の隣に置かない。

IPA: ルートが `Payload/`。`archiveFolder` はフォルダの中身を書くので
`Payload` の **親** を渡す。出力 zip は圧縮対象の外。

PKCS#12 は import 時に必ず `reencodePkcs12Aes` する。ホストの Homebrew
OpenSSL には legacy provider があるので、再エンコードのテストは
「RC2 の p12 を渡して AES の p12 が出る」をホストで行う。実機での RC2
直読み失敗は人間確認。

## 9. ホストハーネス

`ondevice-build-host`:

```
ondevice-build-host \
  --manifest <json> \
  --root <projectRoot> \
  --sysroot <sdk> \
  --resource-dir <clang-resource> \
  --builtins <libclang_rt.ios.a> \
  --p12 <modern.p12> --password <pw> --provision <mobileprovision> \
  --work <dir> --threads N
```

終了コード 0 は **実行ファイルが `isIOSArm64Executable()`** かつ
IPA の zip 一覧に `Payload/<Name>.app/<exec>` があるときだけ。
`--skip-sign` でコンパイル+リンク+bundle まで（CI で証明書が無いとき）。

同じ判定関数をホストと実機で共有する。

## 10. 進捗とライフサイクル

- `UIApplication.idleTimerDisabled = YES` をビルド開始で立て、終了で戻す
- `UIApplicationDidEnterBackgroundNotification` を進捗に 1 行出す
- 進捗コールバックは直列キュー。TU 完了ごとに 1 行
- 既定並列度 `min(4, cores/2)`。ピーク RSS を併記する

## 11. テスト（エージェントがホストで回す）

| テスト | 判定 |
|---|---|
| MachO 検査 | 既知の iOS object / 実行ファイル fixture。platform == 2 |
| 1 ファイル compile | `UIKit/UIKit.h` を include する `.mm` → `isIOSArm64Object()` |
| link | 上記 + compiler-rt → `isIOSArm64Executable()` |
| IPA 構造 | `unzip -Zl` 相当でルートが `Payload/` |
| ZipStore スタンプ | 同じ zip は再展開しない。mtime/size が変わったら再展開 |
| PKCS#12 再エンコード | RC2 p12 → AES p12 が OpenSSL 3（legacy 無し想定の読み）で開ける |
| マニフェスト | 小さな `.jucer`（または手製プロジェクトツリー）から JSON にソース・language・defines が含まれる。C ファイルの language が `c` |
| host CLI | `--skip-sign` で JUCE 最小構成、または PoC の `PocJuceIOS/stage/manifest.json` が通る |

実機のインストールと起動だけ人間。

## 12. ライセンス

取り込むもの: LLVM Apache-2.0 WITH LLVM-exception、OpenSSL Apache-2.0、
zsign MIT。GPL は入れない。

## 13. 参照実装（コピー元。書き直さない）

| コンポーネント | パス |
|---|---|
| Compiler | `~/Documents/src/PocClangIOS/src/PocClang.{h,cpp}` |
| MachOCheck | `~/Documents/src/PocClangIOS/src/MachOCheck.{h,cpp}` |
| Linker | `~/Documents/src/PocLinkIOS/src/PocLink.{h,cpp}` |
| ZipStore | `~/Documents/src/PocLinkIOS/src/ZipStore.{h,cpp}` |
| Signer | `~/Documents/src/PocSignIOS/src/PocSign.{h,cpp}` |
| zsign | `~/Documents/src/PocSignIOS/zsign/src`（`zsign.cpp` / `iowin32.c` 除外） |
| BuildRunner | `~/Documents/src/PocJuceIOS/src/BuildRunner.{h,mm}` |
| LoopbackServer | `~/Documents/src/PocJuceIOS/app/LoopbackServer.{h,m}` |
| LLVM host | `~/Documents/src/PocClangIOS/build-host` |
| LLVM iOS archive | `~/Documents/src/PocClangIOS/build-ios/llvm-ios.a` |
| compiler-rt | `~/Documents/src/PocClangIOS/build-rt-ios/lib/darwin/libclang_rt.ios.a` |
| OpenSSL iOS | `~/Documents/src/PocSignIOS/openssl-ios` |
| 署名 fixture | `~/Documents/src/PocFullChain/assets/` |

CMake の default でこれらの絶対パスを見る。リポジトリには LLVM を vendoring しない。
