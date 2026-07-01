$ErrorActionPreference = 'Stop'
$base   = 'C:\Users\samym\Downloads\Honor-EFI\EFI'
$root   = 'C:\Users\samym\Downloads\HonorFB\efi-variants'
$uefifb = 'C:\Users\samym\Downloads\HonorFB\tools\UEFIGraphicsFB\UEFIGraphicsFB.kext'
$honorfb= 'C:\Users\samym\Downloads\HonorFB\HonorFB.kext'
$ocv    = Get-ChildItem 'C:\Users\samym\Downloads\Honor-EFI\build' -Recurse -File -Filter 'ocvalidate.exe' -ErrorAction SilentlyContinue | Select-Object -First 1

New-Item -ItemType Directory -Force -Path $root | Out-Null

$bootOld = '-v keepsyms=1 debug=0x100 -wegnoegpu'
$bootA   = '-v keepsyms=1 debug=0x100 igfxvesa'
$bootBC  = '-v keepsyms=1 debug=0x100'

# --- anchors (tabs comme dans config.plist) ---
$kernelAnchor = @"
		<key>Add</key>
		<array>
			<dict>
				<key>Arch</key>
				<string>x86_64</string>
				<key>BundlePath</key>
				<string>Lilu.kext</string>
"@
function KextEntry($bundle, $exe, $comment) {
@"
		<key>Add</key>
		<array>
			<dict>
				<key>Arch</key>
				<string>x86_64</string>
				<key>BundlePath</key>
				<string>$bundle</string>
				<key>Comment</key>
				<string>$comment</string>
				<key>Enabled</key>
				<true/>
				<key>ExecutablePath</key>
				<string>Contents/MacOS/$exe</string>
				<key>MaxKernel</key>
				<string></string>
				<key>MinKernel</key>
				<string></string>
				<key>PlistPath</key>
				<string>Contents/Info.plist</string>
			</dict>
			<dict>
				<key>Arch</key>
				<string>x86_64</string>
				<key>BundlePath</key>
				<string>Lilu.kext</string>
"@
}
$dpAnchor = @"
	<key>DeviceProperties</key>
	<dict>
		<key>Add</key>
		<dict/>
"@
$dpNeutralize = @"
	<key>DeviceProperties</key>
	<dict>
		<key>Add</key>
		<dict>
			<key>PciRoot(0x0)/Pci(0x2,0x0)</key>
			<dict>
				<key>name</key>
				<data>PHVudXNlZD4=</data>
				<key>class-code</key>
				<data>/////w==</data>
			</dict>
		</dict>
"@

function Build($name, $boot, $kextDir, $kextBundle, $kextExe, $kextComment, $neutralize) {
    $dst = Join-Path $root "$name\EFI"
    if (Test-Path (Join-Path $root $name)) { Remove-Item -Recurse -Force (Join-Path $root $name) }
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item -Recurse -Force "$base\*" $dst
    $cfgPath = Join-Path $dst 'OC\config.plist'
    $cfg = [IO.File]::ReadAllText($cfgPath)

    # boot-args
    if (-not $cfg.Contains($bootOld)) { Write-Host "  [$name] WARN: boot-args anchor introuvable" -ForegroundColor Yellow }
    $cfg = $cfg.Replace($bootOld, $boot)

    if ($kextDir) {
        Copy-Item -Recurse -Force $kextDir (Join-Path $dst 'OC\Kexts')
        $entry = KextEntry $kextBundle $kextExe $kextComment
        if (-not $cfg.Contains($kernelAnchor)) { Write-Host "  [$name] WARN: Kernel/Add anchor introuvable" -ForegroundColor Yellow }
        $cfg = $cfg.Replace($kernelAnchor, $entry)
    }
    if ($neutralize) {
        if (-not $cfg.Contains($dpAnchor)) { Write-Host "  [$name] WARN: DeviceProperties anchor introuvable" -ForegroundColor Yellow }
        $cfg = $cfg.Replace($dpAnchor, $dpNeutralize)
    }
    [IO.File]::WriteAllText($cfgPath, $cfg)

    # validate
    if ($ocv) {
        $out = & $ocv.FullName $cfgPath 2>&1 | Out-String
        if ($out -match 'No issues found') { Write-Host "  [$name] ocvalidate: OK" -ForegroundColor Green }
        else { Write-Host "  [$name] ocvalidate: ERREURS" -ForegroundColor Red; Write-Host $out }
    }
}

Write-Host "== Variante A (igfxvesa, zero kext) =="
Build 'Honor-EFI-A-igfxvesa' $bootA $null $null $null $null $false

Write-Host "== Variante B (UEFIGraphicsFB + neutralisation iGPU) =="
Build 'Honor-EFI-B-uefigraphicsfb' $bootBC $uefifb 'UEFIGraphicsFB.kext' 'UEFIGraphicsFB' 'UEFIGraphicsFB boot framebuffer' $true

Write-Host "== Variante C (HonorFB EXPERIMENTAL + neutralisation iGPU) =="
Build 'Honor-EFI-C-honorfb-EXPERIMENTAL' $bootBC $honorfb 'HonorFB.kext' 'HonorFB' 'HonorFB EXPERIMENTAL (lld-linked, peut ne pas charger)' $true

Write-Host ""
Write-Host "Variantes dans: $root"
Get-ChildItem $root -Directory | ForEach-Object { Write-Host "  - $($_.Name)" }
