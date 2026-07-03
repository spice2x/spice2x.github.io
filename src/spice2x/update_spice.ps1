$ErrorActionPreference = 'Stop'

# --- configuration ----------------------------------------------------------
$scriptDir = if ($env:SPICE_DIR) { $env:SPICE_DIR } elseif ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
$repo = 'spice2x/spice2x.github.io'
$targets = @('spice.exe', 'spice64.exe', 'spicecfg.exe')
$beta = ($env:SPICE_CHANNEL -match 'beta')
$rc = 0

$title = if ($beta) { '=== spice2x updater (beta channel) ===' } else { '=== spice2x updater ===' }
Write-Host "`n$title"
Write-Host "Target folder: $scriptDir`n"

try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $headers = @{ 'User-Agent' = 'spice2x-updater'; 'Accept' = 'application/vnd.github+json' }

    # --- find the newest release (beta = include pre-releases) --------------
    Write-Host 'Querying latest release...'
    $url = if ($beta) { "https://api.github.com/repos/$repo/releases?per_page=1" } else { "https://api.github.com/repos/$repo/releases/latest" }
    $rel = Invoke-RestMethod -Headers $headers -Uri $url | Select-Object -First 1
    if (-not $rel) { throw 'No releases found.' }

    # --- pick the distribution zip (spice2x-<date>.zip, not the -full one) --
    $asset = $rel.assets | Where-Object { $_.name -like 'spice2x-*.zip' -and $_.name -notlike '*-full.zip' } | Select-Object -First 1
    if (-not $asset) { throw 'No .zip asset found in the release.' }
    Write-Host "Latest release: $($rel.tag_name)$(if ($rel.prerelease) { ' [pre-release]' })  (asset: $($asset.name))"

    # --- download the zip into memory ---------------------------------------
    Write-Host 'Downloading...'
    $bytes = (Invoke-WebRequest -Headers $headers -Uri $asset.browser_download_url -UseBasicParsing).Content

    # --- extract just the three executables straight into this folder -------
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipArchive]::new([IO.MemoryStream]::new([byte[]]$bytes))
    try {
        $updated = 0
        foreach ($name in $targets) {
            $entry = $zip.Entries | Where-Object { $_.Name -eq $name } | Select-Object -First 1
            if (-not $entry) { Write-Warning "  $name not found in the archive"; continue }
            try {
                [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $scriptDir $name), $true)
                Write-Host "  updated $name"; $updated++
            } catch {
                Write-Warning "  FAILED to write $name (running / read-only?): $($_.Exception.Message)"
            }
        }
    } finally { $zip.Dispose() }

    Write-Host "`nDone. $updated of $($targets.Count) executables updated to $($rel.tag_name)."
    Write-Host "Only the .exe file are updated; if you copied any DLL stubs, they were not changed."
    if ($updated -ne $targets.Count) { $rc = 1 }
} catch {
    Write-Host ''; Write-Error $_.Exception.Message; $rc = 1
}

# --- result ------------------------------------------------------------------
Write-Host $(if ($rc) { "`nUpdate FAILED. See the error above." } else { "`nUpdate finished successfully." })
Start-Sleep -Seconds 5
exit $rc
