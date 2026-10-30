# 対応プラットフォームと移植に必要な作業

DxStudioエディターとDirectX 12プレイヤーはWindows PC用です。AndroidにはDirectX 12を移植せず、同じ `scene.dxs` を読むNativeActivity/OpenGL ES 3プレイヤーを用意しています。Windowsの `Game.exe` をモバイル端末へコピーして実行する方式ではありません。

| 対象 | 現在の状態 | 配布物 |
| --- | --- | --- |
| Windows PC | DirectX 12 プレイヤーを実装 | `Game.exe`、`scene.dxs`、コンパイル済みシェーダー2個 |
| Android | デバッグAPK生成済み | arm64-v8a / x86_64、NativeActivity、OpenGL ES 3、シーン・親子階層・タッチ視点操作 |
| iPhone / iPad | 未実装・未検証 | 今後、iOS アプリと署名・配布処理が必要 |

現在の描画実装は Windows SDK の Direct3D 12、DXGI、Win32 ウィンドウ、D3DCompiler を使用します。Direct3D 12 のヘッダーとライブラリは Windows SDK に含まれます。[Microsoft: Direct3D 12 graphics](https://learn.microsoft.com/en-us/windows/win32/direct3d12/)

以下は現在の構成と次の拡張候補です。

1. **共通部分を分離する。** Scene / Entity / Camera / コンポーネント登録を、`Windows.h`、`HWND`、Direct3D のオブジェクトから分離します。ウィンドウ、ファイル操作、時計、入力、アプリの停止・再開をプラットフォームごとの処理へ分けます。DirectXMath の利用は維持できますが、対象コンパイラーと CPU での検証が必要です。
2. **描画処理を差し替え可能にする。** Windows 用 DirectX 12 実装を維持し、モバイル用の描画実装を追加します。頂点、カメラ、深度、色、シェーダー定数の契約は共通化し、描画 API ごとのシェーダー変換または個別実装を用意します。現行の `.cso` ファイルは他の描画 API 用のシェーダーとしてそのまま利用できません。
3. **Android機能を拡張する。** 現在は基本図形とシーン階層を描画できますが、Windows用の外部HLSLとプロジェクト固有C++コンポーネントはAndroid APKへ移植していません。次段階では共通コンポーネントAPI、GLSL/シェーダー変換、リリース署名、AAB、テクスチャとモデルを追加します。[Android NDK: CMake](https://developer.android.com/ndk/guides/cmake)
4. **iOS 用プレイヤーを追加する。** Metal 描画、iOS のアプリライフサイクル、タッチ入力、アプリバンドルからのデータ読み込みを実装します。Metal は iPhone / iPad を含む Apple プラットフォームを対象とします。iOS SDK を含む Xcode のビルド環境として、対応する macOS 環境が必要です。Windows のエディターからプロジェクトを生成する構成にしても、最終ビルド・署名の処理は別途必要です。[Apple: Metal](https://developer.apple.com/metal/)、[Apple: Xcode system requirements](https://developer.apple.com/xcode/system-requirements)
5. **実機で配布物を検証する。** 立方体・回転した平面・両投影のカメラ・コンポーネント動作を、ソースを同梱しないパッケージで確認します。画面回転、停止・再開、タッチ、深度とシェーダーの一致を確認してから、エディターの書き出し対象として公開します。

Windows向け書き出しとAndroidデバッグAPKは利用できます。Android版は現段階では基本シーン確認用プレイヤーであり、Windows版と完全な機能互換ではありません。
