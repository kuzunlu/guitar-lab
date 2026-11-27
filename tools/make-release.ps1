# Makes the files of a GitHub release on this computer:
#   build\release\Guitar-Lab-vX.Y.Z.zip        (VST3 folder + standalone app + manual)
#   build\release\Guitar-Lab-Setup-X.Y.Z.exe   (the installer, Inno Setup 6)
#   build\release\notes.md                     (text for the release page)
# Run it on a tagged commit with no uncommitted changes:
#   powershell -ExecutionPolicy Bypass -File tools\make-release.ps1
# Then on GitHub: Releases > Draft a new release > choose the tag > paste notes.md > add the zip and the exe > Publish.

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

# A release is always an exact tag, built from committed code.
$tag = git describe --tags --match "v[0-9]*"   # v0.1.0 on the tag itself, v0.1.0-2-gabc1234 after it
if ($LASTEXITCODE -ne 0 -or -not $tag -or $tag.Contains('-')) { throw "This commit has no version tag. Tag it first: git tag vX.Y.Z" }
if (git status --porcelain --untracked-files=no) { throw "There are uncommitted changes. Commit them first." }
$number = $tag.Substring(1)   # v0.1.0 -> 0.1.0

$iscc = @("$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
          "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
          "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 was not found." }

# build.bat builds the VST3, the standalone app and the tests, then runs the tests.
cmd /c build.bat
if ($LASTEXITCODE -ne 0) { throw "build.bat failed." }

& $iscc /Q "/DAppVersion=$number" installer\GuitarLab.iss
if ($LASTEXITCODE -ne 0) { throw "The installer build failed." }

$out = "build\release"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force "$out\files" | Out-Null
Copy-Item -Recurse "build\GuitarLab_artefacts\Release\VST3\Guitar Lab.vst3" "$out\files\"
Copy-Item "build\GuitarLab_artefacts\Release\Standalone\Guitar Lab.exe" "$out\files\"
Copy-Item -Recurse "docs\manual" "$out\files\Manual"
# Windows' own tar writes a standard zip (Compress-Archive in PowerShell 5.1 writes "\" in the paths).
& "$env:SystemRoot\System32\tar.exe" -a -c -f "$out\Guitar-Lab-$tag.zip" -C "$out\files" "Guitar Lab.vst3" "Guitar Lab.exe" "Manual"
if ($LASTEXITCODE -ne 0) { throw "Making the zip failed." }
Remove-Item -Recurse -Force "$out\files"
$installer = "Guitar-Lab-Setup-$number.exe"
Copy-Item "build\installer\$installer" "$out\"

$notes = @'
**New here? Read the manual:** every button and setting explained, with pictures and step-by-step recipes. After installing: Start menu > **Guitar Lab Manual**, or the **Manual** button in the plugin. In the zip: `Manual\index.html`.

**Easiest:** run `INSTALLER`, then rescan plugins in your DAW.
It installs the VST3 into `C:\Program Files\Common Files\VST3\` and the standalone app into `C:\Program Files\Guitar Lab\` (Start menu: Guitar Lab).

**Or by hand:** unzip `Guitar-Lab-TAG.zip`, copy the `Guitar Lab.vst3` folder to `C:\Program Files\Common Files\VST3\`, then rescan plugins. `Guitar Lab.exe` is the standalone app and runs from anywhere.

Guitar Lab is an **audio effect**: put it on the audio track your guitar is plugged into. To play another instrument, make a MIDI track and set its MIDI From to that audio track, source **Guitar Lab**. The MIDI uses one channel per string (MPE), so bends stay on their own string.
For guitar-to-MIDI tracking, run Calibrate on the Guitar -> MIDI tab.
Needs 64-bit Windows. The installer is not signed, so Windows SmartScreen may warn: choose More info > Run anyway.
'@
$notes.Replace('INSTALLER', $installer).Replace('TAG', $tag) | Out-File -Encoding utf8 "$out\notes.md"

Write-Host ""
Write-Host "Release files for $tag are in $((Resolve-Path $out).Path):"
Get-ChildItem $out | ForEach-Object { Write-Host "  $($_.Name)" }
