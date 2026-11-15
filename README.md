# Auto Exposure (Adaptive Brightness) for OBS Studio

An OBS Studio video filter that automatically brightens dark scenes and
slightly darkens overly bright ones in real time — useful for games with
unstable in-game exposure that make streams hard to watch.

日本語の説明は [下記](#日本語) にあります。

## Features

- Meters scene brightness every frame using a percentile of a tiny 32×32
  downscale render, so the cost is negligible and small bright lights
  don't dominate the reading
- Smooth, configurable-speed correction that avoids flicker/pumping from
  quick flashes
- Lifts shadows while leaving highlights nearly untouched: the brightening
  gain tapers off for already-bright pixels, so dark scenes don't blow out
- Built-in before/after comparison mode (split screen)
- Fully adjustable: target brightness, correction strength, response time,
  and min/max gain clamps

## Requirements

- OBS Studio 28.0 or later, 64-bit Windows (tested on 32.2.2)
- Windows 10/11 64-bit

## Installation

We distribute this plugin as a plain ZIP rather than a compiled installer,
so you can see and verify every file being placed on your system.

1. Download `auto-exposure-filter-<version>-windows-x64.zip` from
   [Releases](../../releases) and extract it.
2. Copy the `auto-exposure-filter` folder inside it directly into:
   ```
   C:\ProgramData\obs-studio\plugins\
   ```
   You should end up with:
   ```
   C:\ProgramData\obs-studio\plugins\auto-exposure-filter\bin\64bit\auto-exposure-filter.dll
   C:\ProgramData\obs-studio\plugins\auto-exposure-filter\data\...
   ```
3. Start (or restart) OBS Studio.

## Usage

1. Right-click the source you want to correct (game capture, window capture,
   etc.) → **Filters**.
2. Under **Effect Filters**, click **+** → **Auto Exposure (Adaptive
   Brightness)**.
3. Adjust the settings:

**Metering Position** — which brightness level in the scene is used as the
reading (50 = median)
- Decrease: meters on the darker parts of the scene, so the correction lifts
  more strongly
- Increase: meters on the brighter parts, so the correction is gentler

**Metering Width** — averages a band of brightness levels around the
Metering Position instead of reading a single point (0 = single point, the
default)
- Increase: in scenes that are half dark and half bright, the correction
  changes gradually instead of jumping when the balance shifts slightly
- 0: same metering as earlier versions

**Target Brightness** — the brightness the filter aims for
(0 = black, 1 = white)
- Increase: overall brighter image
- Decrease: overall darker, more subdued image

**Correction Strength** — how much of the computed correction is actually
applied (0 = off, 1 = full)
- Increase: stronger, more noticeable correction
- Decrease: more subtle, closer to the original image

**Response Time (seconds)** — how quickly the filter reacts to brightness
changes
- Increase: slower, smoother reaction (less flicker, more lag)
- Decrease: faster reaction (more responsive, but more prone to flicker on
  quick flashes)

**Minimum Gain** — lower clamp on the correction multiplier
- Increase: bright scenes are less likely to be darkened
- Decrease: bright scenes get darkened more aggressively (watch for a flat,
  washed-out look)

**Maximum Gain** — upper clamp on the correction multiplier
- Increase: dark scenes get brightened more aggressively (more visible
  noise)
- Decrease: less brightening of dark scenes, but image quality stays
  cleaner

**Auto-Bypass** — fades the correction out automatically while the scene
doesn't need it, so the filter can stay enabled all the time (off by default)
- Shadow Lift (Full Strength Below / Zero Above): shadow lift only happens
  while the scene is darker than Target Brightness, so these two only have
  an effect when set lower than it. Lower them to leave slightly dark scenes
  alone and lift only clearly dark ones
- Highlight Suppression (Zero Below / Full Strength Above): set both higher
  than Target Brightness. Scenes between the target and "Zero Below" are
  left untouched

**Show Adjustment Indicator** — draws a small gauge in the bottom-left corner
showing how much correction is being applied right now (0–100%). Meant for
tuning; turn it off when you are done.

**Comparison Mode / Split Position** — shows the original image on the left
and the corrected image on the right, divided by a line you can move with
Split Position. Useful for judging the effect while tuning.

**Rule of thumb**: start by setting Response Time to around 1.5–3 seconds
and check for flicker, then tune Correction Strength between 0.5 and 0.8
until it looks natural.

## Building from source

```powershell
git clone --recursive https://github.com/niceokiraku/auto-exposure-filter.git
cd auto-exposure-filter
cmake --preset windows-x64
cmake --build --preset windows-x64
```

The plugin (`build_x64\rundir\RelWithDebInfo\auto-exposure-filter.dll` plus
its `data` folder) can be copied directly into
`%ProgramData%\obs-studio\plugins\auto-exposure-filter\` for testing.

### Optional: building a Windows installer

An Inno Setup script is included at `installer\setup.iss` for anyone who
wants a wizard-style `.exe` installer instead (for example, after obtaining a
code-signing certificate). It is not part of the official release artifacts;
[requires Inno Setup](https://jrsoftware.org/isinfo.php) to build:

```powershell
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" installer\setup.iss
```

The output `.exe` is written to `release\`.

## License

GPL-2.0-or-later — see [LICENSE](LICENSE). This plugin links against
`libobs`, which is licensed under the GPL, so this project is distributed
under the same license. You are free to use, study, modify, and redistribute
it; the source code for this exact release is always available in this
repository.

---

## 日本語

OBS Studio用の映像フィルタです。ゲームによって不安定になりがちな画面の明るさを
リアルタイムで自動補正し、暗いシーンは明るく、明るすぎるシーンは少し抑えます。

### 機能

- 毎フレーム、映像の明るさをパーセンタイルで測定(32×32ピクセルの縮小描画で行うため負荷はごくわずか。小さな光源に測定値が引っ張られにくい)
- 反応速度を設定できる滑らかな補正で、瞬間的なフラッシュ等によるチラつきを抑制
- 暗部だけを持ち上げ、明るい部分はほぼそのまま。暗いシーンを明るくしても白飛びしにくい
- 補正前後を左右に並べて見比べられる比較モードを内蔵
- 目標の明るさ・補正の強さ・反応速度・補正の上限下限をすべて調整可能

### 必要環境

- OBS Studio 28.0以降、64bit版Windows(32.2.2で動作確認済み)
- Windows 10/11 64bit

### インストール

本プラグインはコンパイル済みインストーラーではなく、ZIP形式で配布しています。
これはPCに配置されるファイルを利用者自身の目で確認・検証できるようにするためです。

1. [Releases](../../releases) から `auto-exposure-filter-<バージョン>-windows-x64.zip`
   をダウンロードし、展開してください
2. 中の `auto-exposure-filter` フォルダを、以下の場所にそのままコピーしてください:
   ```
   C:\ProgramData\obs-studio\plugins\
   ```
   結果として次のようなパスになっていればOKです:
   ```
   C:\ProgramData\obs-studio\plugins\auto-exposure-filter\bin\64bit\auto-exposure-filter.dll
   C:\ProgramData\obs-studio\plugins\auto-exposure-filter\data\...
   ```
3. OBS Studioを起動(起動中なら再起動)してください

### 使い方

1. 補正したいソース(ゲームキャプチャ等)を右クリック →「フィルタ」
2. 映像フィルタの「+」→「**自動露出補正 (明るさ自動調整)**」を追加
3. 設定パネルで以下を調整できます:

**測光位置**: 映像のどの明るさを基準に測るか(50=中央値)
- 下げると: 暗い部分を基準に測るので、補正が強く効く
- 上げると: 明るい部分を基準に測るので、補正が穏やかになる

**測光の幅**: 測光位置の1点だけでなく、その前後の範囲を平均して測る(0=1点のみ、既定)
- 上げると: 明暗が半々の場面で、バランスが少し変わっただけで補正が急変するのを抑え、なだらかに変化する
- 0のとき: これまでのバージョンと同じ測り方

**目標の明るさ**: 映像の明るさをどこに合わせたいかの基準値(0=真っ黒、1=真っ白)
- 上げると: 全体的に明るめの映像になる
- 下げると: 全体的に暗め・落ち着いた映像になる

**補正の強さ**: 補正を実際に反映する割合(0=補正なし、1=フル補正)
- 上げると: 補正がより強く・はっきり効く
- 下げると: 元の映像に近い、控えめな効き方になる

**反応速度(秒)**: 明るさの変化に追従するまでの反応速度
- 上げると: ゆっくり滑らかに追従(チラつきにくいが反応も遅れる)
- 下げると: すぐに反応する(チラつき・明滅が起きやすくなる)

**補正の下限**: 補正倍率の下限を設定
- 上げると: 明るいシーンが暗くなりにくくなる
- 下げると: 明るいシーンもより強く暗く調整する(コントラスト低下に注意)

**補正の上限**: 補正倍率の上限を設定
- 上げると: 暗いシーンをより強く明るくする(ノイズが目立ちやすくなる)
- 下げると: あまり明るくならないが、画質は保たれる

**自動バイパス**: 補正が要らない明るさの場面では、補正を自動的に弱めて止めます。
フィルタを常にオンにしたまま使いたい場合に便利です(既定はオフ)。
- 暗部補正(フル稼働する明るさ / 停止する明るさ): 暗部補正は画面が「目標の明るさ」より
  暗いときだけ働くため、この2項目は目標の明るさより低い値にしたときだけ効果があります。
  値を下げると、少し暗い程度の場面はそのままにして、はっきり暗い場面だけ明るくできます
- ハイライト抑制(停止する明るさ / フル稼働する明るさ): 2項目とも目標の明るさより高い値に
  します。目標の明るさから「停止する明るさ」までの場面には補正がかかりません

**調整インジケーターを表示**: 画面左下の小さなバーで、今どれくらい補正が効いているか
(0〜100%)を表示します。調整用なので、確認が済んだらオフに戻してください。

**比較モード / 分割位置**: 左側に補正なしの元映像、右側に補正後の映像を並べて表示します。
境界線は「分割位置」で動かせます。調整中に効果を確認するのに便利です。

**調整の目安**: まず「反応速度」を1.5〜3秒程度にしてチラつきがないか確認し、次に
「補正の強さ」を0.5〜0.8の間で自然に見える強さに調整するのがおすすめです。

### ライセンス

GPL-2.0-or-later です。OBS本体(libobs)がGPLで提供されているため、これにリンクする
本プラグインも同じライセンスで配布しています。ソースコードは常にこのリポジトリで
公開されます。
