# 音源入力

## ファイルから読み込む

`AudioSource(std::string filePath)` で音声ファイルを指定します。パスは UTF-8 で渡し、文字列の途中に NULL 文字を含めることはできません。相対パスの基準、アセット一覧の管理、独自パッケージからの取り出しは、利用側で行います。aEngine は UTF-8 のパスを、各バックエンドが使う形式へ変換します。

ファイルは `Play` の処理中にその場で開かれます。`Play` を呼んでいる間は、ファイルを変更したり削除したりしないでください。

## メモリから読み込む

`AudioSource(std::span<const std::byte> encodedBytes, std::string name)` で、WAV や MP3 などの圧縮されたままのバイト列を渡します。`name` は、失敗時に音源を見分ける名前として `Error::Message` へ追加されます。デコード形式を決めるためには使いません。

渡した `span` が参照されるのは、`Play` の処理中だけです。Miniaudio バックエンドは、再生中に必要なバイト列を内部へコピーします。XAudio2 と OpenAL は、`Play` の処理中に PCM へデコードします。そのため、`Play` から戻った後は、利用側で元のバッファーを解放できます。

## 対応形式

実際に読み込める形式は、使うバックエンドと OS のデコーダーによって異なります。

- Miniaudio: Miniaudio がデコードできる形式
- XAudio2: Windows Media Foundation がデコードできる形式
- OpenAL: Windows Media Foundation がデコードできる形式

aEngine 0.1 では、デコード済みの音声データ（PCM）を公開 API へ直接渡すことはできません。また、ストリーミング再生用のコールバックも提供していません。
