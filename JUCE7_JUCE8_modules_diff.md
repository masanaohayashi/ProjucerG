# JUCE 7.0.12 と JUCE 8.0.13 の modules 差分メモ

調査対象:

- JUCE 7: `JUCE-7.0.12/modules`
- JUCE 8: `JUCE-8.0.13/modules`
- 参照資料: `JUCE-8.0.13/CHANGE_LIST.md`, `JUCE-8.0.13/BREAKING_CHANGES.md`
- 実差分確認: `diff -qr`, `git diff --no-index --stat`, `git diff --no-index --name-status`

この資料では Projucer 本体の差分は扱わない。ただし、`BREAKING_CHANGES.md` にある「GUI Editor が Projucer から削除された」という事実は、背景情報として末尾にだけ記載する。

## 全体像

JUCE 8 の `modules` は JUCE 7 からかなり大きく変わっている。

- モジュール数: 21 から 24 へ増加
- ファイル数: 2286 から 3070 へ増加
- `modules` 全体の diff 統計: 2741 files changed, 455164 insertions, 90663 deletions

ただし、この差分量の多くは外部ライブラリや SDK の同梱・更新によるもの。特に AAX SDK、VST3 SDK、QuickJS、Harfbuzz、SheenBidi、zlib、libjpeg などが大きい。JUCE API として重要なのは、以下の構造変更。

- 新規モジュールの追加
- JavaScript 実装の `juce_core` から `juce_javascript` への分離
- `juce_audio_processors_headless` の追加と、GUI 依存を避けた AudioProcessor 系 API の分離
- `juce_graphics` / `juce_gui_basics` のテキスト描画・Unicode・フォント処理の大幅変更
- `juce_gui_extra` の WebView ベース UI サポート強化
- MIDI 2.0 / UMP 関連の拡充
- Direct2D レンダラ追加に伴う Windows 描画系の大幅変更

## 追加されたモジュール

JUCE 8.0.13 で `modules` 直下に追加されたモジュールは以下。

| モジュール | 概要 | 主な依存 |
| --- | --- | --- |
| `juce_animation` | Animator、Easing、VBlank ベース更新などのアニメーション API | `juce_gui_basics` |
| `juce_audio_processors_headless` | UI を含まない AudioProcessor / plugin hosting 基盤 | `juce_audio_basics`, `juce_events` |
| `juce_javascript` | JavaScript エンジン。JUCE 7 では `juce_core` 内にあった機能を独立モジュール化 | `juce_core` |

削除された `modules` 直下のモジュールは今回の比較では見つからなかった。

## `juce_javascript`: JavaScript の独立モジュール化

JUCE 7 では JavaScript 関連は `juce_core/javascript/juce_Javascript.h` に含まれていた。JUCE 8 では `juce_javascript` モジュールが追加され、`JavascriptEngine`, `JSCursor`, `JSObject` がそこへ移動している。

実 diff でも以下が確認できる。

- `JUCE-7.0.12/modules/juce_core/javascript/juce_Javascript.h`
- `JUCE-8.0.13/modules/juce_javascript/javascript/juce_JavascriptEngine.h`
- `JUCE-8.0.13/modules/juce_javascript/javascript/juce_JSObject.*`
- `JUCE-8.0.13/modules/juce_javascript/javascript/juce_JSCursor.*`
- `JUCE-8.0.13/modules/juce_javascript/choc/javascript/choc_javascript_QuickJS.h`

`BREAKING_CHANGES.md` によると、既存プロジェクトで `JavascriptEngine`, `JSCursor`, `JSObject` を使っている場合は `juce_javascript` を明示的に追加する必要がある。さらに内部実装が QuickJS ベースに置き換わっており、従来通っていた JavaScript 片がより厳密な仕様解釈で失敗する可能性がある。

Projucer GUI Editor を復活させる観点では、もし GUI Editor 側が古い `JavascriptEngine` や `DynamicObject` 前提のコードを持っているなら、単純コピーではビルドが通らない可能性がある。

## `juce_audio_processors_headless`: AudioProcessor の GUI 依存分離

JUCE 8.0.11 で `juce_audio_processors_headless` が追加されている。ヘッダを見ると、AudioProcessor、AudioPluginInstance、AudioProcessorGraph、AudioPluginFormat、各種 parameter、VST/AU/LV2/LADSPA/ARA hosting の UI 非依存部分を持つモジュールになっている。

重要な意図は `BREAKING_CHANGES.md` に明記されている。

- `juce_audio_processors_headless` と `juce_graphics` の依存を切る
- headless な AudioProcessor 処理で `juce_graphics` を含めずに済むようにする
- `AudioProcessor::TrackProperties::colour` は削除され、置き換えられている

関連する API 変更も多い。

- `AudioPluginInstance::getPlatformSpecificData()` は削除
- 代替として `getVSTClient()`, `getVST3Client()`, `getAudioUnitClient()`, `getARAClient()` を使う
- `ExtensionsVisitor` は削除
- `VSTPluginFormatHeadless` 系の一部関数は削除またはシグネチャ変更
- `AudioPluginFormatManager::addDefaultFormats()` は削除

GUI Editor 復活そのものには直接関係しない可能性が高いが、Projucer のビルド単位や module dependency を触る場合、この分離は無視できない。

## `juce_animation`: 新しいアニメーション API

`juce_animation` は JUCE 8.0.0 で追加された新規モジュール。実ファイルとしては以下が追加されている。

- `animation/juce_Animator.*`
- `animation/juce_AnimatorSetBuilder.*`
- `animation/juce_AnimatorUpdater.*`
- `animation/juce_Easings.*`
- `animation/juce_ValueAnimatorBuilder.*`
- `animation/juce_VBlankAnimatorUpdater.h`

GUI Editor 復活に必須とは限らないが、JUCE 8 の GUI 層では VBlank、Component repaint、アニメーション周辺の設計が JUCE 7 より進んでいる。古い GUI Editor の描画・ドラッグ・選択枠などを JUCE 8 に戻す場合、既存の `Timer`/`ComponentAnimator` 前提コードはそのままでも動く可能性があるが、JUCE 8 側の新しい GUI 更新系とは別物として扱う必要がある。

## `juce_graphics`: Unicode、フォント、テキスト描画の大幅変更

JUCE 8 では `juce_graphics` の差分が大きい。実 diff 上では以下が目立つ。

- Harfbuzz 関連ファイルの大幅更新
- SheenBidi 関連ファイルの更新
- `fonts`, `native`, `unicode`, `contexts`, `images` 配下の広範な変更
- PostScript renderer の削除
- Direct2D renderer 追加に関連する Windows 描画変更

`BREAKING_CHANGES.md` 上の重要な API 変更:

- `Typeface::getStringWidth()`, `Typeface::getGlyphPositions()`, `Typeface::getEdgeTableForGlyph()`, `Typeface::applyVerticalHintingTransform()` が削除
- `Font::getStringWidth()`, `Font::getStringWidthFloat()` が削除または deprecated 化
- `Font::getGlyphPositions()` が削除
- `CustomTypeface` が削除
- `Typeface::getOutlineForGlyph()` は戻り値が `bool` から `void` に変更
- `Typeface` の複数 API に `TypefaceMetricsKind` / `FontOptions` 系の考え方が入った
- 文字幅取得は `GlyphArrangement` や `TextLayout` を使う方向へ誘導されている

GUI Editor の復活では、古いエディタ UI が `Font::getStringWidth()` などを使っている場合、JUCE 8 ではそのままコンパイルできない。ラベル、プロパティパネル、コード生成プレビューなど、文字幅計算を行う箇所は移植時に重点確認が必要。

## `juce_gui_basics`: Component、TextEditor、描画、入力、アクセシビリティ

`juce_gui_basics` は JUCE 8 で広く変更されている。実 diff では特に以下が目立つ。

- `widgets/juce_TextEditor.cpp` の大幅変更
- `widgets/juce_TextEditorModel.cpp` の追加
- `components`, `windows`, `native`, `accessibility`, `layout`, `mouse`, `filebrowser` の広範な更新
- `AlertWindow::show()` の戻り値挙動変更
- `Displays` の物理座標・論理座標まわりの API deprecated 化
- `ComponentPeer`, `VBlankAttachment`, platform native peer 周辺の変更

`CHANGE_LIST.md` でも JUCE 8 系の見出しとして以下が並ぶ。

- Direct2D renderer の追加
- Component painting performance の改善
- Windows rendering performance の改善
- iOS input support の改善
- TextEditor layout / Unicode handling の改善
- Accessibility navigation の変更

GUI Editor 復活で影響しやすいポイント:

- 古い GUI Editor のコンポーネント選択・ドラッグ・リサイズ処理が、JUCE 8 の座標系や mouse event 変更の影響を受ける可能性
- `AlertWindow::show()` に依存する同期ダイアログ処理がある場合、戻り値の意味が変わる可能性
- `TextEditor` を多用している場合、内部実装が変わっているため挙動差に注意

## `juce_gui_extra`: WebView ベース UI の強化

JUCE 8.0.0 の主要機能として「WebView based UIs」が追加されている。実 diff では `juce_gui_extra` に以下が追加・変更されている。

- `misc/juce_WebControlRelays.*`
- `misc/juce_WebControlParameterIndexReceiver.h`
- `detail/juce_WebControlRelayEvents.h`
- `native/javascript/index.js`
- `native/javascript/check_native_interop.js`
- `native/juce_WebBrowserComponent_*` の広範な変更
- Android WebView 関連 Java ファイルの再構成

`WindowsWebView2WebBrowserComponent` は削除され、WebView2 設定は `WebBrowserComponent::Options::WinWebView2` 側へ寄せられている。

GUI Editor 自体は古典的な JUCE Components ベースの機能なので WebView 強化と直接は関係しない。ただし Projucer 内で `juce_gui_extra` の依存や初期化まわりが変わっている場合、ビルド設定に影響する可能性がある。

## MIDI / UMP / MIDI-CI 関連

JUCE 7.0.9 で MIDI-CI が入り、JUCE 8 では MIDI 2.0 / UMP 関連がさらに拡張されている。

実 diff の目立つ追加:

- `juce_audio_devices/midi_io/ump/*`
- `juce_audio_basics/midi/ump/juce_UMPStringUtils.*`
- `juce_audio_basics/midi/ump/juce_UMPDeviceInfo.cpp`
- `juce_audio_basics/midi/ump/juce_UMPFactory.cpp`
- `juce_midi_ci/ci/juce_CIMessages.cpp`
- `juce_midi_ci/ci/juce_CIChannelAddress.cpp`
- `juce_midi_ci/ci/juce_CISubscription.cpp`

また `juce_audio_devices/native/juce_Midi_*` の差分も大きい。GUI Editor 復活には直接関係しない可能性が高いが、Projucer 全体をビルドする際の module compile time や platform code の依存には影響する。

## `juce_audio_plugin_client`: AAX SDK の同梱

JUCE 8.0.0 で AAX SDK が同梱されている。`modules/juce_audio_plugin_client/AAX/SDK` 配下に大量のファイルが追加され、diff 統計上も大きな割合を占める。

GUI Editor 復活とは直接関係しないが、JUCE 7 と JUCE 8 の `modules` diff を見る際に差分量を大きく見せている主要因の一つ。

## `juce_audio_devices`: ASIO SDK、MIDI、platform audio

JUCE 8 では `juce_audio_devices` も大きく変わっている。

実 diff の目立つ点:

- ASIO SDK ソースが `modules/juce_audio_devices/native/asio` に追加
- `juce_Bela_linux.cpp` は削除
- JACK 実装ファイル名が `juce_JackAudio_linux.cpp` から `juce_JackAudio.cpp` へ変更
- Windows WASAPI、CoreAudio、CoreMIDI、Android Oboe/OpenSL、ALSA などの platform 実装に広範な差分
- UMP MIDI device API の追加

`BREAKING_CHANGES.md` では `JUCE_WASAPI_EXCLUSIVE` の削除など、オーディオデバイス初期化 API に関する破壊的変更も記載されている。

## `juce_audio_formats`: writer API と WAV など

`juce_audio_formats` では `juce_AudioFormatWriterOptions.h` が追加され、古い `createWriterFor` overload が non-virtual / deprecated になっている。WAV では 32-bit int WAV サポートも追加されている。

GUI Editor には直接関係しない可能性が高い。

## API 破壊的変更のうち、GUI Editor 復活で注意したいもの

古い JUCE 7 の GUI Editor コードを JUCE 8 の Projucer に戻す前提で、`modules` 側だけから見て注意すべき候補。

| 領域 | 変更 | 移植時の注意 |
| --- | --- | --- |
| Font / Typeface | 文字幅・glyph 関連 API の削除・変更 | GUI Editor のラベル配置、プロパティ表示、コード生成画面で古い API 使用があれば修正必須 |
| JavaScript | `juce_javascript` へ分離、QuickJS 化 | 旧コードが JavaScript を使うなら module 追加と API 変更対応が必要 |
| AlertWindow | `show()` の戻り値挙動変更 | 同期ダイアログの分岐確認が必要 |
| Displays / 座標 | `Point<int>` 系 display 変換 API deprecated | マルチディスプレイや HiDPI 周辺の座標処理確認が必要 |
| ComponentPeer / VBlank | platform peer と描画更新系の変更 | エディタのドラッグ・再描画・選択枠表示で挙動差が出る可能性 |
| TextEditor | 実装が大きく変更 | プロパティ編集欄などの細かい挙動確認が必要 |
| WebBrowserComponent | WebView API 再構成 | GUI Editor 直接ではないが Projucer 全体依存で影響の可能性 |

## Projucer GUI Editor についての背景情報

`BREAKING_CHANGES.md` には「The GUI Editor has been removed from the Projucer」と明記されている。理由は、長期間 deprecated で、bugfix や maintenance が行われていなかったため。ここでは Projucer 本体の diff はまだ見ていないため、削除されたファイルや UI 統合点は未調査。

## 次に調べるべきこと

次段階で Projucer 本体を調べるなら、以下の順番がよい。

1. JUCE 7 の Projucer 内で GUI Editor 関連ファイルを特定する。
2. JUCE 8 で削除されたファイル、削除された menu/command/project item integration を確認する。
3. GUI Editor コードが使っている `modules` API を列挙し、上記の破壊的変更に当たる箇所を先に潰す。
4. まずビルドだけ通すための最小復帰、その後ランタイム挙動の修正、という順で分ける。
