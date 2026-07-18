# 外部ライブラリ一覧

## miniaudio

- リポジトリ: `https://github.com/mackron/miniaudio.git`
- 使用するコミット: `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`
- バージョン: 0.11.25
- ライセンス: Public Domain または MIT No Attribution

`ThirdParty/miniaudio` には、上記コミットの作業ツリーを置きます。公開リポジトリで使う Git サブモジュールの設定は、`.gitmodules` に記録しています。親リポジトリが参照するコミットは、aEngine のリポジトリを公開するときに記録します。Miniaudio のソースコードは変更しません。aEngine の Miniaudio バックエンドは、`miniaudio.c` を静的ライブラリへ直接コンパイルします。

## OpenAL Soft

- 配布ファイル: `https://openal-soft.org/openal-binaries/openal-soft-1.25.2-bin.zip`
- ソースコード一式: `https://openal-soft.org/openal-releases/openal-soft-1.25.2.tar.bz2`
- バージョン: 1.25.2
- 配布ファイルの SHA-256: `67A0C4B800BD860C93C04F38CAF8CBE4875F9C84700AC430EFC451F70E265434`
- Win64 DLL の SHA-256: `3963B06E319180700BE0BCC0D027336664BB3A4975876B4A5442F10746FAB5B4`
- ソースコードの SHA-256: `1DBAAC44E7579D5BC8847CA8DB4B2E8B9FD3961041F35EE20DEF4958301E1089`
- 元のリポジトリのタグが指すコミット: `b2c48f7718ef3fcf67921a8b6534c4914e328970`
- ライセンス: LGPL-2.0-or-later
- 追加の案内: PFFFT `LICENSE-pffft`

OpenAL Soft は、ソースコードの Git サブモジュールとしては追加しません。`AENGINE_FETCH_OPENAL_RUNTIME=ON` で CMake を設定するときに、配布ファイルとソースコードのアーカイブを取得するか、キャッシュから再利用します。両方の SHA-256 を確認した後、Windows x64 用の `soft_oal.dll` を `OpenAL32.dll` という名前で実行用ディレクトリへ置きます。配布時は、`OpenAL32.dll`、`COPYING`、PFFFT の案内、`OpenAL-SOURCE.txt`、対応するソースコード一式のアーカイブを、同じ配布物またはダウンロード場所に用意します。
