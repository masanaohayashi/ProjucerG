# JUCE 8.0.13 と JUCE juce9 branch の Projucer 差分調査

調査対象:

- JUCE 8.0.13: `JUCE-8.0.13/extras/Projucer`
  - commit: `7c9d378`
- JUCE juce9 branch: `JUCE-juce9/extras/Projucer`
  - commit: `f8eb289`
  - branch: `juce9`

注意: `juce9` branch では、現時点でも `juce_StandardHeader.h` の version macro は `8.0.13` のままだった。

```cpp
#define JUCE_MAJOR_VERSION      8
#define JUCE_MINOR_VERSION      0
#define JUCE_BUILDNUMBER        13
```

## 概要

`extras/Projucer` 全体では 113 files changed, 1977 insertions, 2671 deletions だった。ただし、その大部分は各ファイル先頭のライセンスヘッダ変更で、実装上の差分はかなり限定的。

最も重要な点:

- `Projucer.jucer` は JUCE 8.0.13 と `juce9` branch で差分なし。
- `Source/ComponentEditor` は JUCE 8.0.13 公式にも `juce9` branch 公式にも存在しない。
- GUI Editor 復活に関係する `Application`, `CodeEditor`, `ProjectContentComponent` 周辺の構造は大きく変わっていない。
- 実質差分は、SVG 読み込み API 変更、ARA 関連の依存更新、PACE 設定初期化の修正、Xcode exporter の notarization 関連設定追加が中心。

## ライセンスヘッダ変更

多くのソースファイルで、JUCE 8 の一般的な dual-license 文言から、JUCE 9 preview 用の文言に置き換わっている。

JUCE 9 preview 側のヘッダでは以下の趣旨になっている。

- `This file is part of the JUCE 9 preview.`
- AGPLv3 で利用可能
- JUCE 9 preview では commercial license できない

これは実装差分ではないが、`juce9` branch をベースに ProjucerG を作る場合、README/LICENSE の表現を branch ごとに見直す必要がある。特に「commercial JUCE licence を持っている場合」という現在の README 表現は、JUCE 9 preview branch では合わない可能性がある。

## プロジェクト構成

### `Projucer.jucer`

`JUCE-8.0.13/extras/Projucer/Projucer.jucer` と `JUCE-juce9/extras/Projucer/Projucer.jucer` は差分なし。

つまり公式側では、Projucer のソースグループ、exporter 定義、module list は `.jucer` 上では変更されていない。

### 生成済みプロジェクト

以下の生成済み project files には差分がある。

- `Builds/LinuxMakefile/Makefile`
- `Builds/MacOSX/Projucer.xcodeproj/project.pbxproj`
- `Builds/VisualStudio2019/Projucer_App.vcxproj`
- `Builds/VisualStudio2019/Projucer_App.vcxproj.filters`
- `Builds/VisualStudio2022/Projucer_App.vcxproj`
- `Builds/VisualStudio2022/Projucer_App.vcxproj.filters`
- `Builds/VisualStudio2026/Projucer_App.vcxproj`
- `Builds/VisualStudio2026/Projucer_App.vcxproj.filters`

主な理由は、JUCE module 側の生成物が変わったためと見られる。`JUCE-juce9/extras/Projucer/JuceLibraryCode` には `include_juce_graphics_lunasvg.c` が追加されている。

`Projucer.jucer` 自体は同一なので、ProjucerG の `juce9` branch では `.jucer` の module path を `../JUCE-juce9` に変えて `--resave` すれば、生成済みプロジェクト側に同種の差分が出る想定。

## ComponentEditor への影響

公式 JUCE 8.0.13 と公式 `juce9` branch のどちらにも `extras/Projucer/Source/ComponentEditor` はない。

したがって、GUI Editor 復活作業としては JUCE 8 版で行った追加内容を `juce9` branch に再適用する必要がある。

ただし、今回の公式差分を見る限り、GUI Editor 復活に必要だった以下の接続箇所は大きく構造変更されていない。

- `Application/jucer_Application.*`
- `Application/jucer_CommandIDs.h`
- `CodeEditor/jucer_OpenDocumentManager.cpp`
- `Project/UI/jucer_ProjectContentComponent.*`
- `Utility/Helpers/jucer_PresetIDs.h`

そのため、JUCE 8 版 ProjucerG の GUI Editor 復活パッチは、`juce9` branch に対しても比較的そのまま移植できる可能性が高い。

## 実質差分の詳細

### 1. SVG 読み込み API の変更

複数箇所で、SVG XML を `XmlDocument::parse` / `parseXML` してから `Drawable::createFromSVG` する処理が、`Drawable::createFromSVGString` に置き換わっている。

対象例:

- `Source/Application/StartPage/jucer_ContentComponents.h`
- `Source/CodeEditor/jucer_ItemPreviewComponent.h`
- `Source/Project/UI/jucer_ContentViewComponent.h`

例:

```cpp
return Drawable::createFromSVGString (iconSvgData);
```

背景として、JUCE 9 preview では SVG 処理に `lunasvg` が入っており、`JuceLibraryCode/include_juce_graphics_lunasvg.c` も追加されている。

GUI Editor の移植で注意する点:

- ComponentEditor 側に `Drawable::createFromSVG`, `XmlDocument::parse`, `parseXML` を使うコードがあれば、JUCE 9 側では同様に `Drawable::createFromSVGString` へ寄せる必要がある可能性が高い。
- 画像/Drawable preview 周りのビルドエラーが出たら、まず SVG API 変更を疑う。

### 2. ARA 関連の project dependency 更新

`Project` 周辺で ARA 関連の依存更新が追加されている。

主な変更:

- `Project::updateVersionDependencies()` が追加。
- project version が変わったときに `pluginARAArchiveIDValue` の default を更新。
- `Project::updateProjectSettings()` で ARA 有効時に `araDocumentArchiveID` を project root に設定。
- project settings UI の再構築条件が `projectType` だけでなく `enableARA` も見るようになった。

関連ファイル:

- `Source/Project/jucer_Project.cpp`
- `Source/Project/jucer_Project.h`
- `Source/Project/UI/Sidebar/jucer_Sidebar.h`

GUI Editor とは直接関係しないが、`Project` / sidebar の変更なので、`ProjectContentComponent` 周辺に GUI Editor メニューを戻すときは conflict が起きる可能性がある。

### 3. PACE sharable target names の初期化修正

`ProjectExporter` の constructor で `paceUseSharableTargetNames` が初期化されるようになっている。

JUCE 8.0.13 側では `ProjectExporter.h` に member と getter があり、property UI にも使われているが、constructor の member initializer list に `paceUseSharableTargetNames` がなかった。

`juce9` branch では以下が追加されている。

```cpp
paceUseSharableTargetNames (settings, Ids::paceUsingSharableTargetNames, getUndoManager())
```

関連ファイル:

- `Source/ProjectSaving/jucer_ProjectExporter.cpp`

これは GUI Editor とは直接関係しないが、JUCE 9 base に移す場合はこの修正を保持するべき。

### 4. Xcode exporter の notarization 対応

`ProjectExport_Xcode.h` で、strip local symbols が有効な場合に以下の build setting を追加する処理が入っている。

```cpp
s.set ("CODE_SIGN_INJECT_BASE_ENTITLEMENTS", "NO");
```

コメントによると、strip する binary に base entitlements が注入されると notarisation を妨げる可能性があるため、それを避けるための設定。

関連ファイル:

- `Source/ProjectSaving/jucer_ProjectExport_Xcode.h`

GUI Editor とは直接関係しないが、`juce9` branch の exporter 修正として保持するべき。

### 5. BinaryData の更新

`JuceLibraryCode/BinaryData.cpp` / `.h` に差分がある。

これは主に template/resource の再生成差分と見られる。`Projucer.jucer` に差分がないため、手作業で追うより `juce9` branch の Projucer をベースにして `--resave` / BinaryData 生成状態を維持する方が安全。

## GUI Editor 復活を JUCE 9 branch に移す方針

JUCE 8 版と同じく、`juce9` branch でも「公式 Projucer をベースに GUI Editor を戻す」方針がよい。

推奨手順:

1. `juce9` branch を作る。
2. `JUCE-juce9/extras/Projucer` を `Projucer/` にコピーして JUCE 9 preview base にする。
3. `Projucer/Projucer.jucer` の module path を `../JUCE-juce9/modules` に変更する。
4. JUCE 8 版 ProjucerG で追加した `Source/ComponentEditor` を移植する。
5. JUCE 8 版で入れた接続差分を再適用する。
   - `jucer_Application.*`
   - `jucer_CommandIDs.h`
   - `jucer_OpenDocumentManager.cpp`
   - `jucer_ProjectContentComponent.*`
   - `jucer_PresetIDs.h`
6. JUCE 8 版で入れた `FontOptions` 対応と `PositionedRectangle::operator==` 修正も維持する。
7. SVG 関連の compile error が出た場合は `Drawable::createFromSVGString` へ更新する。
8. `--resave` して generated project files を安定化する。
9. Mac Debug build と GUI Editor 手動確認を行う。

## リスク

- `juce9` branch は preview で、ライセンス文言も JUCE 8 と違う。公開 README/LICENSE は branch ごとに見直す必要がある。
- version macro がまだ `8.0.13` なので、Projucer のバージョンチェック仕様上は JUCE 9 として扱われない可能性がある。
- SVG/lunasvg 周りは ComponentEditor の古いコードに影響する可能性がある。
- 公式 `Projucer.jucer` が同一なので大規模な project 構造変更は少なそうだが、generated project files は JUCE module 側の変化で差分が出る。

## 結論

JUCE 8.0.13 から `juce9` branch の Projucer への差分は、見た目ほど大きくない。多くはライセンスヘッダ変更で、実質差分は SVG 読み込み、ARA、PACE、Xcode exporter の小修正に集中している。

GUI Editor 復活の観点では、JUCE 8 版 ProjucerG の作業を `juce9` branch に移植する難度は高くなさそう。ただし、JUCE 9 preview のライセンス文言と SVG/lunasvg 変更には注意する必要がある。
