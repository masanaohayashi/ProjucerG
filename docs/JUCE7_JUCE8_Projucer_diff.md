# JUCE 7.0.12 と JUCE 8.0.13 の Projucer 差分メモ

調査対象:

- JUCE 7: `JUCE-7.0.12/extras/Projucer`
- JUCE 8: `JUCE-8.0.13/extras/Projucer`
- 実差分確認: `git diff --no-index --shortstat`, `git diff --no-index --name-status`, `rg`

## 全体像

Projucer 全体の差分統計:

- 237 files changed
- 12956 insertions
- 33789 deletions

削除行が多い主因は GUI Editor、旧ユーザーアカウント/ライセンス UI、Code::Blocks exporter、Visual Studio 2017 exporter などの削除。

GUI Editor 復活に直接関係する最大の変更は、JUCE 8 で `Source/ComponentEditor` がディレクトリごと削除されていること。

## GUI Editor 削除の中核

JUCE 7 に存在し、JUCE 8 で消えている GUI Editor 本体:

- `Source/ComponentEditor`
- ファイル数: 86

削除された主要サブ領域:

| サブディレクトリ | 役割 |
| --- | --- |
| `Components` | Button、Label、Slider、ComboBox、TextEditor など、編集対象 Component ごとの handler |
| `Documents` | GUI Editor 用 document 実装。`ComponentDocument`, `ButtonDocument` |
| `PaintElements` | 背景・パス・画像・テキストなど、paint() 生成対象の編集要素 |
| `Properties` | GUI Editor 用 property component 群 |
| `UI` | 編集パネル、レイアウトエディタ、ペイントルーチンエディタ、リソースパネル、テスト表示 |
| 直下ファイル | `JucerDocument`, `ComponentLayout`, `GeneratedCode`, `ObjectTypes`, `PaintRoutine`, `BinaryResources` など |

つまり JUCE 8 では単にメニューを隠したのではなく、GUI Editor の実体コードと生成・保存・編集 UI が消されている。

## 削除された GUI Editor ファイル一覧

主なファイル群:

- `Source/ComponentEditor/jucer_JucerDocument.*`
- `Source/ComponentEditor/jucer_ComponentLayout.*`
- `Source/ComponentEditor/jucer_GeneratedCode.*`
- `Source/ComponentEditor/jucer_PaintRoutine.*`
- `Source/ComponentEditor/jucer_ObjectTypes.*`
- `Source/ComponentEditor/jucer_BinaryResources.*`
- `Source/ComponentEditor/UI/jucer_JucerDocumentEditor.*`
- `Source/ComponentEditor/UI/jucer_ComponentLayoutEditor.*`
- `Source/ComponentEditor/UI/jucer_PaintRoutineEditor.*`
- `Source/ComponentEditor/UI/jucer_ResourceEditorPanel.*`
- `Source/ComponentEditor/Components/jucer_ComponentTypeHandler.*`
- `Source/ComponentEditor/Components/jucer_*Handler.h`
- `Source/ComponentEditor/PaintElements/jucer_PaintElement*`
- `Source/ComponentEditor/Properties/jucer_*Property*.h`

完全な一覧は `find JUCE-7.0.12/extras/Projucer/Source/ComponentEditor -type f` で確認できる。

## Document open 統合の削除

GUI Editor は `OpenDocumentManager` の document type として登録されていた。

JUCE 7:

```cpp
OpenDocumentManager::DocumentType* createGUIDocumentType();

OpenDocumentManager::OpenDocumentManager()
{
    registerType (new UnknownDocument::Type());
    registerType (new SourceCodeDocument::Type());
    registerType (createGUIDocumentType());
}
```

JUCE 8:

```cpp
OpenDocumentManager::OpenDocumentManager()
{
    registerType (new UnknownDocument::Type());
    registerType (new SourceCodeDocument::Type());
}
```

`createGUIDocumentType()` は JUCE 7 の `jucer_JucerDocument.cpp` 内にあり、GUI Editor が開ける C++ ファイルかどうかを判定して `JucerDocumentEditor` を生成する。

重要な JUCE 7 側の関数:

- `JucerDocument::isValidJucerCppFile (const File&)`
- `JucerDocument::createForCppFile (Project*, const File&)`
- `createGUIDocumentType()`
- `JucerDocumentEditor`

復活時は、単に `Source/ComponentEditor` を戻すだけでなく、`OpenDocumentManager` に `createGUIDocumentType()` 登録を戻す必要がある。

## コマンド体系からの削除

JUCE 8 で `Source/Application/jucer_CommandIDs.h` から GUI Editor 関連コマンドが削除されている。

削除されたもの:

```cpp
enableGUIEditor = 0x300027,
addNewGUIFile   = 0x300200,
```

`addNewGUIFile` はプロジェクト内に新しい GUI Component ファイルを作るコマンド。`enableGUIEditor` は GUI Editor メニューを有効化する設定トグルに使われていた。

## メニューと設定の削除

JUCE 7 の `jucer_Application.cpp` には GUI Editor メニューが存在する。

確認できた JUCE 7 側の参照:

- メニュー名配列に `"GUI Editor"` が含まれる
- `isGUIEditorEnabled()` が false の場合は GUI Editor メニューを非表示
- `menuName == "GUI Editor"` の分岐
- `CommandIDs::enableGUIEditor` のメニュー項目
- `enableOrDisableGUIEditor()` の実行

JUCE 8 ではこれらの参照が消えている。

復活時は、設定トグルを戻すか、常時 GUI Editor 有効として簡略化するかを決める必要がある。まず最小復活なら、設定トグルまで戻すより `createGUIDocumentType()` を常時登録する方が小さい。

## 新規 GUI ファイル作成機能の削除

JUCE 7 の `ProjectContentComponent` には GUI ファイル作成の統合がある。

確認できた JUCE 7 側の参照:

- `ProjectContentComponent::addNewGUIFile()`
- `CommandIDs::addNewGUIFile`
- `getAllCommands()` に `addNewGUIFile`
- `perform()` の `addNewGUIFile` 分岐

JUCE 8 では `addNewGUIFile` コマンド自体が消えているため、新規作成 UI から GUI Editor 用ファイルを作る導線も消えている。

復活の段階分けとしては、まず既存 `.cpp` に埋め込まれた `JUCER_COMPONENT` メタデータを開けるようにし、その後で新規 GUI ファイル作成を戻すのがよい。

## ビルド設定からの削除

`CMakeLists.txt` では JUCE 7 にあった `Source/ComponentEditor` の `.cpp` 群が JUCE 8 で全て消えている。

JUCE 7 の CMake に含まれていた GUI Editor 関連 `.cpp`:

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

`Projucer.jucer`、Visual Studio project、Xcode project、Linux Makefile からも同様に外されている。

## それ以外の大きな Projucer 変更

GUI Editor 以外にも、JUCE 8 の Projucer には以下の変更がある。

- Visual Studio 2017 exporter/build files が削除され、Visual Studio 2026 が追加
- Code::Blocks exporter が削除
- ユーザーアカウント/ライセンス関連 UI が削除
- Apple Icon Composer 用の `Source/Assets/AppIcon.icon` が追加
- `JuceLibraryCode` に compile-time 分割用の generated include が追加
- `juce_gui_basics` / `juce_graphics` の分割コンパイル用ファイルが追加
- テンプレート類は ARA、Animated、Audio Component などで更新

これらは GUI Editor 復活の直接要件ではないが、JUCE 7 の `Projucer.jucer` や exporter 設定を丸ごと戻すと衝突する。

## 復活に必要そうな最小単位

最小復活の候補:

1. `JUCE-7.0.12/extras/Projucer/Source/ComponentEditor` を JUCE 8 側へ戻す。
2. JUCE 8 の `CMakeLists.txt` と `Projucer.jucer` に ComponentEditor の `.cpp` / `.h` を追加する。
3. `OpenDocumentManager` に `createGUIDocumentType()` 登録を戻す。
4. `CommandIDs::addNewGUIFile` と `ProjectContentComponent::addNewGUIFile()` を戻すか、まずは既存 GUI ファイルを開く機能だけに絞って後回しにする。
5. `jucer_Application.cpp` の GUI Editor メニュー/コマンドを戻すか、Document open 統合のみで始める。
6. JUCE 8 modules API 変更に合わせてコンパイルエラーを修正する。

## 予想されるコンパイル修正箇所

`modules` 差分から見て、GUI Editor コードで問題になりやすいのは以下。

- `Font::getStringWidth()` / `getStringWidthFloat()` / `getGlyphPositions()` の削除・deprecated
- `Typeface` / `CustomTypeface` 周辺
- `AlertWindow::show()` の戻り値挙動
- `Displays` の座標変換 API
- `TextEditor` 周辺の挙動・API 差
- `ComponentPeer` / `VBlankAttachment` / platform window 周辺

まず `Source/ComponentEditor` を戻してビルドすると、実際の修正対象は比較的はっきり出るはず。

## 結論

JUCE 8 の Projucer では GUI Editor は完全に削除されている。復活は「隠し機能の再有効化」ではなく、JUCE 7 の `Source/ComponentEditor` 86 ファイルと、以下の接続点を戻す作業になる。

- document type 登録: `OpenDocumentManager`
- コマンド ID: `enableGUIEditor`, `addNewGUIFile`
- メニュー/command handling: `jucer_Application.cpp`
- 新規 GUI ファイル作成: `ProjectContentComponent`
- build project への source 登録: `CMakeLists.txt`, `Projucer.jucer`, 各 exporter project

実装順としては、まず CMake ビルドだけを対象に `Source/ComponentEditor` と `OpenDocumentManager` 登録を戻し、既存 GUI Editor 対応 `.cpp` を開けるところまで持っていくのが現実的。
