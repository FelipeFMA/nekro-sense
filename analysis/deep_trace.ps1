# Deep Trace - Capture ALL Predator Sense WMI Calls
# Run as Administrator!

param(
    [string]$OutputDir = ".\traces",
    [int]$Duration = 120
)

$ErrorActionPreference = "SilentlyContinue"

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

Write-Host "=== Predator Sense Deep Trace ===" -ForegroundColor Cyan
Write-Host "This will capture ALL WMI calls." -ForegroundColor White

$SessionName = "PredatorSenseTrace"
$EtlFile = "$OutputDir\wmi_trace.etl"

# Stop any existing session
logman stop $SessionName -ets 2>$null
logman delete $SessionName -ets 2>$null

Write-Host "[1/3] Starting ETW trace session..." -ForegroundColor Yellow

# Start trace
logman create trace $SessionName -o "$EtlFile" -ets -mode Circular -bs 1024 -max 256 2>$null

# Add WMI providers
logman update trace $SessionName -p "{1418EF04-B0B4-4623-BF7E-D74AB47BBDAA}" 0xffffffffffffffff 0xff -ets 2>$null
logman update trace $SessionName -p "{1FF6B227-2CA7-40F9-9A66-980EADAA602E}" 0xffffffffffffffff 0xff -ets 2>$null

Write-Host "[2/3] Trace running!" -ForegroundColor Green
Write-Host ""
Write-Host "Now interact with Predator Sense:" -ForegroundColor Cyan
Write-Host "  1. Switch to QUIET mode, wait 3 seconds" -ForegroundColor White
Write-Host "  2. Switch to BALANCED mode, wait 3 seconds" -ForegroundColor White
Write-Host "  3. Switch to PERFORMANCE mode, wait 3 seconds" -ForegroundColor White
Write-Host "  4. Switch to TURBO mode, wait 3 seconds" -ForegroundColor White
Write-Host ""
Write-Host "Press Enter when done..." -ForegroundColor Yellow
Read-Host

Write-Host "[3/3] Stopping trace..." -ForegroundColor Yellow
logman stop $SessionName -ets

# Parse trace
Write-Host "Parsing trace file..." -ForegroundColor Yellow
tracerpt $EtlFile -o "$OutputDir\wmi_events.xml" -of XML -y 2>$null
tracerpt $EtlFile -o "$OutputDir\wmi_events.csv" -of CSV -y 2>$null

Write-Host ""
Write-Host "=== Trace Complete ===" -ForegroundColor Green
Write-Host "Files saved to: $OutputDir" -ForegroundColor Cyan
Get-ChildItem $OutputDir | ForEach-Object { Write-Host "  - $($_.Name)" }
