# prek の clang-format フックのバージョンを固定し、ファイルを書き換えずに違反を検出する

- Created: 2026-11-20
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-pin-clang-format-version
- Polished: {YYYY-MM-DD}

## 目的

`prek.toml` の `clang-format` フックは `language = "system"` で PATH 上の `clang-format` を呼んでいる。
そのため、開発者の環境にインストールされた `clang-format` のバージョンによって整形結果が変わり、同じコミットでも手元で差分が出たり出なかったりする。
CI は `DEPS` の `APT_INSTALL_LLVM_VERSION=22` で `clang-format-22` を入れて実行しているため、手元のバージョンが CI と違えば、手元では通るのに CI で落ちる (またはその逆) ことが起きる。

momo は 2026-11-17 の `3aff06cd` (issue 0084) で同じ問題に対処しており、`clang-format==20.1.0` をフック環境に固定し、`-i` をやめて `--dry-run --Werror` にすることで、開発者の PATH に依存せず、ファイルを書き換えずに違反を検出する形にしている。
本 issue は sora-cpp-sdk でも同じ方式に揃え、バージョン差による整形結果の揺れをなくす。

## 現状

### フックの設定

`prek.toml` の `clang-format` フックは次のとおり。

```toml
{
  id = "clang-format",
  name = "clang-format",
  entry = "python3 run.py format",
  language = "system",
  pass_filenames = false,
  types_or = ["c", "c++", "objective-c", "objective-c++"]
}
```

`python3 run.py format` は `run.py` の `_format()` が `clang-format -i` を実行する。対象は `_format()` 内の `patterns` に列挙されており、`include/`、`src/`、`test/`、`examples/{messaging_recvonly_sample,sdl_sample,sumomo}/` の C / C++ / Objective-C ファイル 180 件である。

`-i` でファイルを直接書き換えるため、フックを実行すると作業ツリーが書き換わる。また、どのバージョンの `clang-format` が使われるかを `run.py` は制御していない (`_find_clang_binary()` は `clang-format-<n>` を 50 から 15 まで探し、見つからなければ `clang-format` を使う)。

### CI で使うバージョン

`.github/workflows/formatter.yml` は `DEPS` の `APT_INSTALL_LLVM_VERSION` (現在 22) を読み、`llvm.sh` で LLVM 22 を入れて `clang-format-22` を `apt install` する。その後 `python3 run.py format` を実行し、`git diff --exit-code` で差分があれば失敗させる。

つまり CI は常に LLVM 22 の `clang-format` を使うが、prek のフックは開発者の PATH に依存する。

### バージョン差で整形結果が変わることの実測

2026-11-20 に、リポジトリの C / C++ / Objective-C ファイル 180 件を各バージョンの `clang-format` で `--dry-run --Werror` にかけた結果は次のとおり。

| clang-format | 整形が必要なファイル |
| --- | --- |
| 23.1.6 | 0 / 180 |
| 20.1.8 | 0 / 180 |
| 18.1.8 | 1 / 180 |

18.1.8 で差分が出たのは `src/sora_client_context.cpp` で、`SoraClientContext::Create()` 内の `auto set_audio_device =` で始まるラムダ式の引数折り返しが対象である (`--dry-run` はこの代入行とラムダの引数 2 行を指摘する)。

この 1 件は古いバージョンが新しい整形ルールに追随していないために出るもので、CI では検出されない差分である。逆に、開発者が新しいバージョンを使っていて CI の 22 と結果が食い違う場合も、同じように手元と CI で結論がずれる。

### momo 側の状況 (参考)

momo の `prek.toml` は次の設定にしている。

```toml
[[repos.hooks]]
id = "clang-format"
name = "clang-format"
# CI の llvm.sh 20 と揃えたバージョンを固定して使い、開発者の PATH 上の
# clang-format に依存しないようにする
# ファイルを書き換えず、整形されていない場合だけコミットを失敗させる
entry = "clang-format --dry-run --Werror"
language = "python"
additional_dependencies = ["clang-format==20.1.0"]
types_or = ["c", "c++", "objective-c"]
```

momo に組み込まれた sora-cpp-sdk のコピーで整形差分が出た事例があったが、その原因は momo 側のコピーが上流より古く、日本語コメントへローカルに書き換えられていたことであり、sora-cpp-sdk 本体のファイルは `clang-format` 20.1.8 で差分が出ないことを確認済みである。本 issue はその原因とは別に、フックのバージョン固定と実行方式を sora-cpp-sdk 側でも揃えることを目的とする。

### 既存 issue との関係

open / closed / pending の issue、`CHANGES.md`、直近のコミット履歴に、prek の clang-format フックのバージョン固定や実行方式を扱った記録は見つからなかった。

## 設計方針

- `prek.toml` の `clang-format` フックを、momo と同じくバージョン固定 + `--dry-run --Werror` に変える
  - `language = "python"` と `additional_dependencies = ["clang-format==<version>"]` で、フック環境に固定バージョンを入れる
  - `entry = "clang-format --dry-run --Werror"` にして、ファイルを書き換えず、整形されていないファイルがあるとコミットを失敗させる
  - `pass_filenames` を既定 (真) にし、変更されたファイルだけを検査する。全ファイルを検査したい場合は `prek run --all-files` を使う
- 固定するバージョンは CI が使う LLVM のバージョン (`DEPS` の `APT_INSTALL_LLVM_VERSION`、現在 22) に合わせる
  - 22 系の PyPI パッケージ (`clang-format==22.x`) が利用できるか、`additional_dependencies` でインストールできるかを実装時に確認する。利用できない場合は、momo と同じく 20 系で固定し、`DEPS` の `APT_INSTALL_LLVM_VERSION` を 20 に下げるか、CI とフックのどちらを正とするかを決めて PR 本文に理由を残す
- 対象を `types_or = ["c", "c++", "objective-c", "objective-c++"]` にする
  - momo は `["c", "c++", "objective-c"]` だが、本リポジトリには `test/ios/hello/` の `.m` (Objective-C) と `src/mac/*.mm`、`test/ios/hello/*.mm` (Objective-C++) があり、`run.py format` の `_format()` も `.m` と `.mm` を対象にしている。現行フックの型指定を維持する
  - 現在の `run.py format` の対象は glob パターンで列挙しており、`types_or` は使われていない。フックで変更ファイルを受け取る方式に変えると、`third_party/` などの除外は `prek.toml` の `exclude` (`^(_build/|_install/|_source/|third_party/)`) と `clang-format` の `--force-exclude` に委ねることになる。意図どおりに除外されるかを実装時に確認する
- `python3 run.py format` と `run.py` の `_format()` は削除しない
  - CI の `.github/workflows/formatter.yml` が `python3 run.py format` を呼んでいる。フックだけを新しい方式にしても CI は従来どおり動かせる状態を保つ
- CI の `.github/workflows/formatter.yml` は変更しない
  - CI が使う LLVM のバージョンは `DEPS` の `APT_INSTALL_LLVM_VERSION` が正であり、本 issue でこの値を変える必要が生じた場合だけ最小限の変更にとどめる
- モックやスタブは利用しない

## テスト戦略

- 変更前の状態で、PATH 上の `clang-format` のバージョンを 18 系にすると `src/sora_client_context.cpp` が `--dry-run --Werror` で失敗し、20 系と 23 系では失敗しないことを確認する
- 変更後、`prek run --all-files` を実行し、`clang-format` が Passed になることを確認する
- 変更後、PATH 上の `clang-format` のバージョンを変えても `prek run --all-files` の `clang-format` の結果が変わらないことを確認する (フックが `additional_dependencies` の固定バージョンを使っていることの確認)
- 整形されていない状態を意図的に作り、`prek run --all-files` の `clang-format` が失敗すること、そのときファイルが書き換わらないことを確認する (`--dry-run` の確認)
- `python3 run.py format` が引き続き動作し、フックと同じバージョンの `clang-format` を使った場合に差分が出ないことを確認する
- `third_party/` 配下のファイルが検査対象から除外されることを確認する
- モックやスタブを利用していないことを確認する

## 完了条件

- `prek.toml` の `clang-format` フックが `additional_dependencies` でバージョンを固定し、`--dry-run --Werror` でファイルを書き換えない
- `prek run --all-files` の `clang-format` が Passed になる
- PATH 上の `clang-format` のバージョンを変えても、`prek run --all-files` の `clang-format` の結果が変わらない
- 整形されていないファイルがある場合にフックが失敗し、ファイルが書き換わらない
- `python3 run.py format` が引き続き動作する
- CI の `.github/workflows/formatter.yml` が成功する
- モックやスタブを利用していない

## 解決方法

{どのように対応するのかを明確にすること}
