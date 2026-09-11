# android/Sora の説明を追加する

- Created: 2026-09-11
- Completed: {YYYY-MM-DD}
- Branch: feature/update-android-sora-description
- Polished: {YYYY-MM-DD}

## 目的

`android/Sora` が何のために存在するのか、どのような経緯で追加されたのかを説明するドキュメントまたは README を追加する。現状は説明がなく、位置づけを判断しづらい。

## 現状

- `android/Sora` はハンズフリー対応の際に追加された Android ライブラリ (Gradle プロジェクト) で、`build.gradle.kts` / `settings.gradle.kts` / `Sora` モジュールを持つ
- `android/Sora` に README などの説明はない
- `run.py` の Android の SDK ビルドで `android/Sora` の `./gradlew assembleRelease` を実行し、`Sora-release.aar` を `_install/<target>/<configuration>/sora/lib/Sora.aar` にコピーしている
- SDK 自体の説明は `doc/` にあるが、`android/Sora` の位置づけを説明した記述はない

## 設計方針

- `android/Sora` に README を追加し、目的 (ハンズフリー対応で必要になった理由)、`run.py` からのビルド方法、生成物 (`Sora-release.aar`) の扱いを記載する
- または `doc/` に `android/Sora` の説明を追加する
- 経緯は `git log` などから確認して記載する

## 完了条件

- `android/Sora` の目的と経緯、ビルド方法、生成物の扱いを説明したドキュメントまたは README が追加されていること
- `run.py` からのビルドとの関係が記載されていること

## Pending 理由

- `android/Sora` が追加された経緯を確認する必要がある
- ドキュメントの配置 (README か `doc/` か) の判断が必要
- ドキュメント追加であり、優先度が確定していない

## Pending 解除条件

- `android/Sora` の経緯が確認でき、ドキュメントの配置が決まったこと
