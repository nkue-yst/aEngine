# aEngine

aEngine は、ゲームエンジン、制作ツール、メディアビューアーなどに組み込める、Windows 向けの C++ サウンドエンジンです。
音声ファイルのパス、またはメモリ上の音声データを受け取り、`PlaybackHandle`（再生を識別するための値）を使って再生、停止、音量変更、状態確認を行います。

## 動作条件

- OS: Windows
- Visual Studio 2022 / C++23
- CMake 3.22以上

## プロジェクトへの追加

`AENGINE_BUILD_SHARED_LIBS` でリンク方法を選びます。既定値の `OFF` では静的ライブラリを作ります。`ON` では `aEngineCore.dll`、有効にしたバックエンドの DLL、`aEngineAllBackends.dll` を作ります。どちらを選んでも、CMake ターゲット名と登録関数は同じです。

### cmake による add_subdirectory

使うバックエンドだけを有効にしてから、aEngine を追加します。バックエンドごとのターゲットをリンクした場合は、そのバックエンドの登録関数も呼びます。

```cmake
set(AENGINE_BUILD_MINIAUDIO ON CACHE BOOL "" FORCE)
set(AENGINE_BUILD_XAUDIO2 OFF CACHE BOOL "" FORCE)
set(AENGINE_BUILD_OPENAL OFF CACHE BOOL "" FORCE)
set(AENGINE_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(AENGINE_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(ThirdParty/aEngine EXCLUDE_FROM_ALL)
target_link_libraries(MyApplication PRIVATE aEngine::Miniaudio)
```

### find_package

aEngine のルートディレクトリで、ビルドとインストールを行います。

```powershell
cmake -S . -B out/build -G "Visual Studio 17 2022" -A x64
cmake --build out/build --config Release --parallel
cmake --install out/build --config Release --prefix out/install
```

DLL 版を作る場合は、CMake の設定時に `-DAENGINE_BUILD_SHARED_LIBS=ON` を追加します。
利用側で使う `aEngine::` ターゲットは変わりません。

```cmake
find_package(aEngine CONFIG REQUIRED COMPONENTS Core Miniaudio)
target_link_libraries(MyApplication PRIVATE aEngine::Miniaudio)
```

実行ファイルのターゲットを作った後に、`aengine_stage_runtime_dependencies(MyApplication)` を呼びます。DLL 版では、リンクした aEngine の DLL が実行ファイルと同じディレクトリにコピーされます。また、aEngine の MIT ライセンスは常に `ThirdParty/` へコピーされます。有効なバックエンドに応じて、Miniaudio のライセンスや、OpenAL の `OpenAL32.dll`、ライセンス、ソースコードの案内、対応するソースコード一式もコピーされます。

再利用するライブラリやモジュールが aEngine をリンクし、別のプロジェクトが最終的な実行ファイルを作る場合は、そのプロジェクトからこの関数を呼びます。

DLL 版でも、現在の C++ API をそのまま DLL 越しに使います。aEngine と利用側は、同じ MSVC ツールセット、C/C++ ランタイム（CRT）、Debug / Release 構成でビルドしてください。

## 最小コード

```cpp
#include <aengine/aengine.h>
#include <aengine/backends.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

int main()
{
    auto registry = aengine::BackendRegistry();
    if (auto registration = aengine::RegisterMiniaudioBackend(registry); !registration)
    {
        std::cerr << registration.error().Message << std::endl;
        return 1;
    }

    auto createdEngine = registry.Create(
        aengine::BackendId::Miniaudio,
        aengine::AudioEngineCreateInfo());
    if (!createdEngine)
    {
        std::cerr << createdEngine.error().Message << std::endl;
        return 2;
    }

    auto engine = std::move(*createdEngine);
    auto description = aengine::PlaybackDescription();
    description.Source = aengine::AudioSource("Resources/BGM.mp3");
    description.Volume = 0.5f;

    auto playback = engine->Play(description);
    if (!playback)
    {
        std::cerr << playback.error().Message << std::endl;
        return 3;
    }

    while (true)
    {
        auto state = engine->State(*playback);
        if (!state)
        {
            std::cerr << state.error().Message << std::endl;
            return 4;
        }
        if (*state != aengine::PlaybackState::Playing)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (auto destroyed = engine->DestroyPlayback(*playback); !destroyed)
    {
        std::cerr << destroyed.error().Message << std::endl;
        return 5;
    }

    engine->Shutdown();
    return 0;
}
```

失敗する可能性がある処理は、`std::expected` をもとにした型で結果を返します。登録、生成、再生、操作の結果を毎回確認してください。バックエンド名から `BackendId` への変換方法と、使えなかった場合に別のバックエンドへ切り替えるかどうかは、利用側で決めます。完全な実行例は `samples/BasicPlayback` を参照してください。

サンプルは、バックエンド名と音声ファイルを引数で受け取ります。バックエンド名には `miniaudio`、`xaudio2`、`openal` のいずれかを指定します。大文字と小文字は区別しません。これ以外の名前を指定するとエラーになります。

```powershell
.\out\build\Release\aEngineBasicPlayback.exe miniaudio C:\Audio\BGM.wav
```

## CMake ターゲットと登録関数

|CMake ターゲット|登録関数|用途|
|---|---|---|
|`aEngine::Core`|なし|公開型、エラー、ハンドル、バックエンド一覧、共通の入力チェック|
|`aEngine::Miniaudio`|`RegisterMiniaudioBackend`|Miniaudio によるデコードと音声出力|
|`aEngine::XAudio2`|`RegisterXAudio2Backend`|Media Foundation によるデコードと XAudio2 による音声出力|
|`aEngine::OpenAL`|`RegisterOpenALBackend`|Media Foundation によるデコードと OpenAL Soft による音声出力|
|`aEngine::AllBackends`|`RegisterAllBackends`|ビルド時に有効にしたバックエンドをまとめて登録|

静的初期化による自動登録は行いません。リンクしたターゲットと、呼び出す登録関数を合わせてください。

## 利用側で管理するもの

aEngine を組み込む側は、`BackendRegistry`（使えるバックエンドの一覧）、`IAudioEngine`、有効な `PlaybackHandle` を管理します。エンジンの API は、すべて同じ音声処理用スレッドから呼びます。通常のファイルは UTF-8 のパスで渡します。独自パッケージ内の音源は、圧縮されたままのメモリ上のバイト列で渡します。メモリ上の音源は `Play` の処理中だけ参照されるため、`Play` から戻った後は元のバッファーを解放できます。

組み込み部分の構成、再生の管理方法、音源の読み出し、エラー処理、配布方法については、[組み込みガイド](docs/INTEGRATION.md)を参照してください。

## 文書

- [組み込みガイド](docs/INTEGRATION.md)
- [API とデータの管理](docs/API.md)
- [音源入力](docs/RESOURCE_IO.md)
- [バックエンド](docs/BACKENDS.md)
- [ビルドと配布](docs/BUILD.md)
- [スレッドとエラー](docs/THREADING_AND_ERRORS.md)
- [外部ライブラリ一覧](ThirdParty/README.md)


## ライセンス
aEngine は MIT ライセンスで公開します。Miniaudio と OpenAL Soft には、それぞれのライセンスが適用されます。
