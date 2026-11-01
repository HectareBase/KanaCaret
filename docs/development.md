# 開発手順

## 準備

- Windows 11
- Visual Studio 2022 以降(「C++ によるデスクトップ開発」。CMake と Ninja も同梱のものを使う)

## ビルド

```bat
build.cmd
```

`build.cmd` が Visual Studio を探し、CMake(Ninja、Release)でビルドする。

- **起動中の `KanaCaret.exe` があるとリンクに失敗する**(`LNK1104`)。先に終了させる。
  ```powershell
  Get-Process KanaCaret -ErrorAction SilentlyContinue | Stop-Process
  ```
- 警告は出ない状態を保つ(`/W4`)。

## フォルダー構成と出力先

| 場所 | 中身 | 公開 |
|---|---|---|
| `src/`, `resources/`, `probe/`, `tools/` | ソース、リソース、ツール | する |
| `docs/` | 設計ドキュメント | する |
| `build/` | ビルド成果物 | しない |
| `dist/` | 配布用 zip | しない(Releases に載せる) |
| `local/logs/` | 計測ログ | しない |
| `local/checks/` | 確認用スクリーンショット | しない |

`local/` には、使っているアプリ名・画面座標・画面の内容が入るので、公開しない(`.gitignore` 済み)。ツールは既定でここに書く。

| ツール | 出力 | 名前 |
|---|---|---|
| `ime-probe.exe` | `local/logs/` | `probe-<yyyyMMdd-HHmmss>.log`(値が変わるたびの記録)、同名の `.csv`(アプリ別の集計) |
| `tools/measure-load.ps1` | `local/logs/` | `stats-<yyyyMMdd-HHmmss>.log` |
| `tools/check-app.ps1` | `local/checks/<yyyyMMdd-HHmmss>/` | `<Target>-a.png`、`<Target>-kana.png` |
| `badge-preview.exe` | 引数で指定 | 例: `local/checks/badges.png` |
| `tools/package.ps1` | `dist/` | `KanaCaret-<バージョン>.zip` |

利用者の PC に書くもの: 設定 `%APPDATA%\KanaCaret\settings.ini`、自動起動の Run キー、`--stats`(パス指定なし)のときの `%TEMP%\kanacaret-stats.log`。

## ツール

### 表示の確認: `tools/check-app.ps1`

```bat
powershell -ExecutionPolicy Bypass -File tools\check-app.ps1 -Target notepad -Ime
```

インジケーターを起動した状態で実行する。対象のアプリを開いて「abc」と入力し、インジケーターの位置(物理ピクセル)を表示して、周辺を撮影する。`-Ime` を付けると IME をオンにした「あ」も撮影し、最後に元へ戻す。Target は `notepad` / `explorer` / `start` / `terminal` / `settings`。

- 実際にウィンドウを開いてキー入力するので、実行中はほかの操作をしない。利用者が他のアプリを操作していると、Windows は前面の切り替えを許さない。その場合、スクリプトは他のアプリに入力しないよう、キー入力の前に中止する(開いたウィンドウは閉じる)。
- 自分で開いたウィンドウだけを閉じる。設定アプリは 1 つしか開けないので、開いていれば既存のウィンドウを使い、入力した文字を消して閉じずに残す。
- 結果の `visible=True` と位置に加え、撮影した画像で、キャレットの真上に出ているかを目で確かめる。

### 負荷の計測: `tools/measure-load.ps1`

```bat
powershell -ExecutionPolicy Bypass -File tools\measure-load.ps1 -Seconds 60
```

インジケーターを `--stats` 付きで起動し直し、CPU・メモリと内訳(10 秒ごと)を表示する。計測が終わると元の状態に戻す。何もしていないときの負荷を測るなら、計測中は操作しない。内訳の各項目:

| 項目 | 意味 |
|---|---|
| `events` | 届いた WinEvent の数(`caret` = キャレット、`focus` = フォーカス) |
| `updates` | `Update()` の回数 |
| `ime` | IME の問い合わせ回数 |
| `locate` | Win32/MSAA での入力位置の問い合わせ回数 |
| `uia` | UIA の問い合わせ回数 |

### 取得方法の検証: `ime-probe.exe`

```bat
build\probe\ime-probe.exe
```

手前のアプリの IME 状態と、3 つの方法それぞれで取った入力位置を記録する。キャレットの上に方法ごとの色の四角(赤 = Win32、緑 = MSAA、青 = UIA)が並ぶので、位置が正しいかを目で確かめられる。Ctrl+C で終了すると集計を書き出す。オプション: `--no-overlay`、`--interval <ms>`、`--duration <秒>`、`--out <フォルダー>`。

ログの読み方:

```
19:09:00.619 notepad.exe [RichEditD2DPT] PMv2/120 | IME あ open=1 conv=0x0019 | W32 (812,433 2x24) | MSAA ... | UIA (812,433 1x24)/2  (UIA 900us)
```

- `[...]` はフォーカスのあるウィンドウのクラス名、`PMv2/120` はその DPI 対応方式と DPI
- `open` / `conv` は IME の ON/OFF と変換モード(`conv` の 0x1 ビットが日本語入力)
- 座標は物理ピクセルで `(x,y 幅x高さ)`。`!画面外` は異常値
- UIA の末尾 `/1` `/2` `/3` は、1 = 範囲そのもの、2 = 直後の 1 文字の左端、3 = 直前の 1 文字の右端
- CSV の `caret_events` が 0 より大きいアプリは、キャレット移動の通知で追従できる

### 見た目の確認: `badge-preview.exe`

```bat
build\badge-preview.exe local\checks\badges.png
build\badge-preview.exe --ico resources\app.ico
```

1 つ目は、ライト/ダーク × あ/a × 100/150/200% の 12 通りを 1 枚に並べる(Windows の設定を変えずに確認できる)。2 つ目は exe のアイコンを作り直す。

## 変更したときの確認

変更の内容に応じて、次を確かめる。

| 変更したもの | 確認 |
|---|---|
| どれでも | `build.cmd` が警告なしで通る |
| 入力位置・IME 判定 | `check-app.ps1` で notepad / explorer / terminal / settings(`-Ime` 付き)。撮影した画像で位置を確かめる |
| 負荷に関わる処理 | `measure-load.ps1 -Seconds 60` で、何もしていないときが 1 コアあたり 0.1% 前後のままか |
| 見た目 | `badge-preview.exe` の画像と、`check-app.ps1` の実画面 |
| 設定・メニュー | `settings.ini` を書き換えて起動し直し、`check-app.ps1` で表示が変わるか。確認後は ini を元に戻す |

## リリース手順

1. `CMakeLists.txt` の `project(VERSION)` を上げる(exe のバージョン情報と zip の名前はここから決まる)。
2. `CHANGELOG.md` の「未公開」を新しいバージョンの見出しに変える。
3. 上の確認をする。
4. zip を作る。表示された SHA256 を控える。
   ```bat
   powershell -ExecutionPolicy Bypass -File tools\package.ps1
   ```
5. GitHub の Releases に zip を載せ、本文に変更点と SHA256 を書く。

## 注意

- PowerShell 5.1 は BOM なしの UTF-8 を正しく読めない。日本語を含む `.ps1` は BOM 付き UTF-8 で保存する。
- ソースのコメントと文書は日本語で書く。
- 公開するファイルに、ユーザー名を含むパス、個人が使っているアプリの名前、日付を書かない([README.md](README.md) の「書き方の決まり」)。
