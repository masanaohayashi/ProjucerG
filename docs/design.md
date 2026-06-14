# JUCE 8 Projucer への GUI Editor 復活設計

## 目的

JUCE 8.0.13 の Projucer をベースに、JUCE 7.0.12 の Projucer に存在した GUI Editor を復活させる。

方針は「JUCE 7 の Projucer を JUCE 8 化する」のではなく、「JUCE 8 の Projucer に GUI Editor 関連機能だけを段階的に戻す」とする。これは JUCE 8 側の modules、ビルド設定、exporter、最新 SDK 対応、ライセンス文、生成ファイル構成を維持するため。

## 現在の作業ベース

作業対象:

- `Projucer/`

これは `JUCE-8.0.13/extras/Projucer` をコピーしたもの。

確認済み:

- Mac Debug ビルド成功
- 生成物: `Projucer/Builds/MacOSX/build/Debug/Projucer.app`

注意点:

- コピー先では Xcode project の相対配置が元リポジトリと異なる。
- そのため現時点ではビルド時に `HEADER_SEARCH_PATHS` で以下を補っている。
  - `JUCE-8.0.13/modules`
  - `JUCE-8.0.13/extras/Build`
  - `Projucer/JuceLibraryCode`

最終的にはこの補正をプロジェクト設定側に入れるか、CMake ベースに寄せるかを決める。

## 前提調査の要約

JUCE 8 の Projucer では GUI Editor は単なる非表示化ではなく、実体コードごと削除されている。

削除された中核:

- `Source/ComponentEditor`
- JUCE 7 側のファイル数: 86

削除された接続点:

- `OpenDocumentManager` から `createGUIDocumentType()` 登録が削除
- `CommandIDs` から `enableGUIEditor` と `addNewGUIFile` が削除
- `jucer_Application.cpp` から GUI Editor メニューと有効化トグルが削除
- `ProjectContentComponent` から新規 GUI Component 作成導線が削除
- `CMakeLists.txt`, `Projucer.jucer`, Xcode project などから `ComponentEditor` のソース登録が削除

## 復活対象の構成

JUCE 7 の `Source/ComponentEditor` は大きく以下に分かれる。

| 領域 | 内容 |
| --- | --- |
| `jucer_JucerDocument.*` | GUI Editor の document 中核。C++ 内の `JUCER_COMPONENT` メタデータ読み書き、生成コード反映、DocumentType 登録 |
| `jucer_ComponentLayout.*` | Component 配置、選択、位置情報、Undo 連携 |
| `jucer_GeneratedCode.*` | GUI Editor から C++ コードを生成するためのバッファ/差し込み処理 |
| `jucer_PaintRoutine.*` | paint() 生成対象の管理 |
| `jucer_ObjectTypes.*` | 編集可能な Component / PaintElement 種別の登録 |
| `jucer_BinaryResources.*` | GUI Editor document が使う画像などの binary resource 管理 |
| `Components/*Handler*` | Label/Button/Slider/ComboBox など各 Component の生成・復元・property 編集・コード生成 |
| `Documents/*Document*` | Component 用、Button 用の具体 document |
| `PaintElements/*` | Rectangle/Ellipse/Path/Image/Text などの描画要素 |
| `Properties/*` | GUI Editor 専用 property component |
| `UI/*` | レイアウトエディタ、ペイントエディタ、リソースパネル、document editor、テスト表示 |

## 実装方針

段階的に進める。最初からメニュー、新規作成、全 exporter をまとめて復活させない。

理由:

- JUCE 8 modules 側 API 変更によるコンパイルエラーの切り分けをしやすくする
- 「既存 GUI Editor 対応 `.cpp` を開ける」ことと「新規 GUI ファイルを作る」ことは別機能
- Xcode project / `.jucer` / CMake の更新を分けないと、生成ファイル差分が大きくなりすぎる

## フェーズ 1: GUI Editor 本体をビルドに戻す

目的:

- `Source/ComponentEditor` を作業対象 `Projucer/Source` に追加する
- Mac Debug ビルドでコンパイル対象に入れる
- まだメニューや新規作成機能は戻さなくてもよい

作業:

1. `JUCE-7.0.12/extras/Projucer/Source/ComponentEditor` を `Projucer/Source/ComponentEditor` にコピーする。
2. `Projucer/Builds/MacOSX/Projucer.xcodeproj/project.pbxproj` に必要な `.cpp` を追加する。
3. まず CMake 側も更新できるように `Projucer/CMakeLists.txt` に `ComponentEditor` の `.cpp` 一覧を追加する。

追加する `.cpp`:

- `Source/ComponentEditor/Components/jucer_ComponentTypeHandler.cpp`
- `Source/ComponentEditor/Documents/jucer_ButtonDocument.cpp`
- `Source/ComponentEditor/Documents/jucer_ComponentDocument.cpp`
- `Source/ComponentEditor/PaintElements/jucer_ColouredElement.cpp`
- `Source/ComponentEditor/PaintElements/jucer_PaintElement.cpp`
- `Source/ComponentEditor/PaintElements/jucer_PaintElementGroup.cpp`
- `Source/ComponentEditor/PaintElements/jucer_PaintElementImage.cpp`
- `Source/ComponentEditor/PaintElements/jucer_PaintElementPath.cpp`
- `Source/ComponentEditor/UI/jucer_ComponentLayoutEditor.cpp`
- `Source/ComponentEditor/UI/jucer_ComponentOverlayComponent.cpp`
- `Source/ComponentEditor/UI/jucer_EditingPanelBase.cpp`
- `Source/ComponentEditor/UI/jucer_JucerDocumentEditor.cpp`
- `Source/ComponentEditor/UI/jucer_PaintRoutineEditor.cpp`
- `Source/ComponentEditor/UI/jucer_PaintRoutinePanel.cpp`
- `Source/ComponentEditor/UI/jucer_ResourceEditorPanel.cpp`
- `Source/ComponentEditor/UI/jucer_TestComponent.cpp`
- `Source/ComponentEditor/jucer_BinaryResources.cpp`
- `Source/ComponentEditor/jucer_ComponentLayout.cpp`
- `Source/ComponentEditor/jucer_GeneratedCode.cpp`
- `Source/ComponentEditor/jucer_JucerDocument.cpp`
- `Source/ComponentEditor/jucer_ObjectTypes.cpp`
- `Source/ComponentEditor/jucer_PaintRoutine.cpp`

完了条件:

- `ComponentEditor` のソースがビルド対象に入る
- コンパイルエラー一覧が得られる

## フェーズ 2: JUCE 8 API への最小移植

目的:

- JUCE 7 の GUI Editor コードを JUCE 8 の modules API でコンパイル可能にする

予想される修正:

| 影響領域 | 想定対応 |
| --- | --- |
| `Font::getStringWidth()` / `getStringWidthFloat()` | `GlyphArrangement` または `TextLayout` に置換。UI の概算幅なら `Font::getStringWidthFloat` 相当を helper 化する |
| `Font::getGlyphPositions()` | `GlyphArrangement` で代替 |
| `Typeface` / `CustomTypeface` | 使用箇所があれば JUCE 8 の `Typeface::Ptr`, `FontOptions`, fallback 前提へ修正 |
| `AlertWindow::show()` | 戻り値依存箇所を確認し、JUCE 8 の意味に合わせる |
| `Displays` 座標変換 | `Point<float>` 系 API へ寄せる |
| `TextEditor` | API 差分が出た箇所のみ修正。挙動差は後で確認 |
| strict warnings | JUCE 8 の warning 設定でエラー化する箇所を局所修正 |

方針:

- まずビルドを通すための最小修正に限定する
- UI デザインや生成コード仕様はこの段階では変えない
- 旧コードの大規模リファクタは避ける

完了条件:

- `Projucer - App` Debug がビルド成功する
- まだ GUI Editor が UI から使えなくてもよい

## フェーズ 3: 既存 GUI Editor ファイルを開けるようにする

目的:

- 既存の GUI Editor 対応 `.cpp` を開いたとき、`JucerDocumentEditor` が表示されるようにする

戻す接続点:

### `OpenDocumentManager`

JUCE 7 では以下があった。

```cpp
OpenDocumentManager::DocumentType* createGUIDocumentType();

OpenDocumentManager::OpenDocumentManager()
{
    registerType (new UnknownDocument::Type());
    registerType (new SourceCodeDocument::Type());
    registerType (createGUIDocumentType());
}
```

JUCE 8 では `createGUIDocumentType()` 登録が消えている。

復活方針:

- `jucer_OpenDocumentManager.cpp` に forward declaration を戻す
- constructor で `registerType (createGUIDocumentType())` を戻す
- 最初は GUI Editor 有効化設定に依存せず、常時登録でもよい

### `jucer_JucerDocument.cpp`

JUCE 7 側にある以下をそのまま復帰する。

- `JucerDocument::isValidJucerCppFile()`
- `JucerDocument::createForCppFile()`
- `JucerComponentDocument`
- `createGUIDocumentType()`

確認観点:

- `JUCER_COMPONENT` メタデータを持つ `.cpp` が GUI Editor として開くか
- メタデータを持たない通常 `.cpp` は SourceCodeDocument として開くか

完了条件:

- 既存 GUI Editor 対応 `.cpp` を開いたとき GUI Editor UI が起動する
- 通常 C++ ファイルの code editor 動作が壊れない

## フェーズ 4: GUI Editor メニューとコマンド復帰

目的:

- JUCE 7 にあった GUI Editor メニューと editor command を戻す

戻す対象:

### `jucer_CommandIDs.h`

復活候補:

```cpp
enableGUIEditor = 0x300027,
addNewGUIFile   = 0x300200,
```

注意:

- JUCE 8 側で同じ ID が再利用されていないか確認する
- 現状調査では該当 ID は削除されているだけ

### `jucer_Application.cpp`

戻す候補:

- `getMenuNames()` に `"GUI Editor"`
- `createMenu()` の `"GUI Editor"` 分岐
- `createToolsMenu()` の `enableGUIEditor`
- `getAllCommands()` の `enableGUIEditor`
- `getCommandInfo()` の `enableGUIEditor`
- `perform()` の `enableGUIEditor`
- `createGUIEditorMenu()` が JUCE 7 のどこにあるか確認し復帰
- `isGUIEditorEnabled()` / `enableOrDisableGUIEditor()` の実装復帰

方針:

- まず GUI Editor 常時有効で進める場合、`enableGUIEditor` は後回しでもよい
- ただし JUCE 7 の `JucerDocument::createEditor()` は `isGUIEditorEnabled()` を参照しているため、関数だけは必要になる可能性が高い

完了条件:

- メニューバーに GUI Editor メニューが出る
- GUI Editor document の command が効く

## フェーズ 5: 新規 GUI Component 作成を戻す

目的:

- Project sidebar / menu から「Add new GUI Component...」を使えるようにする

戻す対象:

### `ProjectContentComponent`

JUCE 7 側の復活候補:

- `getAllCommands()` に `CommandIDs::addNewGUIFile`
- `getCommandInfo()` に `addNewGUIFile`
- `perform()` に `addNewGUIFile`
- `ProjectContentComponent::addNewGUIFile()`
- header の `void addNewGUIFile();`

`addNewGUIFile()` は内部で `createGUIComponentWizard (*project)` を使う。

### `jucer_JucerDocument.cpp`

JUCE 7 側には `NewGUIComponentWizard` と `createGUIComponentWizard()` がある。これを戻す必要がある。

確認観点:

- `.h` / `.cpp` の空ファイル作成
- `ComponentDocument` 生成
- `flushChangesToDocuments (&project, true)`
- 生成された `.cpp` に `JUCER_COMPONENT` メタデータが入ること
- 生成直後に GUI Editor として開けること

完了条件:

- 新規 GUI Component を作れる
- 作成されたファイルを保存・再オープンできる

## フェーズ 6: `Projucer.jucer` と exporter 設定の整備

目的:

- 作業対象の `Projucer.jucer` から Xcode project / Visual Studio project / Makefile を再生成できる状態にする

対象:

- `Projucer/Projucer.jucer`
- `Projucer/CMakeLists.txt`
- `Projucer/Builds/MacOSX/Projucer.xcodeproj/project.pbxproj`
- 必要なら `Builds/VisualStudio*`, `Builds/LinuxMakefile`

方針:

- 最初は Mac Debug を優先
- `.jucer` 更新は後で実施して、Projucer 自身で保存した時に `ComponentEditor` が消えないようにする
- exporter project は `.jucer` から再生成できるようになってから整える

注意:

- 今回のコピー配置では module path が通常の JUCE checkout と違うため、`Projucer.jucer` の module path をどう扱うか決める必要がある
- いったん Xcode build setting に絶対パスを入れるより、ローカル作業用として `HEADER_SEARCH_PATHS` 指定で進める方が差分は小さい

## 検証計画

### ビルド検証

最低限:

```sh
xcodebuild \
  -project Projucer/Builds/MacOSX/Projucer.xcodeproj \
  -scheme "Projucer - App" \
  -configuration Debug \
  -derivedDataPath Projucer/Builds/MacOSX/DerivedData \
  HEADER_SEARCH_PATHS="/Users/ring2/Documents/src/Projucer8/JUCE-8.0.13/modules /Users/ring2/Documents/src/Projucer8/JUCE-8.0.13/extras/Build /Users/ring2/Documents/src/Projucer8/Projucer/JuceLibraryCode" \
  build
```

将来的には header path 補正なしで通る状態にしたい。

### 手動動作確認

1. Projucer 起動
2. 通常 `.jucer` project を開く
3. 通常 `.cpp` を開く
4. GUI Editor 対応 `.cpp` を開く
5. GUI Editor で Component を選択・移動・保存
6. 保存後に `.cpp` / `.h` が更新される
7. 再起動後に同じファイルを GUI Editor として開ける
8. 新規 GUI Component 作成

### 回帰確認

- 通常 code editor が壊れていないこと
- project save/export が壊れていないこと
- Projucer 自身の project を開いて保存しても `ComponentEditor` 登録が消えないこと

## `.jucer` と JUCE バージョン互換性チェック

GUI Editor 復活では、既存 project の互換性を不用意に変えないことが重要になる。Projucer には複数の「バージョン」概念があり、役割が違う。

### `.jucer` のファイルフォーマット番号

`.jucer` の root 要素には `jucerFormatVersion` 属性がある。

```xml
<JUCERPROJECT ... jucerFormatVersion="1">
```

JUCE 7.0.12 と JUCE 8.0.13 の Projucer はどちらも `Source/ProjectSaving/jucer_ProjectSaver.cpp` に `constexpr int jucerFormatVersion = 1;` を持っている。保存時には `ProjectSaver::writeProjectFile()` が古い `jucerVersion` 属性を削除し、root の `jucerFormatVersion` が現在値と違う場合に `1` へ更新する。

確認した範囲では、JUCE 7.0.12 / JUCE 8.0.13 の `Project::loadDocument()` は `JUCERPROJECT` tag の XML として parse できること、`ValueTree` の型が `JUCERPROJECT` であることを確認しているが、`jucerFormatVersion` が現在値より大きい場合に明示的に拒否する比較は見当たらない。少なくとも現在の JUCE 7/8 系では、保存側がフォーマット番号を管理し、読み込み側は schema の存在を前提に parse する実装になっている。

復活版 Projucer では `jucerFormatVersion` を上げない。GUI Editor 復活は Projucer アプリ内部の editor/document type/command の復帰であり、`.jucer` project schema の変更ではないため、ここを上げる理由がない。番号を上げると、将来または他 fork の Projucer との互換性判断に影響する。

### project の `version` 属性

`.jucer` root の `version` は project 自身の version であり、Projucer のファイルフォーマット番号ではない。Projucer 自身の `Projucer.jucer` では値が `8.0.13` なので Projucer/JUCE バージョンに見えるが、一般 project ではアプリや plugin の version として扱われる。

GUI Editor 復活作業で `Projucer.jucer` を保存・再生成する場合、`version` と `jucerFormatVersion` の意味を混同しない。GUI Editor 対応のために project version を変える必要もない。

### 「Projucer Out of Date」警告

Project warning の `oldProjucer` は `Source/Project/jucer_Project.h` の `ProjectMessages::Ids::oldProjucer` として定義され、表示文言は "Projucer Out of Date" / "The version of the Projucer you are using is out of date." になっている。

実際の判定は `Project::updateModuleWarnings()` で行われる。enabled modules を走査し、JUCE module について `LibraryModule::getVersion()` から得た module version を `getJuceVersion()` で数値化し、起動中 Projucer に組み込まれた `JUCE_MAJOR_VERSION` / `JUCE_MINOR_VERSION` / `JUCE_BUILDNUMBER` 由来の `getBuiltJuceVersion()` と比較する。

つまりこの警告は「開いた `.jucer` を新しい Projucer が作ったか」ではなく、「project が参照している JUCE module が、この Projucer 自身に組み込まれている JUCE より新しいか」を見ている。新しい JUCE module を古い Projucer で扱うと、module metadata や exporter 設定を正しく理解できない可能性があるため、project message として警告する。

復活版 Projucer は JUCE 8.0.13 ベースなので、この判定は JUCE 8.0.13 のまま維持する。GUI Editor のために JUCE 7 側の判定ロジックへ戻してはいけない。

### 最新版通知

`newVersionAvailable` は別系統の通知で、`Source/Utility/Helpers/jucer_VersionInfo.cpp` と `Source/Application/jucer_AutoUpdater.cpp` が GitHub Releases の `juce-framework/JUCE` を確認し、`ProjectInfo::versionString` より新しい release がある場合に開いている project へ notification を追加する。

これは project file の互換性チェックではなく、アプリ更新通知である。GUI Editor 復活作業では基本的に触らない。

### GUI Editor 復活への設計上の扱い

GUI Editor の layout metadata は、`.jucer` root schema ではなく GUI Editor 対応 `.cpp` 内の `BEGIN_JUCER_METADATA` / `END_JUCER_METADATA` ブロックに保存される。このため、GUI Editor を復活させるだけなら `.jucer` の `jucerFormatVersion` を上げる必要はない。

実装時の方針:

- `ProjectSaver` の `jucerFormatVersion = 1` は変更しない
- `Project::updateModuleWarnings()` と `oldProjucer` warning は JUCE 8 のまま維持する
- GUI Editor 復活で `.jucer` root に新しい属性を追加しない
- `Projucer.jucer` を保存・再生成した場合、`jucerFormatVersion="1"` が維持されることを確認する
- 旧 GUI Editor metadata の読み書きは `.cpp` 側の document type で完結させる

### 検証項目

1. JUCE 7.0.12 由来の project を復活版 Projucer で開ける
2. JUCE 8.0.13 由来の project を復活版 Projucer で開ける
3. project 保存後も `.jucer` の `jucerFormatVersion` が `1` のまま
4. project が JUCE 8.0.13 より新しい module を参照している場合、`Projucer Out of Date` warning が出る
5. GUI Editor 対応 `.cpp` を開いて保存しても `.jucer` の root schema が変わらない

## リスク

### JUCE 8 の font/text API 変更

GUI Editor は property panel、label handler、paint text element などで font/文字幅処理を使う可能性が高い。JUCE 8 では `Font::getStringWidth*` や glyph API が削除・deprecated になっているため、コンパイル修正が必要になる見込み。

### 旧 GUI Editor コードの保守状態

GUI Editor は JUCE 8 の `BREAKING_CHANGES.md` によれば長期間 deprecated で bugfix/maintenance されていなかった。ビルドが通っても、HiDPI、Unicode、TextEditor、マルチディスプレイ、アクセシビリティ周辺で挙動差が出る可能性がある。

### project generator との整合性

`Projucer.jucer` に戻さず Xcode project だけ編集すると、Projucer 自身で保存・再生成した時に変更が消える。この作業では `Projucer/Projucer.jucer` を正本として扱い、Xcode project / Visual Studio project / Makefile / CMake 側はそこから生成・同期される派生物として扱う。

実装時の原則:

- `ComponentEditor` の source 登録はまず `Projucer.jucer` に追加する
- 既存 Projucer で `Projucer.jucer` を開いて save し、Xcode project を再生成する
- 再生成された Xcode project で Mac Debug build を行い、コンパイルエラーを確認する
- Xcode project への直接編集は、原因切り分けや一時的なビルド実験に限る
- 最終コミットには `Projucer.jucer` と、そこから再生成された project files の整合した差分を含める

この順序にすると、後で Projucer 自身を保存した時に GUI Editor 復活用の source 登録が消える事故を避けられる。

### コピー配置の include path 問題

作業用 `Projucer/` は JUCE checkout 内の通常位置ではない。そのため既存 Xcode project の module search path が壊れる。実装中はビルドコマンドで補正し、必要になった段階で project 設定へ反映する。

## 作業順の推奨

1. `Source/ComponentEditor` をコピー
2. JUCE 7 の `Projucer.jucer` から `ComponentEditor` group の source 登録を確認する
3. 作業用 `Projucer/Projucer.jucer` に `ComponentEditor` group を追加する
4. 既存 Projucer で `Projucer.jucer` を開いて save し、Mac Xcode project などを再生成する
5. Mac Debug build を行い、コンパイルエラーを修正する
6. `OpenDocumentManager` に `createGUIDocumentType()` を戻す
7. 既存 GUI file を開く動作を確認
8. GUI Editor menu/command を戻す
9. 新規 GUI Component 作成を戻す
10. `Projucer.jucer` / CMake / exporter の整合を確認する
11. 手動確認と差分整理

## 初期スコープ外

以下は最初の復活では扱わない。

- GUI Editor の UI 改善
- 新しい JUCE 8 animation API への置換
- WebView UI 化
- 旧 Visual Studio 2017 / Code::Blocks exporter の復活
- 旧ライセンス/ログイン UI の復活
- GUI Editor 生成コード仕様の刷新

## 完了定義

最小完了:

- Mac Debug ビルドが通る
- 既存 GUI Editor 対応 `.cpp` を GUI Editor として開ける
- GUI Editor で保存できる

完全完了:

- 新規 GUI Component 作成ができる
- GUI Editor メニュー/コマンドが動く
- `Projucer.jucer` に変更が反映され、project 再生成後も復活部分が維持される
- Mac Debug ビルドが header path 補正なし、または明示的に設計されたローカルパス設定で通る
