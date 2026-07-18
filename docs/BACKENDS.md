# バックエンド

## Miniaudio

`ma_engine` を1個使います。ファイルからの音源は Miniaudio のリソースマネージャーで読み込みます。メモリ上の音源は、内部にコピーしたバイト列と `ma_decoder` から `ma_sound` を作ります。2D 再生だけを行うため、音源の位置による音の変化は無効にしています。

## XAudio2

Windows SDK に含まれる XAudio2 2.8 以降を使います。実行時に `xaudio2_9.dll`、`xaudio2_8.dll` の順で探します。音源は Media Foundation で48 kHz、ステレオ、32ビット浮動小数点 PCM へその場でデコードし、XAudio2 の Source Voice へ渡します。API と ABI が異なる DirectX SDK 版 XAudio2 2.7には対応しません。

## OpenAL

OpenAL Soft 1.25.2 の `OpenAL32.dll` を実行時に読み込みます。音源は Media Foundation で符号付き16ビット PCM へその場でデコードし、OpenAL のバッファーと音源オブジェクトを作ります。

OpenAL と XAudio2 の DLL は、次の順序で探します。

1. `AudioEngineCreateInfo::RuntimeLibrarySearchPaths` に指定したディレクトリ
2. `<exe>/ThirdParty`
3. `<exe>`
4. Windows の System32

`RuntimeLibrarySearchPaths` には、UTF-8 のディレクトリパスを指定します。文字列の途中に NULL 文字を含めることはできません。条件に合わない値を指定すると `InvalidArgument` になります。指定された順に、各ディレクトリ内の DLL を探します。相対パスは、プロセスのカレントディレクトリを基準に解決されます。配布するアプリケーションでは絶対パスを推奨します。最後は `LOAD_LIBRARY_SEARCH_SYSTEM32` を使い、System32 だけを探します。`PATH` やカレントディレクトリを使う通常の DLL 検索は行いません。

## 登録処理

各バックエンドの静的ライブラリは、静的初期化による自動登録を行いません。使うバックエンドに対応する `RegisterMiniaudioBackend` などを呼んでください。すべてをまとめて使う場合は、`aEngine::AllBackends` をリンクして `RegisterAllBackends` を呼びます。
