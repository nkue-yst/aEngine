
## aEngine と利用側の役割

aEngine は、音源のデコード、音声デバイス、再生中のデータ、`PlaybackHandle` を管理するサウンドエンジンです。aEngine を組み込む側では、次のものを管理します。

- ビルドするバックエンドと、設定名から `BackendId` への変換方法
- `BackendRegistry`、`IAudioEngine`、使用中のハンドル
- 音声処理用スレッドと、そのスレッドへ処理を渡す仕組み
- アセット一覧、仮想パス、独自パッケージから音源を読み出す処理
- 利用者へエラーを表示する処理と、別のバックエンドへ切り替える場合のルール
- 実行ファイルと、実行に必要な DLL やライセンスの配布

aEngine は、プロジェクト形式、アセット ID、呼び出し元が持つ追加情報、独自パッケージの中身を解釈しません。これらを扱う接続処理は利用側に置きます。読み出した後のファイルパス、または圧縮されたままのバイト列だけを aEngine へ渡してください。

## リンク方法

`AENGINE_BUILD_SHARED_LIBS=OFF` では、公開する各部品を静的ライブラリとしてビルドします。`ON` では、部品ごとに DLL を作ります。既定値は `OFF` です。CMake ターゲットと API はどちらも同じです。DLL 版では、Core、各バックエンド、AllBackends がそれぞれ実行用 DLL になります。実行中に追加するプラグインではなく、CMake の設定時に決めるリンク方法です。

DLL 版では、aEngine と利用側を同じ MSVC ツールセット、C/C++ ランタイム（CRT）、Debug / Release 構成でビルドしてください。公開 API は C++ 標準ライブラリの型を含むため、異なるツールチェーンで作ったライブラリ間の互換性（ABI 互換性）や、バックエンド DLL の実行中の入れ替えは保証しません。

## CMake ターゲットと登録

CMake の設定でビルドするバックエンドを選び、リンクしたターゲットに対応する登録関数を呼びます。

|CMake の設定|ターゲット|登録関数|
|---|---|---|
|`AENGINE_BUILD_MINIAUDIO`|`aEngine::Miniaudio`|`RegisterMiniaudioBackend`|
|`AENGINE_BUILD_XAUDIO2`|`aEngine::XAudio2`|`RegisterXAudio2Backend`|
|`AENGINE_BUILD_OPENAL`|`aEngine::OpenAL`|`RegisterOpenALBackend`|

複数のバックエンドを使う場合は、`aEngine::AllBackends` をリンクして `RegisterAllBackends` を呼べます。この関数が登録するのは、aEngine の CMake 設定で有効にしたバックエンドだけです。登録していない `BackendId` を `BackendRegistry::Create` へ渡すと、`BackendUnavailable` になります。

静的初期化による自動登録は行いません。また、未登録のバックエンドから別のバックエンドへ自動で切り替えることもありません。設定文字列の表記をそろえる処理と、使えなかったときの切り替え処理は、利用側の仕様として実装してください。

## データをどこで管理するか

推奨する構成は次のとおりです。

```text
利用側のアプリケーション
  ├─ 音声処理用スレッド / 処理待ちキュー
  ├─ アセットとパッケージから音源を読み出す処理
  └─ aEngineとの接続部分
       ├─ BackendRegistry
       ├─ unique_ptr<IAudioEngine>
       └─ 再生中の記録
            ├─ 利用側が必要とする追加情報
            └─ PlaybackHandle
```

`BackendRegistry::Create` はエンジンを作り、その呼び出しの中で初期化します。`BackendRegistry` は、プログラム全体で自動的に1つだけ共有される仕組み（シングルトン）ではありません。エンジンを作った後も、別の `BackendRegistry` や別のエンジンを独立して使えます。

`PlaybackHandle` には、作成元のエンジンと ID を見分ける情報があります。別のエンジンで作ったハンドル、`DestroyPlayback` で破棄したハンドル、`Shutdown` より前に受け取ったハンドルは再利用できません。利用側の再生記録には、ハンドルと、アセット ID や表示名などの追加情報をまとめて保存してください。

基本的な使用の流れは次のとおりです。

1. `BackendRegistry` へ必要なバックエンドを登録する。
2. 選択した `BackendId` からエンジンを作る。
3. `Play` が成功して有効なハンドルが返った場合だけ、再生中の記録へ追加する。
4. `State` で `Playing` を監視する。
5. `Finished` になった後、または停止を求めた後に `DestroyPlayback` を呼ぶ。破棄に成功したら、再生中の記録から削除する。
6. アプリケーションを終了するときは、残っているハンドルを破棄してから、エンジンを `Shutdown` または破棄する。

`Stop` と `StopAll` はハンドルを破棄しません。最後まで再生したハンドルも、`DestroyPlayback` を呼ぶまでは有効です。

一時的なバックエンドのエラーで `State`、`Stop`、`DestroyPlayback` が失敗した場合は、ハンドルを再生中の記録に残して再試行します。`InvalidHandle` だけは、作成元のエンジンがそのハンドルをすでに管理していないことを示します。この場合は、もう一度破棄せずに記録から削除できます。`IAudioEngine` から独自バックエンドを作る場合のハンドル生成ルールは、[API とデータの管理](API.md)を参照してください。

## スレッド

`IAudioEngine` は複数のスレッドから同時に使えません。バックエンド登録後のエンジン作成、`Play`、`State`、`SetVolume`、`Stop`、`DestroyPlayback`、`StopAll`、`Shutdown` は、すべて同じ音声処理用スレッドから呼びます。

ゲーム用スレッドや UI 用スレッドからの要求は、利用側の処理待ちキューを通して音声処理用スレッドへ渡します。aEngine の診断用コールバックは、API の処理中に同じスレッドで実行されます。コールバックの中から同じエンジンの API を呼ばないでください。

## 音源を読み出す

通常のファイルは、UTF-8 のパスで渡します。

```cpp
auto description = aengine::PlaybackDescription();
description.Source = aengine::AudioSource(resolvedUtf8Path);
description.Volume = 1.0f;
auto playback = engine->Play(description);
```

独自パッケージやアセットデータベース内の音源は、利用側で読み出し、圧縮されたままのバイト列として渡します。

```cpp
auto encodedBytes = package.Read(virtualPath);
auto description = aengine::PlaybackDescription();
description.Source = aengine::AudioSource(
    std::span<const std::byte>(encodedBytes.data(), encodedBytes.size()),
    virtualPath);

auto playback = engine->Play(description);
if (!playback)
{
    Report(playback.error());
    return;
}

// spanが参照されるのはPlayの処理中だけなので、ここでencodedBytesを解放できます。
```

`name` は、エラーに付け加える音源の名前です。拡張子からデコーダーを選ぶためには使いません。ファイルとメモリのどちらから渡した音源も、`Play` の処理中にその場で開くか、デコードを始めます。相対パスの基準、パッケージ内に音源があるかの確認、ハッシュ値の確認、最大サイズは、利用側で決めます。

## エラーと診断情報

失敗する可能性がある API は、`Expected<T>` または `Result` を返します。すべての結果を確認してください。失敗したときは、ハンドルやエンジンを使用中のものとして登録しないでください。

主なエラーは次のとおりです。

- `BackendUnavailable`: 対象のバックエンドが未登録、または実行に必要な DLL がない
- `DeviceUnavailable`: 利用できる既定の音声デバイスがない
- `ResourceLoadFailed` / `DecodeFailed`: 音源を開けない、またはデコードできない
- `PlaybackFailed`: OS や外部ライブラリが持つ再生機能の操作に失敗した
- `InvalidHandle`: 別のエンジンのハンドル、破棄済みのハンドル、または古いハンドルを渡した

`DiagnosticCallback` が返す情報は、原因を調べるための補助情報です。成功したかどうかは、API の戻り値で判断します。別のバックエンドへ切り替える場合も、元のエラーをログへ残し、利用側の設定に従って別の `BackendId` を明示的に指定してください。

## 実行用 DLL を探す場所

XAudio2 と OpenAL では、`AudioEngineCreateInfo::RuntimeLibrarySearchPaths` に DLL を探すディレクトリを指定できます。配布するアプリケーションでは、最終的な実行ファイルの場所をもとに作った絶対パスを渡すことを推奨します。標準の検索順と安全上の制限については、[バックエンド](BACKENDS.md)を参照してください。

## 配布

`aengine_stage_runtime_dependencies(<target>)` は、最終的な実行ファイルのターゲットへ使います。

```cmake
add_executable(MyApplication main.cpp)
target_link_libraries(MyApplication PRIVATE MySoundModule)
aengine_stage_runtime_dependencies(MyApplication)
```

DLL 版では、この関数がリンクされた aEngine の DLL を実行ファイルと同じディレクトリへコピーします。リンク方法にかかわらず、ターゲット出力先の `ThirdParty/` には、aEngine 本体と有効なバックエンドに必要なライセンス、追加の実行用ファイルがコピーされます。OpenAL を含む場合は、`OpenAL32.dll` だけでなく、COPYING、PFFFT の案内、ソースコードの案内、対応するソースコード一式のアーカイブも配布物へ含めてください。

すべてのバックエンドを有効にしてインストール用パッケージを作った場合、この関数はパッケージ内にある各バックエンドの実行用ファイルと案内もコピーします。配布物を小さくしたい場合は、aEngine パッケージを作る段階で、不要な `AENGINE_BUILD_*` 設定を `OFF` にしてください。
