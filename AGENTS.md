# AI エージェント向けの指示

このリポジトリは、テキストキャレットの上に IME の状態(「あ」/「a」)を表示する Windows 11 用の常駐アプリ(C++ / Win32)です。

## 最初に読むもの

1. [docs/README.md](docs/README.md) — 文書の一覧と書き方の決まり
2. [docs/design.md](docs/design.md) — いまの設計。コードを変える前に必ず読む
3. [docs/decisions.md](docs/decisions.md) — 採用しなかった案。ここにある案を、理由なく試し直さない
4. [docs/development.md](docs/development.md) — ビルド、ツール、確認手順、リリース手順

## よく使うコマンド

```powershell
# リポジトリのルートで実行する。
# 起動中のインジケーターを終了してからビルドする(起動中だと LNK1104 で失敗する)。
# `cmd /c build.cmd` は Set-Location の後だとフォルダーを見失うので、.\build.cmd を直接呼ぶ
Get-Process KanaCaret -ErrorAction SilentlyContinue | Stop-Process
.\build.cmd

# 表示の確認(実際にアプリを開いて入力する。スクリーンショットは local\checks\ に保存)
Start-Process build\KanaCaret.exe
powershell -ExecutionPolicy Bypass -File tools\check-app.ps1 -Target notepad -Ime

# 負荷の計測(local\logs\stats-*.log に内訳)
powershell -ExecutionPolicy Bypass -File tools\measure-load.ps1 -Seconds 60

# 配布用 zip(dist\)
powershell -ExecutionPolicy Bypass -File tools\package.ps1
```

## 作業の決まり

- **変更したら確かめる。** [docs/development.md](docs/development.md) の「変更したときの確認」に従い、ビルドが警告なしで通ること、`check-app.ps1` の画像で位置が正しいことを確かめてから完了とする。確かめられなかったことは、そうと報告する。
- **文書を一緒に更新する。** 設計や動きを変えたら `docs/design.md` を、判断や不採用の案が出たら `docs/decisions.md` を、アプリの対応状況が分かったら `docs/app-compatibility.md` を、利用者から見える変更は `CHANGELOG.md` の「未公開」を更新する。
- **バージョン番号は `CMakeLists.txt` の `project(VERSION)` だけで管理する。** 上げるのはリリースのときだけ。
- **出力は `local/` に置く。** ログ・スクリーンショット・一時ファイルをリポジトリの他の場所に置かない。`local/`、`build/`、`dist/` はコミットしない。
- **個人情報を公開するファイルに書かない。** ユーザー名を含むパス(`C:\Users\...` など)、利用者が使っているアプリの名前、画面の内容、日付を、ソース・文書・コミットメッセージに入れない。計測結果を文書に書くときは、アプリ名を Windows 標準か広く使われているものに限り、それ以外は「Electron アプリ」などに言い換える。
- **利用者のデスクトップを操作するツールに注意する。** `check-app.ps1` は実際にウィンドウを開いてキー入力する。自分で開いたウィンドウだけを閉じ、利用者のウィンドウやプロセスを終了しない。
- **設定やレジストリを書き換えたら元に戻す。** 確認のために `%APPDATA%\KanaCaret\settings.ini` や自動起動の Run キーを変えたら、終わったら元に戻す。
- **キー入力を受け取る仕組み(キーボードフック、Raw Input)は入れない**([docs/decisions.md](docs/decisions.md))。
- ソースのコメントと文書は日本語で書く。周りのコードの書き方(名前の付け方、コメントの量)に合わせる。
- 日本語を含む `.ps1` は BOM 付き UTF-8 で保存する(PowerShell 5.1 が BOM なしの UTF-8 を読めないため)。
