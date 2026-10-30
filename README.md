# DxStudio — DirectX 12 ゲームエンジンの試作

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

`test.cmd` はシーンの保存・読込とコンポーネント、DirectX 12 の描画・選択・カメラ・リサイズ・書き出しを検証します。生成物は `build-direct` に入ります。CMake を使う場合は `CMakeLists.txt` も利用できます。
