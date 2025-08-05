
# ==============================================
# 🛡️ All-in-One Secure PC Script for Windows (Run as Administrator)
# ==============================================

# ========== Section 1: Enable System Restore and Schedule Daily Restore Point ==========
Write-Host "`n🧩 Enabling System Restore and Daily Restore Point..." -ForegroundColor Cyan
Enable-ComputerRestore -Drive "C:\"
Checkpoint-Computer -Description "Initial Restore Point" -RestorePointType MODIFY_SETTINGS

$scriptPath = "$env:ProgramData\CreateRestorePoint.ps1"
$scriptContent = @"
Checkpoint-Computer -Description 'Daily Restore Point' -RestorePointType MODIFY_SETTINGS
"@
$scriptContent | Out-File -FilePath $scriptPath -Encoding UTF8

$action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument "-ExecutionPolicy Bypass -File `"$scriptPath`""
$trigger = New-ScheduledTaskTrigger -Daily -At 10am
$principal = New-ScheduledTaskPrincipal -UserId "SYSTEM" -RunLevel Highest
Register-ScheduledTask -Action $action -Trigger $trigger -Principal $principal -TaskName "DailyRestorePoint" -Force

# ========== Section 2: Disable USB Storage and Block Dangerous Scripts ==========
Write-Host "`n🔒 Disabling USB storage and blocking dangerous scripts..." -ForegroundColor Cyan
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\USBSTOR" -Name "Start" -Value 4
New-Item -Path "HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions" -Force | Out-Null
Set-ItemProperty -Path "HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions" -Name "DenyUnspecified" -Value 1
Set-ItemProperty -Path "HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions" -Name "DenyUnspecifiedDeviceClasses" -Value 1

$basePath = "HKLM:\SOFTWARE\Policies\Microsoft\Windows\Safer\CodeIdentifiers"
$rulesPath = "$basePath\0\Paths"
New-Item -Path $basePath -Force | Out-Null
Set-ItemProperty -Path $basePath -Name "TransparentEnabled" -Value 1
Set-ItemProperty -Path $basePath -Name "DefaultLevel" -Value 0x10000
New-Item -Path "$basePath\0" -Force | Out-Null
New-Item -Path $rulesPath -Force | Out-Null

$extensions = @("*.ps1", "*.vbs", "*.js", "*.bat", "*.cmd", "*.hta")
$i = 0
foreach ($ext in $extensions) {
    $rule = "$rulesPath\{BLOCK-$i}"
    New-Item -Path $rule -Force | Out-Null
    Set-ItemProperty -Path $rule -Name "ItemData" -Value $ext
    Set-ItemProperty -Path $rule -Name "SaferFlags" -Value 0
    Set-ItemProperty -Path $rule -Name "LastModified" -Value (Get-Date)
    $i++
}

$wsh = "HKLM:\Software\Microsoft\Windows Script Host\Settings"
New-Item -Path $wsh -Force | Out-Null
Set-ItemProperty -Path $wsh -Name "Enabled" -Value 0

$officeVersions = @("16.0", "15.0", "14.0")
foreach ($ver in $officeVersions) {
    $macroKey = "HKCU:\Software\Microsoft\Office\$ver\Word\Security"
    if (!(Test-Path $macroKey)) { New-Item -Path $macroKey -Force | Out-Null }
    Set-ItemProperty -Path $macroKey -Name "VBAWarnings" -Value 4
}

# ========== Section 3: Harden Microsoft Defender ==========
Write-Host "`n🛡️ Hardening Microsoft Defender..." -ForegroundColor Cyan
Set-MpPreference -DisableRealtimeMonitoring $false
Set-MpPreference -MAPSReporting Advanced
Set-MpPreference -SubmitSamplesConsent SendSafeSamples
Set-MpPreference -CloudBlockLevel High
Set-MpPreference -DisableBlockAtFirstSeen $false
Set-MpPreference -DisableIOAVProtection $false
Set-MpPreference -SignatureDisableUpdateOnStartupWithoutEngine $false
Set-MpPreference -EnableNetworkProtection Enabled
Set-MpPreference -DisableScriptScanning $false
Set-MpPreference -PUAProtection Enabled
Set-MpPreference -EnableControlledFolderAccess Enabled

Add-MpPreference -ControlledFolderAccessProtectedFolders "C:\Users\*\Documents"
Add-MpPreference -ControlledFolderAccessProtectedFolders "C:\Users\*\Pictures"
Add-MpPreference -ControlledFolderAccessProtectedFolders "C:\Users\*\Desktop"

$asrRules = @(
    "D4F940AB-401B-4EFC-AADC-AD5F3C50688A",
    "3B576869-A4EC-4529-8536-B80A7769E899",
    "75668C1F-73B5-4CF0-BB93-3ECF5CB7CC84",
    "26190899-1602-49E8-8B27-EB1D0A1CE869",
    "9E6B8B9A-C2A6-4C9F-8267-7F6CFCF7A144",
    "B2B3F03D-6A65-4F7B-A9C7-1C7EF74A9BA4",
    "C1DB55AB-C21A-4637-BB3F-A12568109D35"
)
foreach ($rule in $asrRules) {
    Add-MpPreference -AttackSurfaceReductionRules_Ids $rule -AttackSurfaceReductionRules_Actions Enabled
}

try {
    New-Item -Path "HKLM:\SOFTWARE\Microsoft\Windows Defender\Features" -Force | Out-Null
    Set-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows Defender\Features" -Name "TamperProtection" -Value 5
} catch {}

Update-MpSignature

Write-Host "`n✅ All security configurations applied successfully. Please reboot your PC." -ForegroundColor Green
