# DxStudio — DirectX 12 / 11 / 9 ゲームエンジンの試作

Windows 向けの小さな 2D / 3D ゲーム制作環境です。DirectX 12 と DirectXMath（`XM*`）を使用し、シーン編集、外部 HLSL、自作 C++ コンポーネント、実行ファイルとデータの書き出しを試せます。

## はじめに

- Windows 10/11、DirectX 12 対応 GPU、Windows SDK、Visual Studio の「C++ によるデスクトップ開発」が必要です。
- このフォルダーにある `DxStudio.exe` を起動します。標準プロジェクトは `project/DxStudio.dxproject` です。`File > Open project...` で別の `.dxproject` を開き、複数のプロジェクトを切り替えられます。
- ソースを変更した場合は `build.cmd` を実行します。Visual Studio の C++ ツールを探し、C++20でエディターとゲームをビルドします。
- エディターは英語 UI です。`+ Cube` / `+ Quad`、または Scene と Hierarchy の右クリックメニューで作成します。左の Hierarchy または Scene で選択すると、オレンジ色の枠で表示されます。Inspector は Transform、Rendering、Components に折りたたみでき、回転は度、色は RGBA です。値を入力して Enter または `Apply changes` で反映します。数値欄は上下キーで増減し、Shift を加えると増分が10倍になります。
- Scene 上で左クリックするとオブジェクトを選択します。右ボタンを押しながらマウスで向きを変え、W/S、A/D、Q/E で前後・左右・上下へ移動できます。Shift は高速移動です。Alt + 右ドラッグは選択物を中心に回転、中ドラッグは平行移動、ホイールは拡大・縮小です。右クリックだけなら生成・削除などのメニューが開きます。
- Scene 上部の `Persp` / `Ortho` はEditor専用カメラです。平面（Quad）も立方体と同じ3D空間にあり、透視投影、奥行き、XYZ回転の影響を受けます。ゲーム側はEntityへアタッチした `Camera` コンポーネントを使います。Cameraは複数配置でき、最初のActiveカメラがPlayと書き出しゲームで使用されます。
- Scene の空いている場所から左ドラッグすると矩形の範囲選択ができます。Shift は追加、Ctrl は選択の切り替えです。Hierarchy でも Shift / Ctrl で複数選択できます。複数選択時は、最後のアクティブなオブジェクトを基準にギズモで一括操作します。Inspector の個別値とコンポーネント操作は単一選択時のみ編集できます。範囲選択は画面上の投影範囲による判定で、重なりや遮蔽を考慮したピクセル単位の判定ではありません。
- 選択したオブジェクトにはギズモが表示されます。上部の `Move`・`Rotate`・`Scale` または W・E・R キーで切り替え、色付きの軸や回転リングを左ドラッグします。赤・緑・青はそれぞれ X・Y・Z 軸です。Move の小さい四角は平面移動、Scale の中央は均一拡縮です。Shift を押しながら操作すると、移動は1単位、回転は15度、拡縮は0.1単位でスナップします。ドラッグ中の Esc で操作前に戻せます。F で選択物に視点を合わせます。
- Hierarchy、Scene、Inspector、Project は右上の `Float`、`_`、`x` で独立ウィンドウ化、縮小、非表示にできます。Float後はタイトルバーをマウスで移動・自由リサイズできます。Dock中は、パネル境界に現れる9px幅の専用Splitterをドラッグして、Hierarchy幅、Inspector幅、Project高さを変更できます。左右／上下のリサイズカーソルが表示され、パネルが境界を覆っていても操作できます。狭い幅ではヘッダーボタンをアイコン表示へ自動縮小し、Inspectorは縦スクロールします。
- `Window > Save current layout` と `Load saved layout` は、Dock幅/高さと各パネルのDock・Float・非表示・縮小状態をプロジェクト内の `EditorLayout.dxl` へ保存・復元します。`Reset layout` は既定配置へ戻します。
- 保存・読込・エディタ内Play・Android APK・書き出し・シェーダー再読込は上部ツールバーにまとめています。`Play`（Ctrl+P）を押すと、現在のシーンを複製してプロジェクトのC++コンポーネントをエディタ内で実行します。`Stop` で実行前の編集状態へ戻るため、実行中のTransform変更は保存されません。`Save` / `Load` は選択中プロジェクトの `scene.dxs` を使います。
- `File > New project...` で親フォルダーとプロジェクト名を指定すると、その名前の専用フォルダーを自動作成します。`Scripts`、`Assets/Models`、`Assets/Materials`、`Assets/Textures`、`Assets/Animations`、シーン、シェーダー、`.dxproject`、Visual Studioソリューションはすべてその中へ格納され、Main Cameraも作成されます。
- Hierarchy の項目を別の項目へドラッグすると親子関係を設定できます。空白へドロップするとルートへ戻ります。親変更時は見た目のワールド座標を維持し、循環する親子関係は拒否します。
- `Export` で出力先フォルダーを選びます。画面上のシーンと最後にビルドしたゲームから `Game.exe`、`scene.dxs`、`Default.vs.cso`、`Default.ps.cso` の 4 ファイルを出力します。まとめて配布してください。同名の出力ファイルがある場合は上書きを確認します。ゲームは Esc で終了します。

## 新機能（グラフィックスAPI切替・Unity/UE風UI・デリゲート・コルーチン）

### DirectX 12 / 11 / 9 の切り替え

- エディター: ツールバー右の `DX12 ▾` バッジ、または `Edit > Project Settings > Graphics API` で DirectX 12 / 11 / 9 を選びます。Scene ビューはその場で作り直され、選択はプロジェクトの `ProjectSettings.dxsettings` に保存されます。
- 書き出したゲームも同じ設定を使います（`ProjectSettings.dxsettings` が `Game.exe` の隣にコピーされます）。起動引数 `Game.exe -dx11` / `-dx9` / `-dx12` で上書きできます。
- 選んだAPIが使えない環境では 12 → 11 → 9 の順に自動でフォールバックし、Console に理由を出します。
- DirectX 9 では `Default.hlsl` を Shader Model 3（`vs_3_0` / `ps_3_0`）としてコンパイルします。SM3 で通らないシェーダーの場合は組み込みの単色シェーダーで描画し、警告を出します。書き出し時には `Default9.vs.cso` / `Default9.ps.cso` も生成されます。
- 構成: `Renderer`（ピック・ギズモ・描画順などAPIに依存しない処理）→ `graphics::IGraphicsDevice`（`src/Graphics/GraphicsDevice.h`）→ `DeviceD3D12.cpp` / `DeviceD3D11.cpp` / `DeviceD3D9.cpp`。

### Unity / Unreal Engine 風エディターUI

Unity 2022 の Dark スキン配色（パネル `#383838`、タブ帯 `#282828`、ツールバー `#191919`、選択 `#2C5D87`）を基準に、UE5 の要素（アクティブツールの青、X/Y/Z 入力欄の色付きバー）を組み合わせています。

- ダークテーマのメニューバー（File / Edit / Assets / GameObject / Component / Build / Window / Help、Alt+頭文字で開く）、ダークタイトルバー、ダークのポップアップメニューとスクロールバー。
- メインツールバー: 左に Save / Load、中央に Play / Pause / Step、右に Shader / Export / Android / グラフィックスAPI / Layout。ホバー表示とツールチップ付き。
- Pause（`Ctrl+Shift+P`）と Step（`Ctrl+Alt+P`、1/60秒だけ進める）を追加しました。
- ドックのタブ見出し（アイコン＋タイトル、フォーカス時は青いライン）。下部は `Project | Console` のタブ切り替えです。
- Hierarchy: シーン名の行、折りたたみ可能なツリー（▼/▶ をクリック）、種類ごとのアイコン（Cube / Quad / Camera / Model / 空オブジェクト）、スクリプト付きの印。非アクティブなオブジェクトは灰色で表示します。
- Inspector: Unity のコンポーネント見出し（折りたたみ・アイコン・⋮）、Tag / Layer の欄、X/Y/Z の色付きバー、プロパティ行の形式で表示するコンポーネント値、青い `Add Component` / `Apply changes` ボタン。
- Scene ビュー: アイコンのツール切り替え（Move / Rotate / Scale）、右上に X/Y/Z の向きを示すギズモ。`Scene | Game` タブで Game ビュー（最初の有効な Camera から見た、グリッドやギズモのない表示）に切り替えられます。Unity と同様に Play 中は自動で Game ビューになり、Stop で Scene に戻ります。
- Hierarchy の行はマウスを乗せると強調表示され、親子付けのドラッグ中はドロップ先が青枠で示されます。
- Console（`Ctrl+Shift+C`）: `dxs::Debug::Log / LogWarning / LogError` の出力を、レベル別アイコン・件数・フィルター・Collapse・Clear・コピー付きで表示します。
- ステータスバー: 最後のメッセージと、使用中のAPI・FPS・Play状態。
- 実装: `src/EditorTheme.h/.cpp`（配色・アンチエイリアス付きベクターアイコン・ボタン描画）と `src/ConsolePanel.cpp`。

### デリゲートとコールバックインターフェース（`src/Core/Delegate.h`, `src/Core/EngineCallbacks.h`）

```cpp
dxs::MulticastDelegate<int> onScore;                 // UE の Multicast Delegate / C# の event に相当
auto handle = onScore.AddLambda([](int nScore) { /* ... */ });
onScore.AddRaw(&player, &CPlayer::OnScore);          // メンバー関数
onScore.Broadcast(10);
onScore.Remove(handle);
dxs::ScopedDelegateHandle scoped(onScore, onScore += [](int) {}); // スコープを抜けると自動で解除

// エンジンイベントは2通りで受け取れます
auto h = dxs::EngineEvents::Get().onSceneLoaded.AddLambda([](dxs::Scene& scene) { /* ... */ });

class CMyTool final : public dxs::IPlayModeCallbacks, public dxs::ISceneCallbacks {
    void OnEnterPlayMode(dxs::Scene&) override { /* ... */ }
    void OnSceneSaved(const dxs::Scene&) override { /* ... */ }
};
CMyTool tool;
dxs::CallbackRegistration registration = dxs::RegisterCallbacks(tool); // 実装したインターフェースを全て購読
```

インターフェース: `ISceneCallbacks`（読込・保存）、`IFrameCallbacks`（Update の前後）、`IComponentCallbacks`（Start・アタッチ・デタッチ）、`IPlayModeCallbacks`（Play・Stop・Pause）、`ISelectionCallbacks`、`IGraphicsCallbacks`（API変更）。Broadcast 中に登録や解除をしても安全です。

### コルーチン（`src/Core/Coroutine.h`、C++20 `<coroutine>`）

Unity C# のコルーチンと同じ感覚で書けます。`StartCoroutine` は最初の `co_yield` までその場で実行し、その後は毎フレーム `Update` の後に再開します（Unity と同じ順序）。

| Unity C# | DxStudio C++ |
| --- | --- |
| `IEnumerator Foo()` | `dxs::Coroutine Foo()` |
| `yield return null;` | `co_yield nullptr;`（または `co_yield dxs::NextFrame;`） |
| `yield return new WaitForSeconds(1);` | `co_yield dxs::WaitForSeconds(1.0f);` |
| `WaitForSecondsRealtime` | `dxs::WaitForSecondsRealtime` |
| `WaitUntil` / `WaitWhile` | `dxs::WaitUntil([&]{ ... })` / `dxs::WaitWhile(...)` |
| `yield return StartCoroutine(Sub());` | `co_yield Sub();`（Sub の完了を待つ） |
| `yield break;` | `co_return;` |
| `StartCoroutine` / `StopCoroutine` / `StopAllCoroutines` | `Component` の同名メンバー関数 |
| `Time.deltaTime` / `Time.timeScale` | `dxs::Time::DeltaTime()` / `dxs::Time::SetTimeScale()` |

サンプルとして `project/Scripts/PulseController.cpp` を追加しました（拡大→待機→縮小→点滅を繰り返します）。コルーチン内で例外が起きると、そのコルーチンだけを止めて Console にエラーを出します。`Component` には `LateUpdate` も追加しました。

### 可読性と命名規則

- 約2,900行あった `Editor.cpp` を役割ごとに分割しました: `EditorWindow.h`（エディターの状態）、`Editor.cpp`（メインウィンドウと起動）、`EditorPanels.cpp`（Viewport・Hierarchy・ドックパネルのウィンドウプロシージャ）、`EditorChrome.cpp`（テーマ・メニュー・行の描画）、`EditorLayout.cpp`（ドッキング配置）、`EditorActions.cpp`（コマンド・Play Mode・グラフィックスAPI・プロジェクト）、`EditorInspector.cpp`（選択・Hierarchy・Inspector）。コマンドIDは `EditorCommands.h`、ファイルダイアログや文字列変換は `EditorUtility.h/.cpp` にあります。`.clang-format` を追加し、1行に複数の文を詰め込んでいたコードを整形しています。
- 新しく書いたコードは cpp_TKG の命名規則に従っています（メンバー `m_` 接頭辞、型接頭辞 `n` / `f` / `b` / `p` / `v` / `s` / `d` / `a`、`constexpr` 定数と列挙子は UPPER_CASE、関数は PascalCase）。命名規則にある「既存の違反を一括修正しない」方針に従い、ユーザーのスクリプトが使っている公開API（`entity.draw.position` など）の名前は変えていません。
- ビルドに使うソースの一覧は `build-sources.cmd` にまとめ、`build-direct.cmd` / `build-debug.cmd` / `test.cmd` から共通で使います。

## Project ウィンドウ

下部の Project は `project` フォルダーを表示します。左のツリーでフォルダーを選び、右の一覧で内容を確認できます。検索欄は現在のフォルダーの名前を絞り込み、`Refresh` は表示を更新します。`Import...` は選択中フォルダーへファイルをコピーし、同名ファイルは上書きしません。右クリックの `Create > C++ Component...` は `Scripts` に、必要な `Engine.h` と自動登録コードが記入済みの `.h` / `.cpp` を生成します。生成後は `build.cmd` で再ビルドしてください。

ソースをダブルクリックまたは Enter で開けます。Visual Studio利用時はプロジェクト直下の `DxStudioProject.sln` / `.vcxproj` を自動更新してから開くため、`Engine.h`、基底クラス、依存クラスへ「定義へ移動」でジャンプできます。C++20とエンジンのインクルードパスもIntelliSenseへ設定されます。

Project へのインポートはファイルのコピーです。コンポーネントの `.cpp` は `Scripts` 直下へ置き、登録と再ビルドが必要です。

`GameObject > Place X / FBX model...` はモデルを `Assets/Models` へコピーしてシーンへ配置します。テキスト形式のDirectX `.x` と、組み込みのufbxによるASCII/バイナリFBXに対応します。FBXはポリゴンの三角形化、ノード変換、スキニング、最初のアニメーションスタックのPlay中再生に対応します。InspectorのRenderingには `.dxmat` マテリアルとテクスチャのプロジェクト相対パスを保存できます。マテリアルのBase Colorは描画色へ反映されます。

画面と操作の設計は Unity の公式資料の [Project ウィンドウ](https://docs.unity3d.com/2022.3/Documentation/Manual/ProjectView.html)、[オブジェクトの配置](https://docs.unity3d.com/2022.3/Documentation/Manual/PositioningGameObjects.html)、[Scene の選択](https://docs.unity3d.com/6000.0/Documentation/Manual/SelectGameObjects.html)を参考にしています。

## C++ コンポーネント

Inspector は位置・回転・スケールの `TRANSFORM`、色とアセット参照の `RENDERING / COLOR`、スクリプトの `COMPONENTS` に分かれています。`COMPONENTS` の一覧から登録済みの種類を選び、`Attach` で追加します。アタッチ済みの項目を選んで `Remove`、または右クリックの `Remove Component` でデタッチできます。デタッチ時はシーン保存済みの公開値とPlay用インスタンスも破棄されます。右クリックの `Move Up` / `Move Down` で実行順を変更できます。複数のコンポーネントを同じオブジェクトに追加でき、Inspector順に実行します。同じ種類を同じオブジェクトに重複して追加することはできません。

Unity風ショートカットとして、`Ctrl+D` は選択オブジェクトを複製、`F2` は名前編集、`Delete` は選択削除、`F` はFrame Selected、`W` / `E` / `R` はMove / Rotate / Scale、`Ctrl+P` はPlay / Stopです。

`project/Scripts/MyComponent.cpp` の `Spin` がサンプルです。対象オブジェクトに `Spin` をアタッチすると、エディターと書き出したゲームで回転します。

1. `project/Scripts` に独自の `.h` / `.cpp` を置き、`dxs::Component` を継承したクラスを作成します。初期化は `Start(dxs::Entity&)`、毎フレーム処理は `Update(dxs::Entity&, float)` に記述します。
2. `.cpp` の末尾で `DXS_REGISTER_COMPONENT(クラス名)` を使います。各オブジェクトにクラスのインスタンスが生成されるため、メンバー変数で状態を保持できます。Project の `Create > C++ Component...` はこの形式を自動生成します。
3. `build.cmd` を再実行します。`project/Scripts` 直下の `.cpp` がゲームにコンパイルされます。
4. エディターを再起動し、`COMPONENTS` の選択欄から登録名を選んで `Attach` し、Export します。未登録名があるとゲーム起動時にエラーを表示します。

クラスの `EditorFields()` から `dxs::EditField("表示名", メンバー変数)` を返すと、Inspectorでfloat、int、bool、stringを編集できます。publicメンバーだけでなく、クラス内部から明示的に返したprivateメンバーもEditor公開値として扱えます。値はシーンへ保存され、Play中は実行中インスタンスの現在値が表示されます。`BoxController` がpublic/private両方の例です。

スクリプト追加後はエディターとゲームを両方ビルドする必要があります。DLL の動的読み込みはありません。旧シーン形式 v1～v6 も読み込めますが、保存するとコンポーネント値・アセット参照対応の v7 形式になります。

描画は `IRender` インターフェースで振る舞いを分離しています。組み込みの `PolygonRenderer`、`ModelRenderer`、2D向け `Image` は `IRender` を継承し、C++20 conceptと`static_assert`で継承関係をコンパイル時検査します。InspectorのComponentsからレンダラーを外したEntityは、Transform・親子関係・動作コンポーネントを維持したまま描画されません。

## 外部シェーダー

`project/Default.hlsl` を任意のエディターで編集し、`Reload Shader` を押します。コンパイルエラーはダイアログに表示されます。書き出し時にコンパイル済みのシェーダーを生成するため、配布先に HLSL や C++ のソースは必要ありません。

現行の描画パイプラインでは `VSMain`（`vs_5_0`）と `PSMain`（`ps_5_0`）、`POSITION` の float3 頂点入力、`b0` の `row_major float4x4 mvp` と `float4 color` を使用します。このインターフェースは維持してください。`#include` は書き出し時に解決されます。

## 現在の範囲

これは Unity 風の配置を持つ初期プロトタイプです。色付きの平面・立方体、X/FBXモデル、基本Transform、親子階層、複数プロジェクト、Play Mode、マテリアル/テクスチャ参照管理に対応しています。既定背景は空色のプロシージャル・スカイです（キューブマップ画像ではありません）。現段階ではテクスチャ画像のDirectX 12サンプリング、物理、音声、汎用入力API、透明合成は未実装です。GPU同期は毎フレーム待機する簡易実装です。

Windows向けは DirectX 12 の x64 実行ファイル、Android向けは OpenGL ES 3 のデバッグAPKを生成できます。ツールバーの `Play` はエディタ内動作確認、`Android` は arm64-v8a / x86_64 APK生成です。PDB付きWindowsデバッグ実行ファイルが必要な場合は `build-debug.cmd` を利用できます。Android SDK/NDK、Gradle、互換JDKが必要です。実装範囲は [docs/PLATFORMS.md](docs/PLATFORMS.md) に記載しています。

書き出したゲームにも DirectX 12 と `D3DCompiler_47.dll` が利用できる Windows 環境が必要です。C++ ランタイムは静的リンクしています。ゲームの実行に Visual Studio や C++ / HLSL ソースは不要です。4 ファイルを同じフォルダーに置いてください。既存フォルダーの無関係なファイルや古い HLSL は自動削除されないため、ソースを含めず配布したい場合は空のフォルダーに書き出してください。

## GitHub で管理する場合

`.gitignore` は用意済みです。GitHub 上のリポジトリ作成・認証・公開は行っていません。個別のリポジトリとして管理する場合、このフォルダーで `git init` し、GitHub で作成したリポジトリの URL を `git remote add origin <URL>` で登録できます。親フォルダーにも Git リポジトリがある環境では、入れ子のリポジトリになるため運用を決めてから実行してください。プッシュ前に公開範囲とコミット内容を確認してください。

## テスト

`test.cmd` はシーンの保存・読込とコンポーネント、デリゲート・コールバック・コルーチン、DirectX 12 / 11 / 9 の描画・選択・カメラ・リサイズ・書き出しを検証します。生成物は `build-direct` に入ります。CMake を使う場合は `CMakeLists.txt` も利用できます。
