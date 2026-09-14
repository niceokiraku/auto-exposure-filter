# Auto Exposure (Adaptive Brightness) for OBS Studio

An OBS Studio video filter that automatically brightens dark scenes and
slightly darkens overly bright ones in real time — useful for games with
unstable in-game exposure that make streams hard to watch.

日本語の説明は [下記](#日本語) にあります。

## Features

- Measures the average brightness of the filtered source every frame (via a
  tiny 32×32 downscale render, so the cost is negligible)
- Smoothly adjusts a correction gain over a configurable response time to
  avoid flicker/pumping on quick flashes
- Soft-knee highlight protection so lifting shadows doesn't blow out
  already-bright areas
- Fully adjustable: target brightness, correction strength, response time,
  and min/max gain clamps

## Requirements

- OBS Studio 28.0 or later, 64-bit Windows (tested on 32.2.2)
- Windows 10/11 64-bit

## Installation

We distribute this plugin as a plain ZIP rather than a compiled installer.
This is a deliberate transparency choice: you can see and verify every file
you're placing on your system, rather than trusting an opaque `.exe` wrapper
— and it also means no unsigned-executable SmartScreen warning.

1. Download `auto-exposure-filter-<version>-windows-x64.zip` from
   [Releases](../../releases).
2. Extract it so you end up with:
   ```
   C:\ProgramData\obs-studio\plugins\auto-exposure-filter\bin\64bit\auto-exposure-filter.dll
   C:\ProgramData\obs-studio\plugins\auto-exposure-filter\data\...
   ```
3. Restart OBS Studio.

## Usage

1. Right-click the source you want to correct (game capture, window capture,
   etc.) → **Filters**.
2. Under **Effect Filters**, click **+** → **Auto Exposure (Adaptive
   Brightness)**.
3. Adjust the settings:

| Setting | Meaning | Effect of increasing | Effect of decreasing |
|---|---|---|---|
| Target Brightness | Average brightness the filter aims for (0=black, 1=white) | Overall brighter target | Overall darker target |
| Correction Strength | How much of the computed correction is actually applied (0=off, 1=full) | Stronger, more noticeable correction | More subtle, closer to the original image |
| Response Time (seconds) | Time constant for smoothing brightness changes | Slower, smoother reaction (less flicker, more lag) | Faster reaction (more responsive, more prone to flicker on quick flashes) |
| Minimum Gain | Lower clamp on the correction multiplier | Less darkening of bright scenes | Stronger darkening of bright scenes (risk of flat/washed-out look) |
| Maximum Gain | Upper clamp on the correction multiplier | Stronger brightening of dark scenes (risk of visible noise) | Less brightening of dark scenes |

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

### インストール

本プラグインはコンパイル済みインストーラーではなく、**素のZIP形式**で配布しています。
これは意図的な透明性の確保です — 中身の見えない`.exe`を信用して実行するのではなく、
PCに配置されるファイルを利用者自身の目で確認・検証できるようにするためです
(副次的な効果として、未署名実行ファイルに対するSmartScreenの警告も発生しません)。

[Releases](../../releases) から `auto-exposure-filter-<バージョン>-windows-x64.zip`
をダウンロードして展開し、`C:\ProgramData\obs-studio\plugins\auto-exposure-filter\`
以下に `bin\64bit\auto-exposure-filter.dll` と `data\` フォルダを配置してください。

インストール後はOBS Studioを再起動してください。

### 使い方

フィルタを付けたいソースを右クリック →「フィルタ」→ 映像フィルタの「+」→
「**自動露出補正 (明るさ自動調整)**」を追加します。各設定項目の意味は上の英語版の表を参照してください(日本語UIにも同じ順で表示されます)。

### ライセンス

GPL-2.0-or-later です。OBS本体(libobs)がGPLで提供されているため、これにリンクする
本プラグインも同じライセンスで配布しています。ソースコードは常にこのリポジトリで
公開されます。
