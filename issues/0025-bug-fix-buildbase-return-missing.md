# buildbase.py の get_macos_osver() が return を欠き None を返す

- Priority: High
- Created: 2026-07-10
- Polished: 2026-07-10

## 目的

`buildbase.py` の `get_macos_osver()` 関数が `platform.mac_ver()[0]` の結果を変数に代入しているが `return` 文が欠落している。暗黙的に `None` が返り、macOS ビルドのプラットフォーム検出が破綻する。

## 優先度根拠

macOS 向けビルドでプラットフォームバージョンが正しく検出されず、ビルド設定の不一致によるビルド失敗や誤ったバイナリ生成につながる。High。

## 現状

`buildbase.py:2288-2289`:

```python
def get_macos_osver():
    osver = platform.mac_ver()[0]
    # return がない → None が返る
```

## 設計方針

`return osver` を追加する。

## 完了条件

- `get_macos_osver()` が正しい macOS バージョン文字列を返すこと
- `buildbase.py:2288-2289` に `return osver` が追加されていること
- `CHANGES.md` の `## develop` 配下、`### misc` セクションに `[FIX]` エントリを追記する:
  ```
  - [FIX] buildbase.py の get_macos_osver() が return を欠き None を返すのを修正する
    - @<担当者>
  ```
