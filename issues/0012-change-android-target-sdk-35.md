# Android targetSdk を 35 (Android 15) に引き上げる検討と対応

- Priority: Medium
- Created: 2026-06-25
- Completed: {YYYY-MM-DD}
- Model: deepseek-v4-flash
- Branch: feature/change-android-target-sdk-35
- Polished: {YYYY-MM-DD}

## 目的

現在の compileSdk / targetSdk 34 を 35 (Android 15) に引き上げるかどうかを
調査・検討し、必要に応じて実装する。

## 優先度根拠

Medium。Google Play の target API level 要件 (2025 年 8 月より API 35 以上) は
アプリ向けの要件であり、ライブラリとして提供される sora-cpp-sdk に
直接適用されるものではない。そのため対応は必須ではないが、
最新の Android ツールチェインに追従し、今後の API 変更に備えるために
対応しておくことが望ましい。

## 調査結果

### Android 14 (API 34) の behavior changes

現在 targetSdk 34 である sora-cpp-sdk に対して、Android 14 の全 behavior
changes (12 項目) の影響を分析した結果、**すべて影響なし** と判断した。

特に重要な項目:

| 変更 | sora-cpp-sdk への影響 |
|---|---|
| **Runtime-registered receivers export behavior** | システムブロードキャスト例外により**影響なし**。ドキュメントには "then it shouldn't specify a flag" と明記されており、フラグを指定してはいけない。SDK の 3 箇所の registerReceiver はすべてシステムブロードキャストのみを受信する |
| **BLUETOOTH_CONNECT permission enforcement** | 影響なし。`getProfileConnectionState()` 限定の強化であり SDK はこの API を使用していない |
| **Foreground service types required** | 影響なし。SDK はフォアグラウンドサービスを使用していない |
| **OpenJDK 17 updates** | 影響なし。正規表現 / UUID / ProGuard いずれも使用していない |
| **Implicit/pending intent restrictions** | 影響なし。自作 Intent は送信していない |
| **Dynamic code loading** | 影響なし。使用していない |
| **Background activity launch restrictions** | 影響なし。PendingIntent / bindService を使用していない |
| **JobScheduler ANR** | 影響なし。使用していない |
| **Zip path traversal** | 影響なし。使用していない |
| **MediaProjection consent** | 影響なし。使用していない |
| **Schedule exact alarms** | 影響なし。使用していない |
| **Partial photo/video access** | 影響なし。使用していない |

### Android 15 (API 35) の behavior changes

Android 15 の behavior changes のうち、sora-cpp-sdk に影響するもの:

| 変更 | 影響 | 詳細 |
|---|---|---|
| **Audio Focus 制限** | 要確認 | 最前面アプリまたは FGS 実行中のみ Audio Focus リクエスト可能。`SoraAudioManagerBase` の `start()` 内の `requestAudioFocus()` がバックグラウンド時に `AUDIOFOCUS_REQUEST_FAILED` を返す可能性がある。ただし現状のコードは失敗時に `Log.e` でログ出力するのみでクラッシュはしない |
| **OpenJDK API 変更** | 影響なし | `Arrays.asList(...).toArray()` の戻り値型が `Object[]` に変更。`SoraAudioManager` モジュールの全 Java コードを audit した結果、このパターンは使用されていない。対応不要 |
| **TLS 1.0/1.1 禁止** | 影響なし | 全アプリ対象。サーバー側が TLS 1.2+ 対応済みであることの確認は必要だが SDK 側の変更は不要 |

影響なしと判断した主な変更:
- Bluetooth / SCO 関連の変更は Android 15 には存在しない
- `setCommunicationDevice()` / `getAvailableCommunicationDevices()` に変更なし
- BOOT_COMPLETED レシーバ制限: SDK は未使用
- SYSTEM_ALERT_WINDOW 制限: SDK は未使用
- Foreground service 変更: SDK は FGS 未使用
- DND 制限: SDK は未使用
- Safer intents: 標準システムブロードキャストのみ受信のため影響なし

### 下位互換について

- システムブロードキャスト例外により registerReceiver にフラグ追加は不要
- minSdk を引き上げる必要はなく、下位互換 (minSdk 21 / 29) を維持できる
- 防御的にフラグを追加する場合でも framework API の 3 引数版 + バージョンチェックで対応可能。AndroidX は不要

### Runtime-registered receivers export behavior の詳細

この issue の主題である registerReceiver の export フラグ問題について、
Android 公式ドキュメントの記述と sora-cpp-sdk の実装を照合する。

**Android 14 の要件** (targetSdk 34 以上):

> Apps and services that target Android 14 (API level 34) or higher and use
> context-registered receivers are required to specify a flag to indicate
> whether or not the receiver should be exported to all other apps on the
> device: either RECEIVER_EXPORTED or RECEIVER_NOT_EXPORTED, respectively.
>
> **Exception for receivers that receive only system broadcasts:**
> If your app is registering a receiver **only for system broadcasts** through
> Context#registerReceiver methods, such as Context#registerReceiver(),
> then it **shouldn't** specify a flag when registering the receiver.
>
> (出典: Android 14 Behavior changes — Runtime-registered broadcasts receivers
> must specify export behavior)

ドキュメントは「フラグを指定しなくてもよい」ではなく
「指定すべきでない (shouldn't)」と明記しており、システムブロードキャスト
のみを受信する Receiver は例外ではなく **フラグ指定禁止対象** である。

**システムブロードキャストとは:**

Android 公式ドキュメントでは、システムブロードキャストを以下のように
定義している:

> System broadcasts are messages that are sent by the Android system,
> such as when the device boots up, when the device is connected to a
> power source, and when the screen turns off or on.
>
> (出典: Android Developers — Broadcasts overview)

sora-cpp-sdk が registerReceiver で購読している Action は以下の 4 つで、
いずれもシステムブロードキャストである:

| Action | 送信元 | 種別 |
|---|---|---|
| `Intent.ACTION_HEADSET_PLUG` | システム (有線ヘッドセット抜き差し) | システムブロードキャスト |
| `AudioManager.ACTION_SCO_AUDIO_STATE_UPDATED` | システム (SCO オーディオ状態変化) | システムブロードキャスト |
| `BluetoothHeadset.ACTION_CONNECTION_STATE_CHANGED` | システム (Bluetooth 接続状態変化) | システムブロードキャスト |
| `BluetoothHeadset.ACTION_AUDIO_STATE_CHANGED` | システム (Bluetooth オーディオ状態変化) | システムブロードキャスト |

これらの Action はアプリケーションからは送信できず、Android システム
のみが送信する。したがってドキュメントの例外条件を満たし、
**フラグを指定してはいけない** 状態である。

### AndroidX の要否 (詳細)

#### AndroidX が必要になる条件

`RECEIVER_EXPORTED` または `RECEIVER_NOT_EXPORTED` フラグを
`registerReceiver` に渡す方法は 2 つある:

| 方法 | コード例 | AndroidX の要否 |
|---|---|---|
| ContextCompat.registerReceiver() | `ContextCompat.registerReceiver(ctx, receiver, filter, RECEIVER_NOT_EXPORTED)` | **必要** (androidx.core) |
| Context.registerReceiver() 3 引数版 | `context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)` + `Build.VERSION.SDK_INT` チェック | **不要** |

`Context.RECEIVER_NOT_EXPORTED` は API 33 (Android 13) で追加された。
compileSdk 34 以上であれば参照可能。実行時は `Build.VERSION.SDK_INT >=
Build.VERSION_CODES.TIRAMISU` (API 33) のガードが必要。

PR #336 は ContextCompat 版を採用した。ContextCompat は内部的に
バージョンチェックを自動で行うため、呼び出し側で `Build.VERSION` の
条件分岐を書かなくてよいというメリットがある。

#### なぜ AndroidX が不要と判断したか

**第 1 層: フラグ自体が不要**

上記の通り、システムブロードキャスト例外により SDK の Receiver は
フラグを指定してはいけない。ContextCompat も Context 直呼び出しも
不要。

**第 2 層: 仮に必要でも framework API で代替可能**

もし将来システムブロードキャスト以外のカスタム Action を追加し、
フラグが必要になったとしても、以下の framework API で対応可能:

```java
if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
    context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
} else {
    context.registerReceiver(receiver, filter);
}
```

このコードは compileSdk 34 以上であればコンパイル可能で、
実行時は API 33 未満のデバイスで 2 引数版にフォールバックする。
`androidx.core` への依存は一切発生しない。

**第 3 層: AndroidX が必要な他の変更もない**

- Audio Focus 制限対応 → framework API のみ (`Log.w` への変更)
- OpenJDK API 変更 → コード audit の結果、対象パターンなし
- TLS 1.0/1.1 禁止 → サーバー側の問題、sdk は影響なし

#### PR #336 (revert) の正当性

PR #336 の revert は正しい判断だった。理由は以下の 1 点に集約される:

1. **変更自体が不要だった**: システムブロードキャスト例外の存在を
   認識しておらず、フラグ付与が必要のないケースに AndroidX 依存を
   導入した。不要な依存を増やすことはライブラリとして望ましくない

代替案 (framework API + バージョンチェック) であれば AndroidX 依存は
発生しない。ただしそれも現時点では不要である。

### 既存依存関係への影響

- 新たな AndroidX 依存は追加しない
- 既存の依存 (`androidx.appcompat:appcompat` 等) は変更なし
- AAR の公開 API サーフェスに変更はない
- ホストアプリ側で追加の設定や依存注入は不要

## 設計方針

### 選択肢

| 選択肢 | compileSdk | targetSdk | 実装コスト |
|---|---|---|---|
| A: 現状維持 | 34 | 34 | なし |
| B: compileSdk のみ 35 | 35 | 34 | 低 (1行変更) |
| C: 両方 35 | 35 | 35 | 低 (2行変更 + コード audit) |

### 推奨

選択肢 C。理由:
- compileSdk と targetSdk の乖離を避けられる
- OpenJDK 変更や Audio Focus 制限への影響は実際のコード audit と動作確認で軽微と予想
- 最新のツールチェインに追従できる

### Audio Focus 制限への対応方針

`requestAudioFocus()` が `AUDIOFOCUS_REQUEST_FAILED` を返した場合の
ログレベルを `Log.e` から `Log.w` に下げる (クラッシュではなく非 fatal
なため)。フォアグラウンドサービスは sora-cpp-sdk としては管理しない
(必要ならホストアプリ側で対応)。

### OpenJDK API 変更への対応方針

`SoraAudioManager` モジュールの全 Java コードを audit した結果、
`Arrays.asList(...).toArray()` パターンは **使用されていない**。
その他 OpenJDK 17/21 の変更 (正規表現、UUID、`removeFirst`/`removeLast` 等)
も影響しない。対応不要。

## 完了条件

- 調査結果を踏まえて選択肢 A / B / C のいずれかに決定していること
- 選択肢 B または C の場合、決定した内容に応じたファイル変更が完了していること
- compileSdk を 35 にしてビルド・テストが正常に通ること (選択肢 B/C の場合)

## 決定後の実装内容

### 決定内容に応じた作業

| 選択肢 | 作業 |
|---|---|
| A: 現状維持 | `CHANGES.md` に「見送り」のエントリを追加 (任意) |
| B: compileSdk のみ 35 | compileSdk の値を 34 → 35 に変更 + `CHANGES.md` |
| C: 両方 35 | compileSdk/targetSdk を 35 に変更 + Audio Focus ログ調整 + `CHANGES.md` |

### 選択肢 C を選んだ場合の具体手順

- `Sora` モジュールの build.gradle の compileSdk を 34 → 35 に変更
- テストアプリの build.gradle の compileSdk/targetSdk を 34 → 35 に変更
- `SoraAudioManagerBase` の Audio Focus 失敗時のログレベルを `Log.e` → `Log.w` に変更 (任意)
- CI に Android 35 platform が存在することを確認
- `CHANGES.md` にエントリを追加
