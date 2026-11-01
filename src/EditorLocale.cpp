//==================================================================================
//
// エディターの表示言語（英語 / 日本語）のソースファイル [EditorLocale.cpp]
//
// 翻訳は「英語の文字列 → 日本語の文字列」の表で行う。英語がそのまま翻訳キーに
// なるため、コード側は今まで通り英語で書き、表示する所で Tr() を通すだけでよい。
// 新しい文字列を日本語化するときは JAPANESE_TEXT に 1 行追加する。
//
//==================================================================================

//**********************************************************************************
// *** インクルードファイル ***
//**********************************************************************************
#include "EditorLocale.h"

#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_map>

namespace dxs::editor {
namespace {

//**********************************************************************************
// *** 定数宣言 ***
//**********************************************************************************
constexpr const wchar_t* PREFERENCES_FILE = L"EditorPreferences.txt"; // 設定ファイル名
constexpr const char* LANGUAGE_KEY = "Language";                      // 設定ファイルのキー

//**********************************************************************************
// *** 日本語訳の表（英語 → 日本語） ***
//**********************************************************************************
// ・キーは画面に出す英語の文字列と完全に一致させる（メニューの "\tCtrl+S" は除く）。
// ・末尾が ": " や " " のキーは「前方一致」でも使われる
//   （例: "Rendering failed: " + 詳細 → "描画に失敗しました: " + 詳細）。
struct TranslationPair {
    const wchar_t* pEnglish;  // 英語（翻訳キー）
    const wchar_t* pJapanese; // 日本語
};

constexpr TranslationPair JAPANESE_TEXT[] = {
    // ---- メインメニュー / ツールバー ----
    {L"File", L"ファイル"},
    {L"Edit", L"編集"},
    {L"Assets", L"アセット"},
    {L"GameObject", L"ゲームオブジェクト"},
    {L"Component", L"コンポーネント"},
    {L"Build", L"ビルド"},
    {L"Window", L"ウィンドウ"},
    {L"Help", L"ヘルプ"},
    {L"Save", L"保存"},
    {L"Load", L"読み込み"},
    {L"Shader", L"シェーダー"},
    {L"Export", L"書き出し"},
    {L"Android", L"Android"},
    {L"Layout", L"レイアウト"},
    {L"Play", L"再生"},
    {L"Pause", L"一時停止"},
    {L"Step", L"コマ送り"},
    {L"Stop", L"停止"},
    {L"Exit", L"終了"},
    {L"Save scene", L"シーンを保存"},
    {L"Reload scene", L"シーンを再読み込み"},
    {L"Reload scene from disk", L"ディスクからシーンを再読み込み"},
    {L"New project...", L"新規プロジェクト..."},
    {L"Open project...", L"プロジェクトを開く..."},
    {L"Save project file as...", L"名前を付けてプロジェクトファイルを保存..."},
    {L"Export Windows game...", L"Windows 用ゲームを書き出し..."},
    {L"Build and export Windows game...", L"Windows 用ゲームをビルドして書き出し..."},
    {L"Build Android Debug APK", L"Android デバッグ APK をビルド"},
    {L"Cancel build", L"ビルドを中止"},
    {L"Play in Editor", L"エディター内で再生"},
    {L"Build platforms...", L"ビルド対象プラットフォーム..."},
    {L"Build platforms", L"ビルド対象プラットフォーム"},
    {L"Apply Inspector Changes", L"インスペクターの変更を適用"},
    {L"Apply changes", L"変更を適用"},
    {L"Copy", L"コピー"},
    {L"Duplicate", L"複製"},
    {L"Delete", L"削除"},
    {L"Delete selection", L"選択を削除"},
    {L"Delete objects", L"オブジェクトを削除"},
    {L"Frame selection", L"選択をフレームに収める"},
    {L"Frame Selected", L"選択をフレームに収める"},
    {L"Project Settings", L"プロジェクト設定"},
    {L"Graphics API", L"グラフィックス API"},
    {L"Preferences", L"環境設定"},
    {L"Language", L"言語"},
    {L"English", L"English"},
    {L"日本語", L"日本語"},
    {L"Script editor...", L"スクリプトエディター..."},
    {L"Choose script editor...", L"スクリプトエディターを選択..."},
    {L"Use automatic Visual Studio detection", L"Visual Studio を自動検出して使う"},
    {L"Reload shader", L"シェーダーを再読み込み"},
    {L"Reimport / Reload shader", L"再インポート / シェーダーを再読み込み"},
    {L"Front orthographic view", L"正面の平行投影ビュー"},
    {L"Import...", L"インポート..."},
    {L"Import file...", L"ファイルをインポート..."},
    {L"Create Empty", L"空のオブジェクトを作成"},
    {L"3D Object", L"3D オブジェクト"},
    {L"2D Object", L"2D オブジェクト"},
    {L"World Quad (Polygon Renderer)", L"ワールド四角形（Polygon Renderer）"},
    {L"Create 2D world quad", L"2D ワールド四角形を作成"},
    {L"Place X / FBX model...", L"X / FBX モデルを配置..."},
    {L"Create GameObject", L"ゲームオブジェクトを作成"},
    {L"Save current layout", L"現在のレイアウトを保存"},
    {L"Load saved layout", L"保存したレイアウトを読み込み"},
    {L"Reset layout", L"レイアウトをリセット"},
    {L"Save Layout", L"レイアウトを保存"},
    {L"Load Layout", L"レイアウトを読み込み"},
    {L"Revert Factory Settings", L"初期設定に戻す"},
    {L"Show / restore", L"表示 / 元に戻す"},
    {L"Float / dock", L"フロート / ドッキング"},
    {L"Minimize / expand", L"最小化 / 展開"},
    {L"Hide", L"非表示"},
    {L"Dock", L"ドッキング"},
    {L"Float", L"フロート"},
    {L"Minimize", L"最小化"},
    {L"Close", L"閉じる"},
    {L"Console", L"コンソール"},
    {L"DxStudio manual (README)", L"DxStudio マニュアル（README）"},
    {L"About DxStudio", L"DxStudio について"},
    {L"Save scene (Ctrl+S)", L"シーンを保存 (Ctrl+S)"},
    {L"Play (Ctrl+P)", L"再生 (Ctrl+P)"},
    {L"Pause (Ctrl+Shift+P)", L"一時停止 (Ctrl+Shift+P)"},
    {L"Step one frame (Ctrl+Alt+P)", L"1 フレーム進める (Ctrl+Alt+P)"},
    {L"Move tool (W)", L"移動ツール (W)"},
    {L"Rotate tool (E)", L"回転ツール (E)"},
    {L"Scale tool (R)", L"拡大縮小ツール (R)"},
    {L"W Move", L"W 移動"},
    {L"E Rotate", L"E 回転"},
    {L"R Scale", L"R 拡縮"},
    {L"Export the Windows game", L"Windows 用ゲームを書き出し"},
    {L"Graphics API: DirectX 12 / 11 / 9", L"グラフィックス API: DirectX 12 / 11 / 9"},
    {L"Scene draw mode", L"シーンの描画モード"},
    {L"Game view aspect ratio", L"ゲームビューの縦横比"},
    {L"Shaded", L"シェーディング"},
    {L"Wireframe", L"ワイヤーフレーム"},
    {L"Shaded Wireframe", L"シェーディング + ワイヤーフレーム"},
    {L"Free Aspect", L"自由な縦横比"},
    {L"Persp", L"透視"},
    {L"Ortho", L"平行"},
    // ---- パネル ----
    {L"Hierarchy", L"ヒエラルキー"},
    {L"Inspector", L"インスペクター"},
    {L"Project", L"プロジェクト"},
    {L"PROJECT", L"プロジェクト"},
    {L"Scene", L"シーン"},
    {L"Game", L"ゲーム"},
    {L"Search Hierarchy", L"ヒエラルキーを検索"},
    {L"Search current folder", L"現在のフォルダーを検索"},
    {L"Refresh", L"更新"},
    {L"Name", L"名前"},
    {L"Type", L"種類"},
    {L"Folder", L"フォルダー"},
    {L"Clear", L"クリア"},
    {L"Collapse", L"まとめる"},
    {L"Create", L"作成"},
    {L"Create > C++ Component...", L"作成 > C++ コンポーネント..."},
    {L"New C++ Component", L"新しい C++ コンポーネント"},
    {L"Component name (letters, digits, underscores):", L"コンポーネント名（英字・数字・アンダースコア）:"},
    {L"Cancel", L"キャンセル"},
    {L"Creates Scripts/Name.h and Name.cpp. Rebuild to attach.",
     L"Scripts/名前.h と 名前.cpp を作成します。ビルドし直すと付けられます。"},
    {L"Import a file into the selected project folder", L"選択中のプロジェクトフォルダーにファイルを取り込む"},
    // ---- インスペクター ----
    {L"Transform", L"トランスフォーム"},
    {L"Position", L"位置"},
    {L"Rotation °", L"回転 °"},
    {L"Scale", L"スケール"},
    {L"Tag", L"タグ"},
    {L"Layer", L"レイヤー"},
    {L"Untagged", L"Untagged"},
    {L"Default", L"Default"},
    {L"Color", L"色"},
    {L"Material", L"マテリアル"},
    {L"Texture", L"テクスチャ"},
    {L"Mesh", L"メッシュ"},
    {L"Script", L"スクリプト"},
    {L"Add Component", L"コンポーネントを追加"},
    {L"Remove Component", L"コンポーネントを削除"},
    {L"Remove", L"削除"},
    {L"Reset", L"リセット"},
    {L"Move Up", L"上へ移動"},
    {L"Move Down", L"下へ移動"},
    {L"Set", L"設定"},
    {L"Rendering", L"レンダリング"},
    {L"Physics", L"物理"},
    {L"Input", L"入力"},
    {L"Scripts", L"スクリプト"},
    {L"Components", L"コンポーネント"},
    {L"Rendering / Color", L"レンダリング / 色"},
    {L"Mesh Renderer", L"Mesh Renderer"},
    {L"Model Renderer", L"Model Renderer"},
    {L"Polygon Renderer", L"Polygon Renderer"},
    {L"Image", L"Image"},
    {L" (Script)", L" (スクリプト)"},
    {L"Browse X / FBX...", L"X / FBX を参照..."},
    {L"Create Material (Direct3D 11 / 12)", L"マテリアルを作成（Direct3D 11 / 12）"},
    {L"Convert to X File", L"X ファイルに変換"},
    {L"Material created. Save the scene to keep it.", L"マテリアルを作成しました。残すにはシーンを保存してください。"},
    {L"Converted to an X file. Save the scene to keep it.", L"X ファイルに変換しました。残すにはシーンを保存してください。"},
    {L"Cannot create a material from the X file: ", L"X ファイルからマテリアルを作成できません: "},
    {L"Cannot convert the FBX file to an X file: ", L"FBX ファイルを X ファイルに変換できません: "},
    {L"Active (visible in the scene)", L"有効（シーンに表示）"},
    {L"Size", L"サイズ"},
    {L"Element ", L"要素 "},
    {L"The associated script can not be loaded. Register the class and rebuild.",
     L"関連付けられたスクリプトを読み込めません。クラスを登録してビルドし直してください。"},
    // 組み込みコンポーネントのフィールド
    {L"Mass", L"質量"},
    {L"Drag", L"空気抵抗"},
    {L"Use Gravity", L"重力を使用"},
    {L"Is Kinematic", L"キネマティック"},
    {L"Bounciness", L"反発係数"},
    {L"Friction", L"摩擦"},
    {L"Is Trigger", L"トリガー"},
    {L"Center X", L"中心 X"},
    {L"Center Y", L"中心 Y"},
    {L"Center Z", L"中心 Z"},
    {L"Size X", L"サイズ X"},
    {L"Size Y", L"サイズ Y"},
    {L"Size Z", L"サイズ Z"},
    {L"Radius", L"半径"},
    {L"Convex", L"凸形状"},
    {L"Active", L"有効"},
    {L"Orthographic", L"平行投影"},
    {L"Field of View", L"視野角"},
    {L"Near Clip", L"近クリップ"},
    {L"Far Clip", L"遠クリップ"},
    {L"Actions File", L"アクションファイル"},
    {L"Action Map", L"アクションマップ"},
    // ---- ステータスバー ----
    {L"Playing", L"再生中"},
    {L"Paused", L"一時停止中"},
    {L"Edit Mode", L"編集モード"},
    {L"RMB + WASD/QE: fly | Alt + RMB: orbit | MMB: pan | Shift + gizmo: snap",
     L"右ドラッグ + WASD/QE: 飛行 | Alt + 右ドラッグ: 周回 | 中ドラッグ: パン | Shift + ギズモ: スナップ"},
    {L"Scene view: RMB + WASD/QE fly | Alt + RMB orbit | MMB pan | W/E/R tools",
     L"シーンビュー: 右ドラッグ + WASD/QE 飛行 | Alt + 右ドラッグ 周回 | 中ドラッグ パン | W/E/R ツール"},
    {L"Game view: rendered through the first active Camera component.",
     L"ゲームビュー: 最初に有効な Camera コンポーネントから描画しています。"},
    {L"Changes applied. Save the scene to keep them.", L"変更を適用しました。残すにはシーンを保存してください。"},
    {L"Component attached. Save or export to keep it.",
     L"コンポーネントを追加しました。残すには保存または書き出しを行ってください。"},
    {L"Component detached. Its serialized Inspector values were removed.",
     L"コンポーネントを外しました。インスペクターの保存値も削除されました。"},
    {L"Component field applied. Values remain live during Play Mode.",
     L"フィールドを適用しました。再生中も値はリアルタイムに反映されます。"},
    {L"Component value applied. Values stay live during Play Mode.",
     L"値を適用しました。再生中も値はリアルタイムに反映されます。"},
    {L"Component order changed. Components run in Inspector order.",
     L"コンポーネントの順番を変更しました。インスペクターの順に実行されます。"},
    {L"Drag a colored axis to transform. Esc cancels a drag.",
     L"色の付いた軸をドラッグして変形します。Esc でドラッグを取り消します。"},
    {L"Focused selection. W / E / R: Move / Rotate / Scale.", L"選択をフォーカスしました。W / E / R: 移動 / 回転 / 拡縮"},
    {L"GameObject disabled. It remains in the hierarchy.", L"ゲームオブジェクトを無効にしました。ヒエラルキーには残ります。"},
    {L"GameObject enabled.", L"ゲームオブジェクトを有効にしました。"},
    {L"Mesh assigned. Save the scene to keep it.", L"メッシュを設定しました。残すにはシーンを保存してください。"},
    {L"MeshRenderer attached. Pick a mesh with the Mesh field's button.",
     L"MeshRenderer を追加しました。メッシュ欄のボタンでメッシュを選んでください。"},
    {L"IRender component attached at compile-time-known renderer type.", L"レンダラー（IRender）コンポーネントを追加しました。"},
    {L"Model placed from project/Assets/Models. ASCII FBX and text X are supported.",
     L"project/Assets/Models からモデルを配置しました。ASCII 形式の FBX とテキスト形式の X に対応しています。"},
    {L"Object added with a MeshRenderer. Save the scene to keep it.",
     L"MeshRenderer 付きのオブジェクトを追加しました。残すにはシーンを保存してください。"},
    {L"Object added. Save the scene to keep it.", L"オブジェクトを追加しました。残すにはシーンを保存してください。"},
    {L"Object moved to the hierarchy root.", L"オブジェクトをヒエラルキーの最上位へ移動しました。"},
    {L"Orthographic camera. Save or export to keep this camera view.",
     L"平行投影カメラです。このカメラ表示を残すには保存または書き出しを行ってください。"},
    {L"Parent set by drag and drop. Save the scene to keep it.",
     L"ドラッグ＆ドロップで親を設定しました。残すにはシーンを保存してください。"},
    {L"Pause armed: Play starts paused.", L"一時停止を予約しました: 再生は一時停止状態で始まります。"},
    {L"Paused. Step advances one frame.", L"一時停止中です。コマ送りで 1 フレーム進みます。"},
    {L"PLAY MODE resumed.", L"再生を再開しました。"},
    {L"Play Mode stopped. Runtime changes were discarded.", L"再生を停止しました。再生中の変更は破棄されました。"},
    {L"PLAY MODE: project components are running. Press Stop or Ctrl+P to restore the edited scene.",
     L"再生中: プロジェクトのコンポーネントが動作しています。停止または Ctrl+P で編集中のシーンに戻ります。"},
    {L"Perspective camera. Quads and cubes share the same 3D world.",
     L"透視投影カメラです。四角形と立方体は同じ 3D 空間にあります。"},
    {L"Project file saved.", L"プロジェクトファイルを保存しました。"},
    {L"Project opened. Project C++ classes require a Debug or Release rebuild.",
     L"プロジェクトを開きました。プロジェクトの C++ クラスは Debug / Release でビルドし直す必要があります。"},
    {L"Renderer unavailable. Fix the shader and select Reload Shader.",
     L"レンダラーを使用できません。シェーダーを修正して「シェーダーを再読み込み」を選んでください。"},
    {L"Rendering stopped. Select Reload Shader to retry.",
     L"描画を停止しました。「シェーダーを再読み込み」でやり直してください。"},
    {L"Saved workspace layout restored.", L"保存したレイアウトを復元しました。"},
    {L"Scene loaded.", L"シーンを読み込みました。"},
    {L"Scene saved to the active project.", L"シーンをプロジェクトに保存しました。"},
    {L"Selected objects removed. Save to keep changes.", L"選択したオブジェクトを削除しました。残すには保存してください。"},
    {L"Selection duplicated. Ctrl+D repeats this action.", L"選択を複製しました。Ctrl+D で繰り返せます。"},
    {L"Selection updated. Shift adds, Ctrl toggles; drag empty space to select an area.",
     L"選択を更新しました。Shift で追加、Ctrl で切り替え、空き部分のドラッグで範囲選択します。"},
    {L"Stepped one frame.", L"1 フレーム進めました。"},
    {L"Transform cancelled.", L"変形を取り消しました。"},
    {L"Transform changed. Save the scene to keep it.", L"トランスフォームを変更しました。残すにはシーンを保存してください。"},
    {L"Transform reset.", L"トランスフォームをリセットしました。"},
    {L"Workspace layout saved in the active project.", L"レイアウトをプロジェクトに保存しました。"},
    {L"Floating panel.", L"フロートパネルです。"},
    {L"Release to dock the panel in the highlighted slot (Ctrl: keep floating).",
     L"離すと強調表示された位置にドッキングします（Ctrl: フロートのまま）。"},
    {L"Export complete. Run Game.exe in the selected folder.",
     L"書き出しが完了しました。選んだフォルダーの Game.exe を実行してください。"},
    {L"Export complete", L"書き出し完了"},
    {L"Export failed.", L"書き出しに失敗しました。"},
    {L"Exporting game", L"ゲームを書き出し中"},
    {L"Exported to:\n", L"書き出し先:\n"},
    {L"Build failed.", L"ビルドに失敗しました。"},
    {L"No build is running.", L"実行中のビルドはありません。"},
    {L"Canceling the build...", L"ビルドを中止しています..."},
    {L"An export cannot be canceled; it finishes shortly.", L"書き出しは中止できません。まもなく完了します。"},
    {L"Building ", L"ビルド中: "},
    {L"Android Debug APK", L"Android デバッグ APK"},
    {L"   (Build > Cancel build to stop)", L"   （ビルド > ビルドを中止 で停止）"},
    {L" build completed.", L" のビルドが完了しました。"},
    {L" build canceled.", L" のビルドを中止しました。"},
    {L"Language changed.", L"言語を変更しました。"},
    // ---- エラー・確認 ----
    {L"A build is already running (Build > Cancel build stops it).",
     L"すでにビルドを実行中です（ビルド > ビルドを中止 で停止できます）。"},
    {L"Wait for the running build to finish (or use Build > Cancel build).",
     L"実行中のビルドが終わるまでお待ちください（または ビルド > ビルドを中止）。"},
    {L"The build failed.", L"ビルドに失敗しました。"},
    {L"Full log: ", L"ログ全体: "},
    {L"The build script is missing.", L"ビルドスクリプトが見つかりません。"},
    {L"Could not start the build process.", L"ビルドプロセスを開始できませんでした。"},
    {L"Remove all selected objects from the scene?", L"選択したオブジェクトをすべてシーンから削除しますか？"},
    {L"The selected folder already contains one or more exported game files. Replace them?",
     L"選んだフォルダーには書き出し済みのゲームファイルがあります。置き換えますか？"},
    {L"Stop Play Mode before saving, loading, switching, exporting, building, or reloading the shader.",
     L"保存・読み込み・切り替え・書き出し・ビルド・シェーダー再読み込みの前に再生を停止してください。"},
    {L"The existing scene could not be loaded. Fix it and use Load before exporting.",
     L"既存のシーンを読み込めませんでした。修正して読み込み直してから書き出してください。"},
    {L"The existing scene could not be loaded. Fix it and use Load before saving.",
     L"既存のシーンを読み込めませんでした。修正して読み込み直してから保存してください。"},
    {L"Choose a registered component first.", L"先に登録済みのコンポーネントを選んでください。"},
    {L"Select an attached component to remove.", L"削除するコンポーネントを選んでください。"},
    {L"Select a component field first.", L"先にコンポーネントのフィールドを選んでください。"},
    {L"Select exactly one GameObject to add a component.",
     L"コンポーネントを追加するゲームオブジェクトを 1 つだけ選んでください。"},
    {L"Enter a finite number for each transform and color field.",
     L"トランスフォームと色の各欄には有限の数値を入力してください。"},
    {L"Enter a non-empty, single-line name.", L"空でない 1 行の名前を入力してください。"},
    {L"Mesh must name an X or FBX file (pick one with the button).",
     L"メッシュには X か FBX ファイルを指定してください（ボタンで選べます）。"},
    {L"Place an X/FBX model first, or assign a model asset through Place X / FBX model.",
     L"先に X / FBX モデルを配置するか、「X / FBX モデルを配置」でモデルを設定してください。"},
    {L"The project does not contain Default.hlsl.", L"プロジェクトに Default.hlsl がありません。"},
    {L"Renderer initialization failed: ", L"レンダラーの初期化に失敗しました: "},
    {L"Rendering failed: ", L"描画に失敗しました: "},
    {L"Could not check the export folder: ", L"書き出し先フォルダーを確認できませんでした: "},
    {L"Could not open README.md.", L"README.md を開けませんでした。"},
    {L"Could not save the workspace layout.", L"レイアウトを保存できませんでした。"},
    {L"Could not finalize the workspace layout.", L"レイアウトの保存を完了できませんでした。"},
    {L"No valid saved workspace layout was found.", L"有効な保存済みレイアウトが見つかりません。"},
    {L"The saved workspace layout is invalid.", L"保存されたレイアウトが不正です。"},
    {L"Choose a folder for the exported game", L"ゲームの書き出し先フォルダーを選んでください"},
    {L"Save DxStudio project file", L"DxStudio プロジェクトファイルを保存"},
    {L"DxStudio project", L"DxStudio プロジェクト"},
    {L"Enter a valid project name.", L"正しいプロジェクト名を入力してください。"},
    {L"A folder with that project name already exists. Choose another name.",
     L"同じ名前のプロジェクトフォルダーがあります。別の名前にしてください。"},
    {L"Could not create the project folders: ", L"プロジェクトのフォルダーを作成できませんでした: "},
    {L"Could not copy the default shader: ", L"既定のシェーダーをコピーできませんでした: "},
    {L"Project folder unavailable", L"プロジェクトフォルダーを使用できません"},
    {L"The project folder is unavailable.", L"プロジェクトフォルダーを使用できません。"},
    {L"Choose a file to import.", L"インポートするファイルを選んでください。"},
    {L"Could not select a file to import.", L"インポートするファイルを選べませんでした。"},
    {L"Could not initialize the file dialog.", L"ファイル選択ダイアログを初期化できませんでした。"},
    {L"A file with that name already exists. Nothing was overwritten.", L"同じ名前のファイルがあります。上書きはしていません。"},
    {L"Import failed (Windows error ", L"インポートに失敗しました（Windows エラー "},
    {L"The destination folder is unavailable or outside the project.",
     L"保存先フォルダーを使用できないか、プロジェクトの外にあります。"},
    {L"The destination folder is unavailable, outside the project, or uses a reparse point.",
     L"保存先フォルダーを使用できないか、プロジェクトの外にあるか、リパースポイントです。"},
    {L"This item is unavailable or outside the project folder.",
     L"この項目は使用できないか、プロジェクトフォルダーの外にあります。"},
    {L"This file type is not opened by the project browser.", L"この種類のファイルはプロジェクトブラウザーでは開けません。"},
    {L"No application could open this file.", L"このファイルを開けるアプリケーションがありません。"},
    {L"Use 1 to 64 letters, digits or underscores, starting with a letter. Do not include an extension.",
     L"英字で始まる 1〜64 文字の英字・数字・アンダースコアにしてください。拡張子は付けないでください。"},
    {L"That name is reserved by Windows. Choose another component name.",
     L"その名前は Windows の予約名です。別のコンポーネント名にしてください。"},
    {L"A header or source file with that name already exists. Existing files were not changed.",
     L"同じ名前のヘッダーまたはソースファイルがあります。既存のファイルは変更していません。"},
    {L"The Scripts folder is unavailable or uses a reparse point.",
     L"Scripts フォルダーを使用できないか、リパースポイントです。"},
    {L"The Scripts folder could not be enumerated.", L"Scripts フォルダーの中身を取得できませんでした。"},
    {L"Could not create the component (Windows error ", L"コンポーネントを作成できませんでした（Windows エラー "},
    {L"Component created", L"コンポーネントを作成しました"},
    {L"Run build.cmd and restart the editor to attach the new component.",
     L"build.cmd を実行してエディターを再起動すると、新しいコンポーネントを付けられます。"},
    {L"Visual Studio project files could not be generated.", L"Visual Studio のプロジェクトファイルを生成できませんでした。"},
    {L"Engine.h could not be located for Visual Studio navigation.",
     L"Visual Studio での参照用に Engine.h が見つかりませんでした。"},
    {L"The script editor could not be started. Use Choose script editor or Automatic editor in the Project menu.",
     L"スクリプトエディターを起動できませんでした。プロジェクトメニューの「スクリプトエディターを選択」または「自動」を使ってくだ"
     L"さい。"},
    {L"Choose an editor (Visual Studio, VS Code, or another editor)",
     L"エディターを選んでください（Visual Studio、VS Code など）"},
    {L"Choose an editor executable (.exe).", L"エディターの実行ファイル（.exe）を選んでください。"},
    {L"Could not select an editor.", L"エディターを選べませんでした。"},
    {L"Automatic editor (Visual Studio / default app)", L"自動（Visual Studio / 既定のアプリ）"},
    {L"Could not save the script editor setting.", L"スクリプトエディターの設定を保存できませんでした。"},
    {L"Could not save the script editor setting. Reparse points are not supported.",
     L"スクリプトエディターの設定を保存できませんでした。リパースポイントには対応していません。"},
    {L"The editor preferences folder is unavailable.", L"エディターの設定フォルダーを使用できません。"},
    {L"Cannot determine executable directory.", L"実行ファイルのフォルダーを特定できません。"},
    {L"Use the Build menu or the toolbar buttons.", L"ビルドメニューかツールバーのボタンを使ってください。"},
};

//**********************************************************************************
// *** 内部状態 ***
//**********************************************************************************
EditorLanguage g_language = EditorLanguage::ENGLISH; // 現在の表示言語

// 英語 → 日本語の索引（初回使用時に表から作る）
const std::unordered_map<std::wstring_view, const wchar_t*>& JapaneseIndex() {
    static const auto index = [] {
        std::unordered_map<std::wstring_view, const wchar_t*> map;
        for (const auto& pair : JAPANESE_TEXT) {
            map.emplace(pair.pEnglish, pair.pJapanese);
        }

        return map;
    }();
    return index;
}

// 組み立てた訳文の保管場所（Tr() が返すポインタを長く有効にするため）
const wchar_t* Store(std::wstring sText) {
    static std::unordered_map<std::wstring, std::wstring> cache;
    const auto found = cache.find(sText);
    if (found != cache.end()) return found->second.c_str();
    auto key = sText;
    return cache.emplace(std::move(key), std::move(sText)).first->second.c_str();
}

// 設定ファイルのパス（%LOCALAPPDATA%\DxStudio\EditorPreferences.txt）
std::filesystem::path PreferencesPath() {
    wchar_t aBuffer[MAX_PATH]{};
    const DWORD dwLength = GetEnvironmentVariableW(L"LOCALAPPDATA", aBuffer, MAX_PATH);
    if (dwLength == 0 || dwLength >= MAX_PATH) return {};
    return std::filesystem::path(aBuffer) / L"DxStudio" / PREFERENCES_FILE;
}

std::wstring Widen(const std::string& sText) {
    if (sText.empty()) return {};
    const int nLength = MultiByteToWideChar(CP_UTF8, 0, sText.data(), static_cast<int>(sText.size()), nullptr, 0);
    std::wstring sResult(static_cast<size_t>(nLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, sText.data(), static_cast<int>(sText.size()), sResult.data(), nLength);
    return sResult;
}

std::string Narrow(const std::wstring& sText) {
    if (sText.empty()) return {};
    const int nLength =
        WideCharToMultiByte(CP_UTF8, 0, sText.data(), static_cast<int>(sText.size()), nullptr, 0, nullptr, nullptr);
    std::string sResult(static_cast<size_t>(nLength), '\0');
    WideCharToMultiByte(CP_UTF8, 0, sText.data(), static_cast<int>(sText.size()), sResult.data(), nLength, nullptr, nullptr);
    return sResult;
}
} // namespace

//==================================================================================
// --- 現在の言語の取得処理 ---
//==================================================================================
EditorLanguage GetLanguage() {
    return g_language;
}

//==================================================================================
// --- 言語の切り替え + 保存処理 ---
//==================================================================================
bool SetLanguage(EditorLanguage language) {
    g_language = language;
    const auto path = PreferencesPath();
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file << LANGUAGE_KEY << ' ' << (language == EditorLanguage::JAPANESE ? "ja" : "en") << '\n';
    return static_cast<bool>(file);
}

//==================================================================================
// --- 起動時の言語読み込み処理 ---
//==================================================================================
void LoadLanguagePreference() {
    // 設定ファイルがあればそれに従う
    std::ifstream file(PreferencesPath(), std::ios::binary);
    std::string sKey, sValue;
    while (file >> sKey >> sValue) {
        if (sKey == LANGUAGE_KEY) {
            g_language = sValue == "ja" ? EditorLanguage::JAPANESE : EditorLanguage::ENGLISH;
            return;
        }
    }

    // 無ければ Windows の表示言語（日本語なら日本語）
    g_language = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_JAPANESE ? EditorLanguage::JAPANESE : EditorLanguage::ENGLISH;
}

//==================================================================================
// --- 翻訳処理 ---
//==================================================================================
const wchar_t* Tr(const wchar_t* pEnglish) {
    if (!pEnglish || !*pEnglish || g_language == EditorLanguage::ENGLISH) return pEnglish;
    const auto& index = JapaneseIndex();
    const std::wstring_view sText(pEnglish);

    // 1. 完全一致
    if (const auto found = index.find(sText); found != index.end()) return found->second;

    // 2. メニューのショートカット（"Save scene\tCtrl+S"）は前半だけ訳す
    if (const auto nTab = sText.find(L'\t'); nTab != std::wstring_view::npos) {
        if (const auto found = index.find(sText.substr(0, nTab)); found != index.end()) {
            return Store(std::wstring(found->second) + std::wstring(sText.substr(nTab)));
        }

        return pEnglish;
    }

    // 3. 「固定の前置き + 可変部分」（"Rendering failed: " + 詳細 など）は前置きだけ訳す。
    //    一番長く一致する前置きを使う。
    const wchar_t* pBest = nullptr;
    size_t nBest = 0;
    for (const auto& pair : JAPANESE_TEXT) {
        const std::wstring_view sKey(pair.pEnglish);
        if (sKey.size() <= nBest || sKey.size() >= sText.size()) continue;
        if (sKey.back() != L' ' && sKey.back() != L'\n') continue;
        if (sText.compare(0, sKey.size(), sKey) == 0) {
            pBest = pair.pJapanese;
            nBest = sKey.size();
        }
    }

    if (pBest) return Store(std::wstring(pBest) + std::wstring(sText.substr(nBest)));
    return pEnglish; // 訳なし: 英語のまま
}

std::wstring Tr(const std::wstring& sEnglish) {
    return Tr(sEnglish.c_str());
}

std::string TrUtf8(const std::string& sEnglish) {
    if (g_language == EditorLanguage::ENGLISH) return sEnglish;
    return Narrow(Tr(Widen(sEnglish)));
}

//==================================================================================
// --- 翻訳付きメニュー項目の追加処理 ---
//==================================================================================
BOOL AppendMenuL(HMENU hMenu, UINT uFlags, UINT_PTR uId, LPCWSTR pText) {
    // 区切り線・ビットマップ・オーナードローは文字列を持たないのでそのまま
    const bool bHasText = pText && !(uFlags & (MF_SEPARATOR | MF_BITMAP | MF_OWNERDRAW));
    return AppendMenuW(hMenu, uFlags, uId, bHasText ? Tr(pText) : pText);
}

//==================================================================================
// --- 翻訳付きメッセージボックスの表示処理 ---
//==================================================================================
int MessageBoxL(HWND hOwner, LPCWSTR pText, LPCWSTR pCaption, UINT uType) {
    return MessageBoxW(hOwner, Tr(pText), Tr(pCaption), uType);
}

//==================================================================================
// --- コントロールの表示文字列の翻訳処理 ---
//==================================================================================
const wchar_t* LocalizedControlText(const wchar_t* pClass, const wchar_t* pText) {
    if (!pClass || !pText || !*pText) return pText;

    // 入力欄（EDIT）やリストの値はユーザーのデータなので訳さない
    if (_wcsicmp(pClass, L"BUTTON") != 0 && _wcsicmp(pClass, L"STATIC") != 0) return pText;
    return Tr(pText);
}

} // namespace dxs::editor
