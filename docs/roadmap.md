# 制限事項と今後の候補

## 制限事項(仕様として受け入れているもの)

| 制限 | 理由 | 解決するには |
|---|---|---|
| スタートメニューの検索欄では、インジケーターが隠れる | スタートメニューは通常の最前面ウィンドウより上の層に描かれる | `uiAccess="true"`(署名と Program Files への配置が必要。[decisions.md](decisions.md)) |
| 管理者として実行しているアプリでは表示しない | UIPI により、通常の権限からは IME の状態を問い合わせられない | 同上 |
| 入力位置を公開していないアプリでは、IME 切り替えの直後だけマウス位置に出す | 独自描画のエディターやゲームは、Win32/MSAA/UIA のどれでも位置を返さない | アプリごとの対応は難しい |
| 初回起動時に SmartScreen の警告が出る | コード署名をしていない | Microsoft Store か SignPath Foundation(下記) |

## 未確認のこと

[app-compatibility.md](app-compatibility.md) の「まだ確かめていないアプリ・条件」を参照。確かめたら、そちらの表に移す。

## 今後の候補

優先度の高い順。

1. **無料のコード署名**
   - Microsoft Store: 個人の開発者登録は無料。MSIX にパッケージし、自動起動を Run キーから MSIX のスタートアップタスクに変える必要がある。ストアでの `uiAccess` の可否は未調査。
   - SignPath Foundation: オープンソースなら無料。OSI 承認ライセンスでの公開、GitHub Actions でのビルド、2 要素認証が条件。発行元は SignPath Foundation になる。
2. **未確認のアプリ・条件の確認**(Office、Firefox、Google 日本語入力、DPI の違うマルチモニターなど)
3. **変換中の表示**: 変換候補ウィンドウとインジケーターが重なることがある。変換中は隠すか半透明にする(`EVENT_OBJECT_IME_SHOW` などで検知できる見込み)。
4. **入力モードの細分化**: カタカナ(`IME_CMODE_KATAKANA`)を「カ」、全角英数を「Ａ」で表す。
5. **UIA のイベント**: 定期的な問い合わせの代わりに、`TextSelectionChanged` などの UIA イベントで追従する。UIA を使うアプリでの負荷と遅れが減る見込み。
6. **アクリル背景**: DWM のアクリル(`DWMSBT_TRANSIENTWINDOW`)を使えるか。クリック透過との両立が課題。
7. **Windows 10 での確認**
