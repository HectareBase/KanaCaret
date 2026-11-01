# 設計

テキストキャレット(文字入力位置)の少し上に、IME が日本語入力なら「あ」、直接入力なら「a」を表示する常駐アプリ。

## 目標

| 項目 | 目標 | 現状(16 論理コア、120 DPI で計測) |
|---|---|---|
| 何もしていないときの CPU | 1 コアあたり 0.1% 前後 | 0.10% |
| 入力中の CPU | 小さく抑える | メモ帳で毎秒 8 文字入力して 0.6〜1.1% |
| メモリ | 小さく抑える | プライベート 5.7〜6.5MB(Direct2D/DirectWrite で約 3〜4MB) |
| 実行ファイル | 単一 exe、ランタイム不要 | 約 225KB(アイコン込み、CRT 静的リンク) |
| IME 切り替えの反映 | 150ms 以内 | 入力直後は 100ms ごとに確認 |
| 対応 OS | Windows 11 | Windows 10 は未確認 |

計測は `tools\measure-load.ps1` で行う([development.md](development.md))。

## 構成

```
src/
  main.cpp            制御の中心。Update()(状態の確認と表示)、WinEvent フック、タイマー、
                      通知領域のメニュー、設定の反映、--stats の記録
  ImeMonitor.*        IME 状態の問い合わせ(WM_IME_CONTROL)
  CaretLocator.*      フォーカスのあるウィンドウの特定と、Win32/MSAA での入力位置の取得、矩形の妥当性チェック
  UiaWorker.*         UI Automation で入力位置を問い合わせる専用スレッド
  IndicatorWindow.*   表示用の層状ウィンドウ(配置、フェード)
  Renderer.*          Direct2D/DirectWrite でバッジを描く(インジケーター、通知領域のアイコン、exe のアイコン)
  Theme.*             Windows の配色設定の読み取りと、バッジの配色
  Settings.*          settings.ini と自動起動(Run キー)
  TrayIcon.*          通知領域のアイコンとメニューの表示
resources/
  app.manifest        PerMonitorV2 DPI 対応、Common Controls v6
  app.rc, app.ico     アイコンとバージョン情報(バージョン番号は version.h.in 経由で CMakeLists.txt から入る)
probe/                検証ツール ime-probe(アプリごとに取得方法を調べる)
tools/                確認・計測・配布のツール(development.md)
```

スレッドは UI スレッドと UIA 用ワーカースレッドの 2 本。UIA の呼び出しは別プロセスとの通信で最大 1 秒止まることがあるため、ワーカーで行い、結果を `PostMessage` で UI スレッドに返す。

## 処理の流れ

すべての処理は `main.cpp` の `Update(reason)` に集まる。呼ばれるきっかけは次の 4 つ。

| きっかけ | reason | 内容 |
|---|---|---|
| `EVENT_SYSTEM_FOREGROUND` / `EVENT_OBJECT_FOCUS`(システム全体) | Focus | 手前のアプリやフォーカスが変わった |
| `EVENT_OBJECT_SHOW/HIDE/LOCATIONCHANGE`(`OBJID_CARET` のみ、フォーカス中のアプリだけ) | Caret | キャレットが出た・消えた・動いた。33ms 以内に続いたものは 1 回にまとめる |
| 100ms のタイマー | Tick | IME の切り替えの確認と、通知の来ないアプリの追従 |
| UIA ワーカーの結果 | UiaResult | 結果を反映して表示だけ更新する |

`Update()` の手順:

1. フォーカスのあるウィンドウを調べる(`GetGUIThreadInfo`。UWP は子の `CoreWindow` を見る)。自分自身なら隠す。
2. フォーカスが変わったら、取得方法の記録をリセットし、キャレット用のフックをそのアプリに付け替える(付け替えはメッセージループに戻ってから)。
3. 除外アプリ、またはフルスクリーン中(1 秒に 1 回 `SHQueryUserNotificationState`)なら隠す。
4. 必要なときだけ IME の状態を問い合わせる(下の「負荷を抑える仕組み」)。取得できなければ隠す。
5. 表示モードで隠す状態なら、入力位置は問い合わせずに隠す。
6. 入力位置を取得する(下の「入力位置の取得」)。
7. 取れたらキャレットの位置に、取れなければ(IME 切り替えの直後 1.5 秒だけ)マウスの位置に表示する。

## IME 状態の判定

1. `ImmGetDefaultIMEWnd(フォーカスのあるウィンドウ)` で IME ウィンドウを得る。
2. `SendMessageTimeout(WM_IME_CONTROL, IMC_GETOPENSTATUS)` で ON/OFF を得る。OFF なら「a」。
3. ON なら `IMC_GETCONVERSIONMODE` を得て、`IME_CMODE_NATIVE` が立っていれば「あ」、なければ「a」(IME オンの半角英数モード)。
4. 応答が無い・権限が足りない(管理者権限のアプリ)ときは「不明」として隠す。

IME をオフにしても変換モードの NATIVE ビットは立ったままなので、ON/OFF を先に見るのが必須。プロセス外から読めるのはこの方法だけ([decisions.md](decisions.md))。

## 入力位置の取得

| 順 | 方法 | 主な対象 | 実装 |
|---|---|---|---|
| 1 | Win32 のシステムキャレット(`GetGUIThreadInfo` の `rcCaret`) | メモ帳など Win32 アプリ | `CaretLocator.cpp` |
| 2 | MSAA(`AccessibleObjectFromWindow(OBJID_CARET)`) | Chromium / Electron | `CaretLocator.cpp` |
| 3 | UI Automation(`TextPattern2::GetCaretRange` → 範囲の矩形 → 直後の 1 文字の左端 → 直前の 1 文字の右端) | WinUI/UWP(エクスプローラーの検索欄、設定アプリ、スタートメニュー)、Windows Terminal | `UiaWorker.cpp`(非同期) |
| 4 | マウスカーソルの位置(IME 切り替えの直後だけ。設定でオフにできる) | 上で取れないアプリ | `main.cpp` |

取得した矩形は、次の条件をすべて満たすときだけ使う(`PlausibleCaret`)。

- 高さが 6〜120 DIP、幅が高さ以下(文書全体や選択範囲、高さ 1px の矩形をはじく)
- 画面内にある(アニメーション中の画面外の座標をはじく)
- MSAA で幅 0 でない(Chromium が前の入力欄の古い位置を幅 0 で返す)

取得方法の使い分け:

- **Chromium/Electron(クラス名が `Chrome_` で始まる)では UIA を使わない。** UIA は古い位置のまま更新されないことがある。
- **Win32 で取れたウィンドウでは、以後 Win32 だけを試す**(MSAA のプロセス間通信を減らす)。メモ帳はしばらく操作しないとキャレットを破棄するので、フォーカスが移るまで最後の位置に表示し続ける。
- **UIA でしか取れなかった入力欄は「exe 名|ウィンドウクラス」で覚え**、次からは Win32/MSAA を飛ばして UIA へ進む。ただし UIA で問い合わせる前に毎回 Win32 を確かめ、取れたら Win32 に戻す(開いた直後のメモ帳はキャレットが無く、UIA で取れてしまうため)。
- UIA の問い合わせは 1 件ずつ。前の問い合わせが終わっていなければ、その回は飛ばす。タイムアウトは 1 秒。
- キャレットが一瞬消えても(再描画のたびに `HideCaret` するアプリ)、300ms は最後の位置に表示し続ける。

アプリ別の結果と癖は [app-compatibility.md](app-compatibility.md)。

## 負荷を抑える仕組み

`main.cpp` の定数で調整する。`--stats` の記録で効果を確かめられる。

| 仕組み | 定数 | 理由 |
|---|---|---|
| キャレット用のフックはフォーカス中のアプリだけに付ける | — | システム全体だと、無関係なウィンドウの移動・表示の通知が毎秒 100 件以上届く |
| キャレットの通知を 33ms ごとにまとめる | `kCaretCoalesceMs` | Chromium は 1 回のキー入力で何度も通知する |
| キャレットの通知では IME を問い合わせない | — | IME の切り替えは 100ms ごとの確認で拾える |
| 入力から 0.5 秒以内だけ 100ms ごとに IME を確認し、それ以外は 1 秒ごと | `kImeActiveMs`, `kImeIdlePollMs` | IME の切り替えはほぼキー操作で起きる(`GetLastInputInfo` で判定) |
| UIA は入力から 2 秒以内だけ 250ms ごと、それ以外は 2 秒ごと | `kUiaActiveMs`, `kUiaPollMs`, `kUiaIdlePollMs` | UIA は 1 回で約 3ms の CPU を使う |
| 通知が来るアプリでも取りこぼし対策で問い合わせ直す: 入力中 1 秒、それ以外 5 秒 | `kRelocateMs`, `kIdleRelocateMs` | |
| 表示モードで隠している間は入力位置を問い合わせない | — | 「切り替えたときだけ表示」が最も軽い |
| 画像は状態・テーマ・DPI・大きさが変わったときだけ描き直す。移動は位置だけ変える | — | |

タイマー自体は常に 100ms で動かす。入力が無い間は何も問い合わせないので、起きても負荷はほとんど無い。

## 表示

- ウィンドウ: `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`。クリックを透過し、フォーカスを奪わず、タスクバーや Alt+Tab に出ない。フォーカスが変わるたびに最前面を付け直す。
- 描画: `UpdateLayeredWindow` に 32bpp のプリマルチプライド DIB を渡す。フェードは全体の不透明度だけを変える(フェードイン 83ms、フェードアウト 167ms、ease-out。「アニメーション効果」がオフなら即時)。
- 大きさ: 小 18 / 中 22 / 大 28 DIP の正方形。影のために周囲に 6 DIP の余白を持つ。
- 配置(キャレットとの間は 4 DIP):
  - 上: 左端をキャレットにそろえ、キャレットの上
  - 右上: キャレットの右で、下端をキャレットの上端にそろえる
  - 下: キャレットの下
  - 作業領域からはみ出すときは上下を入れ替える。マウス位置に出すときはカーソルの 8 DIP 上。
- DPI: すべて物理ピクセルで扱い(PerMonitorV2)、キャレットのあるモニターの DPI で大きさを決める。

## 見た目

| 状態 | 背景 | 枠 | 文字 |
|---|---|---|---|
| 「a」ライト | `#F9F9F9` | `#8A8A8A` | `#1B1B1B` |
| 「a」ダーク | `#3A3A3A` | `#9A9A9A` | `#FFFFFF` |
| 「あ」ライト | アクセント Dark1 | 同左 | 白 |
| 「あ」ダーク | アクセント Light2 | 同左 | 黒 |
| ハイコントラスト | `COLOR_WINDOW` / 「あ」は `COLOR_HIGHLIGHT` | `COLOR_WINDOWTEXT` | `COLOR_WINDOWTEXT` / `COLOR_HIGHLIGHTTEXT` |

- 「a」の濃い枠は、暗いアプリの上でも輪郭が見えるようにするため(Fluent の ControlStrongStroke 相当)。
- 「あ」の塗りは Fluent の AccentFillColorDefault に合わせる。アクセントは `HKCU\...\Explorer\Accent\AccentPalette` から読む。「アクセントカラーにする」をオフにすると「a」と同じ配色になる。
- 角の丸みは箱の 5/22、文字は箱の 13/22。「あ」は Yu Gothic UI、「a」は Segoe UI Variable Text(無ければ Segoe UI)、どちらも Semibold。背景が透明なので文字はグレースケールのアンチエイリアス。
- 影: 角丸を少しずつ大きくして薄く重ねる(6 層、広がり 4 DIP、下へ 1 DIP。不透明度はライト 16%、ダーク 30%)。
- テーマの変更は `WM_SETTINGCHANGE` / `WM_SYSCOLORCHANGE` / `WM_DWMCOLORIZATIONCOLORCHANGED` で読み直す。インジケーターはアプリのテーマ(`AppsUseLightTheme`)、通知領域のアイコンは Windows のテーマ(`SystemUsesLightTheme`)に合わせる。
- 通知領域のアイコンは、いまの IME 状態のバッジを描いて作る。exe のアイコンは `badge-preview.exe --ico` で作る(既定のアクセント `#005FB8` の「あ」)。

## 設定と常駐

設定は `%APPDATA%\ImeIndicator\settings.ini`(UTF-16、セクション `[Indicator]`)。通知領域のメニューで変えるとすぐ保存する。

| キー | 値 | 既定 |
|---|---|---|
| `Mode` | 0 = 入力欄にいる間は常に表示、1 = 切り替えたときだけ(1.5 秒)、2 = 日本語入力のときだけ | 0 |
| `Placement` | 0 = 上、1 = 右上、2 = 下 | 0 |
| `Size` | 0 = 小、1 = 中、2 = 大 | 1 |
| `AccentKana` | 1 = 「あ」をアクセントカラーにする | 1 |
| `MouseFallback` | 1 = 入力位置が分からないときはマウスの位置に表示 | 1 |
| `Exclude` | 表示しない exe 名(小文字、カンマ区切り) | 空 |

- 自動起動は `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` の値 `ImeIndicator`(exe のパス)。ini には持たない。
- 二重起動は名前付き Mutex `Local\ImeIndicator.SingleInstance` で防ぐ。
- Explorer が再起動したら(`TaskbarCreated`)通知領域のアイコンを登録し直す。
- メニューの「○○.exe では表示しない」には、最後にフォーカスがあった他アプリの名前が出る。

## 表示しない条件

- テキスト入力欄にフォーカスが無い(入力位置が取れず、IME 切り替えの直後でもない)
- IME の状態が取れない(管理者権限のアプリなど)
- 除外したアプリ、フルスクリーンのアプリ・ゲーム・プレゼンテーション中、一時停止中
- 表示モードの条件に合わない
