# Tremu のインストール

[English](en.md) · [Deutsch](de.md) · [Español](es.md) · [Français](fr.md) · **日本語** · [Русский](ru.md) · [中文](zh.md)

Tremu は Meta Quest 3 / 3S をガンシューティングの筐体に変えます。ゲームは大きな仮想スクリーンに映り、
WonderFitter のガンか Touch コントローラーで狙います。Tremu には**ゲーム、BIOS、ファームウェア、
キーは含まれていません**。ご自身のコピーを使ってください。

## 1. ヘッドセットの準備

1. **開発者モード**をオンにします。Meta Horizon モバイルアプリでヘッドセットを開き、
   「ヘッドセットの設定」›「開発者モード」へ（無料の Meta 開発者組織が必要です）。
2. PC にサイドロードツールを入れます：[Meta Quest Developer Hub](https://developers.meta.com/horizon/documentation/android-apps/meta-quest-developer-hub/)
   または [SideQuest](https://sidequestvr.com/)。ヘッドセットを USB で接続し、ヘッドセット内で USB デバッグを許可します。

## 2. Tremu をインストール

ツールで `tremu.apk` をインストールします（APK をヘッドセットにドラッグ＆ドロップ）。初回起動時に次を求められます：

- **すべてのファイルへのアクセス**：許可してください。Tremu は `Download/Roms` のゲームを読み、
  自分のファイルを `Tremu` フォルダに置きます。
- **付近のデバイス（Bluetooth）**：WonderFitter のガンのために許可してください。

Tremu は「ライブラリ」›「提供元不明」に表示されます。

## 3. ゲームを追加

| 何を | ヘッドセット上の場所 |
|---|---|
| ゲーム | `Download/Roms/<機種>`、機種ごとに 1 フォルダ（例：`Download/Roms/ps1`） |
| MAME のアーケードゲーム（Area 51、CarnEvil など） | `Download/Roms/mame/<ゲーム>`（解凍したもの、`.chd` と一緒に） |
| Wii U、PS3、PS Vita のゲーム | `Download/Roms/wiiu`、`Download/Roms/ps3`、`Download/Roms/vita` |
| BIOS（PS2、セガサターン、メガCD など） | `Tremu/bios`（PS2：`Tremu/bios/pcsx2/bios`） |
| Xbox のシステムファイル | `Tremu/Xbox/system` |
| ポスター | `Tremu/Banner`（足りないポスターは自動でダウンロード） |

ケーブルなしでも：カルーセルの下に「ゲームを追加：http://…」とコードが表示されます。同じ Wi-Fi の
PC やスマホでそのアドレスを開き、ファイルを置けば Tremu が整理します。
ファイル（BIOS、ハードディスクなど）が足りないときは、どれをどこに置くかがメッセージに出ます。

## 4. 連携アプリ

Tremu 自体がアーケード、ファミコン、スーパーファミコン、N64、メガドライブ、セガサターン、PS1、PS2、
PSP、ドリームキャスト、ゲームキューブ、Wii、Xbox、Xbox 360 をエミュレートします。次の 4 つは別アプリを
使います。Tremu がアプリを開き、ゲーム終了時にカルーセルへ戻ります：

| 対象 | アプリ | ソース |
|---|---|---|
| PS3 | ARMSX3（`com.armsx3`） | [proelitem/ARMSX3](https://github.com/proelitem/ARMSX3) |
| Wii U | Cemu（`info.cemu.cemu`） | [proelitem/Cemu](https://github.com/proelitem/Cemu)、ブランチ `tremu` |
| PS Vita | Vita3K（`org.vita3k.emulator`） | [proelitem/Vita3K](https://github.com/proelitem/Vita3K)、ブランチ `tremu` |
| Time Crisis VR | `org.timecrisis.quest` | [proelitem/time-crisis-vr](https://github.com/proelitem/time-crisis-vr) |

**必ずこれらのビルド**を使ってください。公式版は Tremu で選んだゲームを受け取れず、カルーセルにも
戻りません。公式版がすでに入っている場合（特に ARMSX3）は**先にアンインストール**してください。
署名が違うため Android が上書きを拒否します。
アプリがなくてもゲームのタイルは表示され、Tremu がどのアプリが必要か教えてくれます。
PS3 のファームウェア、PS Vita のファームウェア（`.PUP`、`Download/Roms/vita` に置く）、Wii U のキーはご自身で用意してください。

## 5. WonderFitter のガン

ガンの電源を入れれば、公式 WonderFitter アプリなしで Tremu が Bluetooth で見つけます。
ガンは**一度に 1 つのアプリしか受け付けません**。公式アプリが開いていたら閉じてください。
取り付けの設定（角度、オフセット）は Tremu のメニューにあり、ガンごとに保存されます。
ガンがなければ Touch コントローラーで狙います。

Wii リモコン（Wii のゲーム）：ヘッドセットに USB 接続した Mayflash の **DolphinBar** 経由のみ。

## 6. 一緒に遊ぶ

同じ Wi-Fi：1 台がゲームを始め、メニュー ›「マルチプレイ」›「ホスト（Wi-Fi）」を選びます。もう 1 台の
カルーセルに「参加：…」のタイルが出ます。両方のヘッドセットに**同じバージョンの Tremu と同じゲーム
ファイル**が必要です。MAME のゲームではホストがゲームを最初からやり直してプレイヤー 2 を待ち、
二人一緒に始めます。

インターネット経由：「インターネットでホスト」でコードが表示されるので相手に伝えます（メニュー ›
コードで参加）。有料サーバーは使いません。コードが出ない場合はルーターの UDP ポート 47617 を開けるか、
UPnP を有効にしてください。

## 終了とメニュー

左コントローラーのメニューボタン：ゲームのオプション。Tremu から起動したゲーム（PS3、Wii U、Vita、
Time Crisis VR）で 1.5 秒長押し：カルーセルに戻ります。
