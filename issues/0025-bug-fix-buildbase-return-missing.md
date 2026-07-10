# buildbase.py の get_macos_osver() が return を欠き None を返す

- Priority: Low
- Created: 2026-07-10
- Completed: {YYYY-MM-DD}
- Model: DeepSeek V4 Pro
- Branch: feature/fix-buildbase-return-missing
- Polished: 2026-07-10

## 目的

`buildbase.py` の `get_macos_osver()` 関数は `platform.mac_ver()[0]` を式文として評価するだけで、結果をどこにも代入せず `return` 文もない。そのため関数は暗黙的に `None` を返す。 `get_windows_osver()` が `return` を持つのに対し、macOS 版だけが返り値を返さない非対称な実装になっている。

## 優先度根拠

現状 macOS の `osver` は消費側が存在しないため実害はない（後述「現状」参照）。将来 macOS の `osver` を参照するコードが追加された際に `None` が伝播して初めて顕在化する潜在バグである。実害がなく修正も 1 行で済むため Low。

## 現状

`buildbase.py:2288-2289` の実際のコード:

```python
def get_macos_osver():
    platform.mac_ver()[0]
```

`osver` への代入すら存在せず、`platform.mac_ver()[0]` の評価結果は捨てられている。この関数が返す `None` は次の経路で伝播する:

- `get_build_platform()` (`buildbase.py:2299`) で `osver = get_macos_osver()` に代入され、`PlatformTarget(os, osver, arch)` に渡される
- `run.py:686` で `Platform("macos", get_macos_osver(), "arm64")` に渡される

ただし `osver` を実際に参照するのは Ubuntu / Jetson のコードパスのみで、macOS のコードパス（`PlatformTarget.package_name` の `f"macos_{self.arch}"`、`get_webrtc_platform()` の `f"macos_{platform.target.arch}"`、`Platform.__init__()` の macOS バリデーション（`buildbase.py:2425-2433`、`os` と `arch` のみ検証））は `osver` を一切参照しない。したがって現状ではビルド失敗や誤ったバイナリ生成は発生しない。

## 設計方針

`get_windows_osver()` (`buildbase.py:2278-2285`) と同様に返り値を返すようにする。`platform.mac_ver()[0]` を `return platform.mac_ver()[0]` に変更する。

## 完了条件

- `buildbase.py:2289` の `platform.mac_ver()[0]` が `return platform.mac_ver()[0]` に修正されていること
- macOS 環境で `get_macos_osver()` を呼び出し、`None` ではない macOS バージョン文字列（例: `"15.2"`）が返ることを確認すること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] buildbase.py の get_macos_osver() が return を欠き None を返すのを修正する
    - @<担当者>
  ```
