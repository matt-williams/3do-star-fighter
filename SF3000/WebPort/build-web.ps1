[CmdletBinding()]
param(
    [string]$EmsdkRoot = "$env:USERPROFILE\.copilot\tools\emsdk",
    [string]$AssetRoot,
    [switch]$DebugSymbols,
    [switch]$SkipAssets,
    [switch]$SkipMedia,
    [string]$Ffmpeg
)

$ErrorActionPreference = "Stop"
$webPortRoot = Split-Path -Parent $PSCommandPath
$repositoryRoot = Resolve-Path (Join-Path $webPortRoot "..\..")
$webRoot = Join-Path $webPortRoot "web"
$emcc = Join-Path $EmsdkRoot "upstream\emscripten\emcc.exe"

if (-not (Test-Path -LiteralPath $emcc -PathType Leaf)) {
    throw "Emscripten was not found at $emcc."
}

if ([string]::IsNullOrWhiteSpace($AssetRoot)) {
    $rawAssetRoot = Join-Path $repositoryRoot "web-assets\raw"
    $AssetRoot = Join-Path $repositoryRoot "web-assets\runtime"
    if (-not $SkipAssets) {
        & python (Join-Path $repositoryRoot "tools\convert_assets.py") `
            --build-runtime --raw-root $rawAssetRoot --runtime-output $AssetRoot
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to generate the endian-normalized browser asset mirror."
        }
    }
}
$AssetRoot = (Resolve-Path -LiteralPath $AssetRoot).Path
$resources = Join-Path $AssetRoot "SF_Resources"
if (-not (Test-Path -LiteralPath $resources -PathType Container)) {
    throw "AssetRoot must contain SF_Resources."
}
foreach ($directory in "Music", "Video", "Voices", "Samples") {
    if (Test-Path -LiteralPath (Join-Path $resources $directory)) {
        throw "AssetRoot must be media-free; found SF_Resources\$directory."
    }
}
$soundEffectsRoot = Join-Path $webRoot "audio"
& python (Join-Path $repositoryRoot "tools\convert_assets.py") `
    --sound-effects-only --clean --output $soundEffectsRoot
if ($LASTEXITCODE -ne 0) {
    throw "Failed to convert browser sound effects."
}
if (-not $SkipMedia) {
    $streamedMediaRoot = Join-Path $webRoot "media"
    $mediaArguments = @(
        (Join-Path $repositoryRoot "tools\convert_assets.py"),
        "--streamed-media-only",
        "--clean",
        "--output",
        $streamedMediaRoot
    )
    if (-not [string]::IsNullOrWhiteSpace($Ffmpeg)) {
        $mediaArguments += @("--ffmpeg", $Ffmpeg)
    }
    & python @mediaArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to convert streamed music and voice-over media."
    }
    $cinematicsRoot = Join-Path $webRoot "video"
    $cinematicArguments = @(
        (Join-Path $repositoryRoot "tools\convert_assets.py"),
        "--cinematics-only",
        "--clean",
        "--output",
        $cinematicsRoot
    )
    if (-not [string]::IsNullOrWhiteSpace($Ffmpeg)) {
        $cinematicArguments += @("--ffmpeg", $Ffmpeg)
    }
    & python @cinematicArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to convert cinematic media."
    }
}

$webSources = @(
    "Collision.c",
    "Maths_Stuff.c",
    "Plot_Land.c",
    "SF_ARMAnim.c",
    "SF_ARMBurn.c",
    "SF_ARMCell_portable.c",
    "SF_ARMLink.c",
    "SF_ARMUtils.c",
    "sf_3do_compat.c",
    "sf3000_webport_laser.c",
    "sf3000_webport_plot_graphic.c",
    "sf3000_webport_sky.c",
    "sf3000_webport_smoke.c",
    "sf_web_port_renderer.c",
    "sf_web_runtime.c"
)
$legacySources = @(
    "Star3000.c",
    "SFlib\Bit_Control.c",
    "SFlib\Bonus_Control.c",
    "SFlib\broker_shell.c",
    "SFlib\bs_joystick.c",
    "SFlib\Collision_Update.c",
    "SFlib\Draw_Frame.c",
    "SFlib\Draw_Land.c",
    "SFlib\Explosion.c",
    "SFlib\Graphics_Set.c",
    "SFlib\Ground_Control.c",
    "SFlib\Laser_Control.c",
    "SFlib\PlayCPakStream.c",
    "SFlib\PrepareStream.c",
    "SFlib\Rotate_Land.c",
    "SFlib\Setup_Tables.c",
    "SFlib\SF_Access.c",
    "SFlib\SF_Bonus.c",
    "SFlib\SF_Celutils.c",
    "SFlib\SF_Control.c",
    "SFlib\SF_Font.c",
    "SFlib\SF_Io.c",
    "SFlib\SF_Joystick.c",
    "SFlib\SF_Map.c",
    "SFlib\SF_Menu.c",
    "SFlib\SF_Message.c",
    "SFlib\SF_Music.c",
    "SFlib\SF_NVRam.c",
    "SFlib\SF_Palette.c",
    "SFlib\SF_Pyramid.c",
    "SFlib\SF_Screenutils.c",
    "SFlib\SF_Sound.c",
    "SFlib\SF_Status.c",
    "SFlib\SF_Utility.c",
    "SFlib\SF_Video.c",
    "SFlib\SF_War.c",
    "SFlib\Ship_Command.c",
    "SFlib\Ship_Control.c",
    "SFlib\Smoke_Control.c",
    "SFlib\Sound_Control.c",
    "SFlib\test_prog.c",
    "SFlib\Update_Frame.c",
    "SFlib\Weapons.c"
)

$arguments = @(
    "-std=c11",
    "-funsigned-char",
    "-DSF_WEB_PORT",
    "-I$webPortRoot\include",
    "-I$webPortRoot",
    "-I$repositoryRoot\SF3000",
    "-I$repositoryRoot\SF3000\SFlib",
    "-sASYNCIFY",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sNO_EXIT_RUNTIME=1",
    "-sEXPORTED_RUNTIME_METHODS=UTF8ToString,HEAPU8",
    "-o", (Join-Path $webRoot "starfighter.js")
)
$arguments += "--preload-file", "$resources@/"

if ($DebugSymbols) {
    $arguments += "-gsource-map"
}

$arguments += $webSources | ForEach-Object { Join-Path $webPortRoot $_ }
$arguments += $legacySources | ForEach-Object {
    Join-Path $repositoryRoot "SF3000\$_"
}

& $emcc @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
