# オンデバイスビルドの Projucer 統合 — 技術資料

更新日: 2026-08-20
状態: **技術検証完了。実装未着手。**

この文書は、実装担当者への引き継ぎを目的とする。

- 何が実証済みで、何が未証明か
- Projucer のどこに、どう接続するか
- 実装時に必ず踏む落とし穴（これが本文書の中核）
- 作業分解と、判断が必要な設計事項

検証に使った動作するコードは `~/Documents/src/Poc*` にある。各ディレクトリの
`RESULT.md` に数値・ビルド構成・そのGateが証明していないことが記録してある。
全体サマリは `~/Documents/src/GATES.md`。

---

## 1. 結論

**iPhone 15 Pro (iOS 26.6) 上で、JUCE 9 のアプリ（audio モジュール込み
31 translation unit）をコンパイルし、リンクし、署名し、インストールして
起動させることに成功した。** すべて 1 プロセス内、サブプロセスなし、
Mac の関与なし。

```
compiled 31 TUs on 3 thread(s) in 48.5 s, peak rss 1730 MB
linked in 278 ms  -> 11,944,936 bytes  arm64 MH_EXECUTE platform iOS
signed            -> packaged 12,158,984 bytes
installed, launched
```

構成: `juce_core` / `events` / `data_structures` / `graphics` / `gui_basics` /
`gui_extra` / `audio_basics` / `audio_devices` / `audio_formats` /
`audio_processors` / `audio_utils` / `dsp`。`AudioDeviceManager` を実際に
初期化しており、デッドストリップされていない。

Mac の役割は、検証アプリ本体と 2 つの zip（iOS SDK / JUCE プロジェクト）を
最初に端末へ置いたことのみ。

### 性能

| | iPhone 15 Pro | M2 Max (シングルスレッド) |
|---|---|---|
| JUCE 19 TU コンパイル（逐次） | 81.0 s | 74.2 s |
| JUCE 19 TU コンパイル（3並列） | **36.1 s** | — |
| リンク | 0.22 s | 0.23 s |
| 署名 | 0.015 s | — |
| ピーク RSS（逐次 / 3並列） | 949 MB / **1691 MB** | 1000 MB |

端末は M2 Max のシングルコアと**ほぼ同等**（74.2 s 対 81.0 s）。

### 並列度の選択 ― 時間とメモリのトレードオフ

M2 Max 実測（19 TU）:

| threads | 時間 | ピーク RSS |
|---|---|---|
| 1 | 74.2 s | 1000 MB |
| 2 | 37.7 s | 1497 MB |
| 4 | 24.0 s | 1940 MB |
| 6 | 20.7 s | 2275 MB |

**時間はサブリニア、メモリはほぼリニア。** 4 を超えると効果が乏しい
（Harfbuzz 14 s と juce_graphics 15 s がクリティカルパスを作る）。

iPhone 15 Pro 実測: **3 並列で 36.1 s / 1691 MB、jetsam なし。**

実装では並列度を可変にし、ピークメモリを併記して**トレードオフが見える**
ようにすること。検証アプリは `min(4, コア数/2)` を既定にしている。
iPad Pro M2 はメモリ上限が高いので、より上げられる余地がある（未実測）。

### メモリ ― 最重要の知見

**メモリは並列度で決まり、TU 数では決まらない。**

| | 19 TU (5 モジュール) | 31 TU (12 モジュール) |
|---|---|---|
| 逐次のピーク RSS | 1000 MB | **960 MB** |
| 3 並列のピーク RSS | 1691 MB | **1730 MB** |
| 3 並列の時間 | 36.1 s | 48.5 s |

TU が 1.6 倍になってもピークメモリは +2%。増えるのは時間だけである。

逐次では RSS が 950 MB 前後で頭打ちになり、以降は微減する。これは各 TU の
一時的なワーキングセットが解放され、保持されるのは有界な量だけであることを
意味する。並列にすると「保持分 + 並列度 × 一時分」になる。

**したがって規模の問題は事実上存在しない。** 100 TU のプロジェクトでも、
並列度を同じにすればメモリはほぼ同じはずである（未実測）。実装で調整すべき
パラメータは並列度ただ一つ。

---

## 2. 動かせない制約（設計の出発点）

### 2.1 プロセスを起動できない

iOS は `fork`/`exec` をサンドボックスで禁じている。したがって通常のツール
チェーン構成 —— ドライバが `clang -cc1` を spawn し、次にリンカを spawn する
—— は**存在し得ない**。

すべてアプリにリンクし、ライブラリとして関数呼び出しで駆動する:

| ツール | 使えない形 | 使う形 |
|---|---|---|
| clang | `clang` ドライバ | `clang::CompilerInstance::ExecuteAction` + `EmitObjAction` |
| lld | `ld64.lld` 実行ファイル | `lld::lldMain(args, out, err, {{lld::Darwin, &lld::macho::link}})` |
| 署名 | `codesign` | zsign を `ZSignAsset::Init` + `ZBundle::SignFolder` |

lld は**ライブラリ利用を公式に想定**しており、`lldMain()` は再入可能と明記され
`Result::canRunAgain` を返す。実機 400 サイクルで一度も `false` にならなかった。

### 2.2 ドライバを迂回する = ドライバの仕事を引き受ける

**本文書で最も重要な節。** これを知らずに始めると数日溶ける。

`-cc1` はドライバが渡す前提の設定を一切自分で導出しない。以下はすべて実際に
踏んだもので、**どれもエラーメッセージが原因を指さない**。

| 欠落 | 症状 |
|---|---|
| `-internal-iframework` ×3 | `'UIKit/UIKit.h' file not found`。SDK は存在し、全ヘッダが到達不能 |
| `-fgnuc-version=4.2.1` | `sys/cdefs.h`「Unsupported compiler」/ `TargetConditionals.h`「unknown compiler」/ `__darwin_va_list` 型衝突 |
| `-fdefine-target-os-macros` | `TARGET_OS_*` 未定義 |
| `-fobjc-runtime=ios-N` | 誤った Objective-C ABI |
| `-target-cpu` / `-target-feature +neon` 他 | `<simd/matrix.h>` が `vzip1q_f64` を「未定義の識別子」と言う。NEON という語は出ない |
| `-fexceptions -fcxx-exceptions` | 「cannot use 'try' with exceptions disabled」。フラグ未指定とは言わない |
| 検索パスの**順序** | `<cstddef>`「header search paths が正しくない」。libc++ の `<stddef.h>` が clang builtin より**先**でなければならない |
| `-resource-dir` のレイアウト | `'stdarg.h' file not found`。`<dir>/include/` が検索されるので、中身を直に置いてはいけない |

`__GNUC__` が特に厄介である。**現代の clang はドライバに指示されたときだけ
`__GNUC__` を定義する。** cc1 直叩きでは未定義になり、Apple のヘッダ群が
「未知のコンパイラ」として全面的に拒否する。

**確定した引数構成**（`PocClangIOS/src/PocClang.cpp` が実装）:

```
-triple arm64-apple-ios17.0
-emit-obj
-o <out>
-resource-dir <bundle>/clang-resource        # <dir>/include が検索される
-isysroot <sysroot>

# 順序が重要。libc++ が先、resource-dir は3番目
-internal-isystem        <sysroot>/usr/include/c++/v1
-internal-isystem        <sysroot>/usr/local/include
-internal-isystem        <resource-dir>/include
-internal-externc-isystem <sysroot>/usr/include
-internal-iframework     <sysroot>/System/Library/Frameworks
-internal-iframework     <sysroot>/System/Library/SubFrameworks
-internal-iframework     <sysroot>/Library/Frameworks

# ターゲット。triple からは導出されない
-target-cpu apple-a7
-target-abi darwinpcs
-target-feature +v8a -target-feature +aes -target-feature +fp-armv8
-target-feature +neon -target-feature +perfmon -target-feature +sha2

# Apple のヘッダが要求するもの
-fgnuc-version=4.2.1
-fdefine-target-os-macros
-fobjc-runtime=ios-17.0
-fobjc-exceptions
-fblocks
-fexceptions
-fcxx-exceptions

-x <objective-c++|c++|c> [-std=gnu++17] -O3
-D... -I... <source>
```

`apple-a7` は素の `arm64-apple-ios` triple に対してドライバが選ぶ値で、
64bit iOS 端末すべてが満たすベースライン。

**検証手順として推奨**: 迷ったら `clang -### <driver args>` の出力と突き合わせる。
新しい Xcode / LLVM に上げるたびに差分が出うるので、この照合を自動テストに
組み込むこと。

### 2.3 バックグラウンドで I/O が壊滅する

SDK zip の展開が **前面 8.8 秒 → 背面 10 分超**。これは PoC の不手際ではなく
**製品仕様の制約**である。

必須:
- ビルド中は `UIApplication.idleTimerDisabled = YES`
- `UIApplicationDidEnterBackgroundNotification` を記録し、ユーザーに見せる
- 長時間処理には進捗表示。**10 分間「処理中」としか言わないログはハングと
  区別がつかない**（実際に一度、起きていないメモリ強制終了を追ってしまった）

---

## 3. アーキテクチャ

### 3.1 全体

```
Projucer (iPad)
 ├─ Project (.jucer)              既存
 ├─ OnDeviceExporter              新規。既存 Xcode exporter の兄弟
 │    └─ BuildManifest 生成       ソース一覧・defines・includes・frameworks
 ├─ BuildEngine                   新規
 │    ├─ ToolchainStore           zip → データ領域（SDK / JUCE / toolchain）
 │    ├─ Compiler                 clang -cc1 を in-process
 │    ├─ Linker                   lld::macho を in-process
 │    ├─ BundleBuilder            Info.plist + .app 構築
 │    ├─ Signer                   zsign を in-process
 │    └─ Installer                loopback TLS + itms-services
 └─ 既存 MCP ブリッジ              AI エージェント接続（別途）
```

### 3.2 Projucer が既に持っている資産

**これが決定的に効く。** `jucer_ProjectExport_Xcode.h` は既に以下を計算済み:

- ソースファイル一覧（モジュールの unity build 単位を含む）
- header search paths / preprocessor defines
- リンクする framework 一覧
- Info.plist の内容 / entitlements
- deployment target / architecture / bundle identifier

つまり **ビルドグラフを設計する仕事が存在しない**。必要なのは
「`.xcodeproj` を書き出す」を「マニフェストを生成する」に差し替えた
**新しい exporter を 1 本足すこと**。

接続点:

```
Projucer/Source/ProjectSaving/jucer_ProjectExporter.cpp:78
    ProjectExporter::getExporterTypeInfos()      ← ここに登録
Projucer/Source/ProjectSaving/jucer_ProjectExporter.h:133-159
    実装すべき純粋仮想関数（getExporterIdentifier, create, isiOS 等）
```

`create()` が `.xcodeproj` の代わりに `manifest.json` を書き出す。

### 3.3 マニフェスト形式（検証で使用したもの）

```json
{
  "name": "JuceHello",
  "bundleId": "tokyo.studio-r.jucehello",
  "defines":  ["JUCE_MODULE_AVAILABLE_juce_core=1", "..."],
  "includes": ["generated/JuceLibraryCode", "juce/modules"],
  "sources":  [{"file": "juce/modules/juce_core/juce_core.mm",
                "language": "objective-c++"}, ...],
  "frameworks": ["UIKit", "Foundation", "UserNotifications", ...],
  "libraries":  ["System", "c++"]
}
```

言語は `c` / `c++` / `objective-c++` の 3 種。**C ソースに `-std=gnu++17` を
渡してはならない**（JUCE は zlib / libpng / libjpg / Sheenbidi / lunasvg を
C として同梱している）。

---

## 4. 実装コンポーネント詳細

### 4.1 ToolchainStore（zip をデータ領域へ）

参照実装: `PocLinkIOS/src/ZipStore.{h,cpp}`

必要なもの:

| 配布物 | zip | 展開後 | 内容 |
|---|---|---|---|
| iOS SDK | 31.5 MB | 192 MB | `usr/`, `System/` をそのまま |
| JUCE | 12 MB | 48 MB | `modules/` + 生成された JuceHeader.h |
| clang builtin headers | 数 MB | — | アプリバンドルに同梱で可 |
| LLVM/clang/lld/zsign | — | — | アプリバイナリに静的リンク（約 100 MB） |

設計上の要点:

- **zip で十分**。iOS SDK は 8935 ファイルで **symlink が 1 本もない**。
  形式を決める前に確認したことであり、これが「もっと凝った形式が不要」な理由。
- **削るな。** 70〜192 MB を選別する価値はない。ユーザーが Xcode.xip から
  作った zip をそのまま受け取れる方が運用が楽。
- **完了判定はディレクトリの存在ではなく実ファイルで行う。**
  `usr/lib/libSystem.tbd` と `UIKit.framework` の実在を見る。中途半端な展開を
  成功と誤認しない。
- **zip の差し替えを検出すること。** サイズ+mtime をスタンプとして展開先に
  保存し、一致しなければ再展開する。これを怠ると新しいプロジェクトを送っても
  古いものがビルドされ続ける（実際に踏んだ）。
- 展開先を iCloud バックアップ対象から外すこと（192 MB）。

### 4.1.5 並列ビルド

参照実装: `PocJuceIOS/src/BuildRunner.{h,mm}` — **ホストハーネスと実機アプリが
この 1 ファイルを共有している。**

- TU ごとに独立した `CompilerInstance` と診断を持つ。共有されるのは
  ターゲットレジストリだけで、それは全処理の前に一度書かれる。
- `dispatch_semaphore` で並列度を絞り、`dispatch_group_wait` で待つ。
- **ブロックは値キャプチャする。** `std::atomic` も結果ベクタもコピーできない
  ため、共有状態はポインタ経由で触ること。
- 進捗コールバックは直列キューで呼ぶこと。

### 4.2 Compiler

参照実装: `PocClangIOS/src/PocClang.{h,cpp}`

```cpp
CompilerInvocation::CreateFromArgs (invocation, args, diagnostics);  // "-cc1" は含めない
CompilerInstance instance (invocation);
instance.createDiagnostics (&printer, /*ShouldOwnClient=*/false);
EmitObjAction action;
instance.ExecuteAction (action);
```

- `LLVMInitializeAArch64{TargetInfo,Target,TargetMC,AsmParser,AsmPrinter}` を
  一度だけ呼ぶ。
- 診断は `TextDiagnosticPrinter` を `raw_string_ostream` に向け、呼び出し側へ
  文字列で返す。端末にコンソールはない。
- **公開ヘッダに LLVM の型を出さないこと。** UI 側が `-fno-rtti` に縛られず、
  ホストハーネスと実機アプリで同一コードを走らせられる。

### 4.2.5 compiler-rt builtins

**ドライバは `libclang_rt.ios.a` を黙ってリンクしている。**

これが無いと `juce_dsp` が `std::complex<double>` を除算した瞬間に
`undefined symbol: ___divdc3` でリンクが失敗する。エラーは「ランタイム
ライブラリが丸ごと無い」とは言わない。64bit 除算・複素数演算・ソフト浮動小数を
使うプロジェクトはすべて同じ壁に当たる。

Xcode のものを流用せず、**LLVM ソースから自前でビルドすること**（0.4 MB）:

```sh
cmake -S llvm-project/compiler-rt/lib/builtins -B build-rt-ios -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=llvm-project/llvm/cmake/platforms/iOS.cmake \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
  -DCMAKE_BUILD_TYPE=Release -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
  -DCMAKE_C_COMPILER_TARGET=arm64-apple-ios17.0
ninja -C build-rt-ios
# -> build-rt-ios/lib/darwin/libclang_rt.ios.a
```

自前ビルドと Xcode 由来で、生成される実行ファイルはバイト単位で一致した
(11,944,936 バイト)。アプリバンドル内では clang 自身と同じ配置
`<resource-dir>/lib/darwin/` に置き、リンク引数に絶対パスで渡す。

### 4.3 Linker

参照実装: `PocLinkIOS/src/PocLink.{h,cpp}`

```cpp
LLD_HAS_DRIVER (macho)
auto r = lld::lldMain (args, out, err, {{ lld::Darwin, &lld::macho::link }});
// r.retCode, r.canRunAgain
```

- **`-platform_version ios <min> <sdk>` は必須。** 付けないと
  `LC_BUILD_VERSION` が付かず、iOS バイナリにならない。
- `-syslibroot <sysroot>`、`-lSystem -lc++`、`-framework ...`。
- `canRunAgain` が `false` を返したら以降のビルドを拒否すること。実機 400 回で
  一度も起きていないが、lld が明示的に用意している契約である。

### 4.4 BundleBuilder

`.app` に最低限必要な Info.plist キー（検証済み）:

```
CFBundleIdentifier / CFBundleExecutable / CFBundleName / CFBundleDisplayName
CFBundleVersion / CFBundleShortVersionString / CFBundlePackageType=APPL
CFBundleInfoDictionaryVersion=6.0 / MinimumOSVersion / UIDeviceFamily=[1,2]
CFBundleSupportedPlatforms=[iPhoneOS] / DTPlatformName=iphoneos
UILaunchScreen={} / UIRequiredDeviceCapabilities=[arm64]
```

**実行ファイルに 0755 を設定すること。** zip 往復で実行ビットが失われ、
installd は実行不能な実行ファイルを持つバンドルを拒否する。

### 4.5 Signer

参照実装: `PocSignIOS/src/PocSign.{h,cpp}`、zsign (MIT)

- `ZSignAsset::Init(cert, p12, prov, entitlements, password, adhoc, sha256Only, singleBinary)`
  → `ZBundle::SignFolder(...)`。フォルダを in-place で署名する。
- zsign の CLI エントリ `src/zsign.cpp`（`main()` を持つ）は除外。
  `third-party/minizip/iowin32.c` も除外（`windows.h` を include する）。
  **それ以外にソース修正は不要。**
- entitlements は空文字を渡せば provisioning profile から導出される。
- ログは stdout 直書きで callback がないため、署名中だけ `dup2` で捕捉する。
  **出力先を `.app` の隣に置かないこと** —— `Payload/` に紛れ込み installd が
  拒否する。`$TMPDIR` を使う。

**OpenSSL に関する重大な注意:**

キーチェーンが書き出す PKCS#12 は **RC2-40-CBC** で暗号化されている。
OpenSSL 3 は RC2 を **legacy provider** から提供し、これは動的ロードされる
モジュールなので **iOS では原理的に利用できない**（ビルドフラグでは解決しない）。

対処は資材側:

```sh
openssl pkcs12 -in exported.p12 -legacy -nodes -out combined.pem
openssl pkcs12 -export -in combined.pem -out modern.p12 \
    -keypbe AES-256-CBC -certpbe AES-256-CBC -macalg sha256
```

**アプリ側で import 時に自動再エンコードすること。** ユーザーに openssl を
叩かせてはいけない。ホスト（Homebrew の OpenSSL）では legacy provider が
あるため再現しない —— 実機でしか出ない。

### 4.6 Installer

参照実装: `PocSelfInstall/`、`PocFullChain/app/main.mm`

`itms-services://?action=download-manifest&url=<https url>` を `openURL` する。

**成立条件は 1 つだけ: manifest が「端末が既に信頼している証明書での HTTPS」で
配信されること。** 自己署名では不可、CA プロファイルの手動インストールが要る。

検証で採った回避策は `*.backloop.dev`:
- DNS が `127.0.0.1` に解決される
- Let's Encrypt 署名の証明書と**秘密鍵が公開されている**

これにより端末が自分自身に信頼された TLS を提示できる。pairing record も VPN も
entitlement も外部サービスも不要。

`NWListener` + `sec_identity_create`（PKCS#12 から `SecPKCS12Import`）で
127.0.0.1 に bind し、`/manifest.plist` と `/payload.ipa` を返すだけ。

**ipa の構造**: ルートが `Payload/` でなければ installd は**何も言わずに拒否**
する。`Zip::Archive` は与えられたフォルダの**中身**を書くので、`Payload` の
**親**を渡すこと。また **出力先を圧縮対象の外に置くこと**（自分自身を含む
ディレクトリの圧縮は失敗する）。

**この経路の制約: インストール時にタップが必要。** ヘッドレスにはならない。

---

## 5. 作業分解

前提: 実装者は C++ / Objective-C++ / CMake に習熟していること。LLVM の内部知識は不要。

| # | 作業 | 規模 | 依存 |
|---|---|---|---|
| 1 | ツールチェーンのクロスビルド自動化（LLVM+clang+lld / OpenSSL / zsign） | 中 | — |
| 2 | `OnDeviceExporter` を Projucer に追加、マニフェスト生成 | 中 | — |
| 3 | `ToolchainStore` — zip 取り込み・展開・スタンプ管理・UI | 小 | 1 |
| 4 | `Compiler` / `Linker` ラッパ移植 | 小 | 1 |
| 5 | `BundleBuilder` / `Signer` / `Installer` | 中 | 1 |
| 6 | 資材管理 UI（証明書 import + 自動再エンコード、profile、Keychain 保管） | 中 | 5 |
| 7 | ビルドのキャンセルと並列度 UI（並列自体は実装済み・実証済み） | 小 | 4 |
| 8 | 増分ビルド（.o キャッシュ、依存追跡） | 大 | 4, 7 |
| 9 | エラーの UI 表示（診断のパース、ソース行への紐付け） | 中 | 4 |
| 10 | 前面維持・進捗・バックグラウンド検出 | 小 | — |

**1〜5 と 10 で「動く」。6〜9 が「使える」。**

推奨順序: 1 → 4 → 2 → 3 → 5 → 10 → 9 → 7 → 8 → 6

理由: 4 を先に立ち上げると、以降すべての作業を**ホスト上で検証できる**
（後述）。

---

## 6. 開発手法の指示

### 6.1 ホストと実機で同一コードを走らせること

**これは推奨ではなく必須**とする。

`PocClang.h` / `PocLink.h` / `PocSign.h` は LLVM や zsign の型を一切公開して
いない。同じ `.cpp` を macOS CLI と iOS アプリの両方にリンクできる。

効果は実測で明らかだった。ある不具合を実機でだけ追っていたときは 1 回の試行に
端末往復が必要だったが、**ホストで同じ失敗を再現できた瞬間に、6 つの不足フラグが
数分で全部見つかった**。

**新しい機能はまずホストハーネスで通すこと。** 実機は「iOS が拒否するか」を
確かめるためだけに使う。

### 6.2 ログではなく成果物を検証すること

Gate 01 と Gate 08 では、端末が生成した `.o` / 実行ファイルを吸い出し、
`otool` でホスト生成物と照合した（バイト単位で一致）。

`.ipa` の構造不正を見つけられたのもこの手法である —— installd は**エラーを
一切出さずに**インストールしなかった。ログには何もない。`unzip -Zl` で
一目だった。

```sh
xcrun devicectl device copy from --device <id> \
  --domain-type appDataContainer --domain-identifier <bundle> \
  --source Documents/<path> --destination ./<local>
```

### 6.3 PASS 判定を甘くしないこと

- ファイルが生成された ≠ 成功。**`LC_BUILD_VERSION` の platform が 2 (iOS)**
  であることまで見る。ホスト向け生成物との差はこの 1 つのロードコマンドだけ。
- インストールできた ≠ 成功。**起動するまで**見る（AMFI は起動時にも検証する）。
- 判定条件はコード 1 箇所に置き、ホストと実機で食い違わないようにする
  （`MachOCheck.h` の `isIOSArm64Executable()` がその例）。

---

## 7. 未証明・未解決

### 7.1 規模

検証したのは **JUCE 12 モジュール / 31 TU** である。

- ピークメモリは 200 ms 間隔の RSS 観測であり、真のピークはこれより高い
  可能性がある。
- iPad Pro M2 は iPhone 15 Pro より jetsam 上限が高いと期待されるが、未実測。
  より高い並列度が使える余地がある。

**検証済み**（2026-08-20 追加）: audio モジュール込み 31 TU で、メモリは
19 TU 構成と変わらなかった。上記「メモリ」節を参照。

**残る規模の未検証**: 100 TU 級。理屈の上ではメモリは変わらないはずだが、
確認されていない。また iPad Pro M2 での並列度上限も未実測。

### 7.2 資材の入手

証明書・provisioning profile・SDK はすべて **Mac から持ち込んだファイル**である。

- Apple Developer Services を端末から叩いて証明書/profile を取得する経路は
  未着手。参照実装は **xtool (MIT)** の `xtool ds`。
- SDK も Mac の Xcode から抽出した。Xcode.xip を端末で展開する経路は未検証。
- 検証では p12 が**アプリバンドルに平文で、パスワードがソースに**入っている。
  実装では実行時 import + Keychain 保管が必須。

### 7.3 その他

- **インストールにタップが必要。** `installd` 直接経路（xtool / idevice が使う）
  なら不要かもしれないが未検証。build-run ループの体感に直結する。
- `*.backloop.dev` は第三者のドメインと証明書で、週次再発行・**2026-10-29 失効**。
  製品化するなら自前のループバック解決ドメインと証明書が要る。普通の作業だが
  作業である。
- 増分ビルドは未実装。JUCE モジュールの `.o` は使い回せるはずで、ここが
  vibe coding ループの体感を決める。
- デバッガは対象外。ログは生成アプリ側にシムを仕込めば Apple のインフラなしで
  取れる（IDE は自分がビルドしたアプリしか動かさないため）。

---

## 8. ライセンス

| | ライセンス | 用途 |
|---|---|---|
| LLVM / clang / lld | Apache-2.0 with LLVM exception | コンパイラ・リンカ |
| OpenSSL | Apache-2.0 | zsign の依存 |
| zsign | **MIT** | 署名 |
| xtool | **MIT** | Developer Services の参照実装 |
| a-Shell / ios_system | BSD-3 | iOS 上の clang 先行事例 |
| Feather | GPL-3.0 | backloop.dev 手法の参照（読むのみ） |

Projucer 本体は AGPLv3（JUCE 商用ライセンスとのデュアル）。GPL/AGPL 系を
取り込むと商用ライセンス路線は失われるが、上記の採用分は **MIT / BSD /
Apache のみ**である。

---

## 9. 参照実装の所在

```
~/Documents/src/GATES.md            全体サマリ
~/Documents/src/PocClangIOS/        Gate 01  clang in-process
~/Documents/src/PocSelfInstall/     Gate 02  self-install
~/Documents/src/PocSignIOS/         Gate 04  on-device signing
~/Documents/src/PocLinkIOS/         Gate 08/09  lld、SDK、UIKit ビルド
~/Documents/src/PocFullChain/       4 Gate 統合
~/Documents/src/PocJuceIOS/         JUCE 19 TU ビルド（本命）
```

各ディレクトリの `RESULT.md` には、成功した数値だけでなく**踏んだ失敗と
その原因**が記録してある。実装時に同じ穴に落ちないための一次資料である。
