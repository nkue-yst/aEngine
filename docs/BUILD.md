# ビルドと配布

## ビルド設定

- `AENGINE_BUILD_MINIAUDIO`: Miniaudio バックエンドをビルドする。既定値は `ON`。
- `AENGINE_BUILD_XAUDIO2`: XAudio2 バックエンドをビルドする。既定値は `ON`。
- `AENGINE_BUILD_OPENAL`: OpenAL バックエンドをビルドする。既定値は `ON`。
- `AENGINE_BUILD_SHARED_LIBS`: 公開する各部品を DLL としてビルドする。既定値は `OFF` で、静的ライブラリを作る。
- `AENGINE_FETCH_OPENAL_RUNTIME`: 検証済みのアーカイブから、OpenAL の実行に必要なファイル一式を作る。既定値は `ON`。キャッシュにアーカイブがなければダウンロードする。すでにある場合も SHA-256 を確認し、必要なファイル一式を作り直す。
- `AENGINE_MINIAUDIO_ROOT`: Miniaudio のソースコードがあるディレクトリ。既定値は `ThirdParty/miniaudio`。
- `AENGINE_OPENAL_RUNTIME_ROOT`: OpenAL のヘッダー、DLL、ライセンスがあるディレクトリ。既定値は、ビルドディレクトリ内の `ThirdPartyRuntime/OpenAL`。
- `AENGINE_BUILD_SAMPLES`: aEngine を単独でビルドするときの既定値は `ON`。`add_subdirectory()` で追加するときは `OFF`。

## aEngine だけをビルドする

```powershell
cmake -S . -B out/build -G "Visual Studio 17 2022" -A x64
cmake --build out/build --config Debug --parallel
```

既定の静的ライブラリ版から DLL 版へ切り替えるには、CMake の設定時に `-DAENGINE_BUILD_SHARED_LIBS=ON` を追加します。DLL 版では、Core、有効にした各バックエンド、AllBackends が別々の DLL になります。内部で使う WindowsSupport だけは静的ライブラリのまま、各バックエンド DLL に組み込まれます。`aEngine::Core` などの CMake ターゲット名は変わりません。

すでに用意した OpenAL のファイル一式を使い、ダウンロードを行わない場合は、`AENGINE_FETCH_OPENAL_RUNTIME=OFF` と `AENGINE_OPENAL_RUNTIME_ROOT=<root>` を同時に指定します。`<root>` には次のファイルが必要です。1つでもない場合は、CMake の設定時にエラーになります。

```text
<root>/
  Win64/OpenAL32.dll
  include/AL/al.h
  include/AL/alc.h
  COPYING
  LICENSE-pffft
  Source/openal-soft-1.25.2.tar.bz2
```

ソースコードのアーカイブは、SHA-256 が `1DBAAC44E7579D5BC8847CA8DB4B2E8B9FD3961041F35EE20DEF4958301E1089` と一致する必要があります。

`AENGINE_FETCH_OPENAL_RUNTIME=ON` では、すでにあるファイル一式をそのまま使い回しません。ダウンロード用キャッシュのアーカイブを確認し、なければダウンロードした後で、上記のファイル一式を毎回作り直します。

Miniaudio バックエンドだけをビルドする例:

```powershell
cmake -S . -B out/miniaudio -G "Visual Studio 17 2022" -A x64 `
  -DAENGINE_BUILD_MINIAUDIO=ON `
  -DAENGINE_BUILD_XAUDIO2=OFF `
  -DAENGINE_BUILD_OPENAL=OFF
cmake --build out/miniaudio --config Release --parallel
```

## インストールと配布

`cmake --install` は、次のものを出力します。

- ヘッダー
- 選んだリンク方法のライブラリ
- CMake パッケージ
- LICENSE、README、各ガイド
- 外部ライブラリの一覧と、ビルドしたバックエンドに必要なライセンス表記

DLL 版の aEngine と、OpenAL の実行用 DLL は `bin` へ入ります。静的ライブラリとインポートライブラリは `lib` へ入ります。主な出力先は次のとおりです。

```text
<prefix>/
  bin/aEngine*.dll                                     DLL版のビルド時
  bin/OpenAL32.dll                                      OpenALのビルド時
  lib/aEngine*.lib                                     静的ライブラリまたはインポートライブラリ
  share/aEngine/LICENSE                                常に出力
  share/aEngine/ThirdParty/README.md                    外部ライブラリ一覧
  share/aEngine/ThirdParty/miniaudio-LICENSE.txt        Miniaudioのビルド時
  share/aEngine/ThirdParty/COPYING                      OpenALのビルド時
  share/aEngine/ThirdParty/LICENSE-pffft                OpenALのビルド時
  share/aEngine/ThirdParty/OpenAL-SOURCE.txt            OpenALのビルド時
  share/aEngine/ThirdParty/openal-soft-1.25.2-source.tar.bz2 OpenALのビルド時
```

インストール済みの aEngine パッケージを使うアプリケーションでは、次の手順で配布に必要なファイルをコピーします。

実行ファイルのターゲットを作った後に、`aengine_stage_runtime_dependencies(<target>)` を呼びます。DLL 版では、リンクした aEngine の DLL が実行ファイルと同じディレクトリへコピーされます。次のライセンスと追加ファイルは、ターゲット出力先の `ThirdParty/` へコピーされます。aEngine をリンクする途中のライブラリやモジュールではなく、配布する `.exe` のターゲットへこの関数を使ってください。

- `aEngine-LICENSE.txt`: 常に配置
- `miniaudio-LICENSE.txt`: Miniaudio をビルドしたパッケージで配置
- `OpenAL32.dll`、`OpenAL-COPYING.txt`、`OpenAL-LICENSE-pffft.txt`、`OpenAL-SOURCE.txt`、対応するソースコード一式のアーカイブ: OpenAL をビルドしたパッケージで配置

`add_subdirectory()` で aEngine を追加する場合も、同じ関数を使えます。この関数を使わずに手作業で配布物を作る場合、DLL 版では必要な aEngine の DLL を実行ファイルと同じディレクトリへ置きます。また、aEngine 本体の `LICENSE` と Miniaudio の `ThirdParty/miniaudio/LICENSE` もコピーしてください。`OpenAL32.dll` を配布する場合は、ライセンスと案内だけでなく、対応するソースコード一式のアーカイブも配布物に含めます。ダウンロードで配布するときは、バイナリと同じ場所から同じような手順で取得できるようにしてください。

この関数がコピーする内容は、aEngine パッケージを作ったときに有効だったバックエンドで決まります。例えば、すべてのバックエンドを有効にしてパッケージを作ると、利用側が `aEngine::Miniaudio` だけを直接リンクしていても、パッケージ内にある OpenAL の実行用ファイルもコピーされます。配布物を小さくしたい場合は、aEngine パッケージを作る段階で、不要な `AENGINE_BUILD_*` 設定を `OFF` にしてください。

静的ライブラリ版と DLL 版のどちらも、公開 API に C++ 標準ライブラリの型を含みます。そのため、aEngine と利用側は同じ MSVC ツールセット、C/C++ ランタイム（CRT）、Debug / Release 構成でビルドしてください。aEngine 0.1.x では、ソースコードから使う API の互換性を保ちます。ただし、異なる MSVC 環境で作ったライブラリ間の互換性（ABI 互換性）や、バックエンド DLL の実行時読み込みは保証しません。

CMake ターゲットの選び方、登録関数、利用側と aEngine の役割分担については、[組み込みガイド](INTEGRATION.md)を参照してください。
