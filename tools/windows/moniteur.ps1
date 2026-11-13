# Serial monitor for the Heltec V4 (native USB). Does NOT reset the board when opening.
# Survives board resets (the USB port disappears/reappears): it reconnects automatically.
param([string]$Port = "", [string]$Log = "journal_serie.txt")
$ErrorActionPreference = "Stop"
if (-not $Port) {
    $ports = @(Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -match 'USB' } |
        ForEach-Object { [regex]::Match($_.FriendlyName, 'COM[0-9]+').Value } | Where-Object { $_ })
    if ($ports.Count -eq 1) { $Port = $ports[0] }
    else { $Port = Read-Host "Port COM de la carte (ex: COM16)" }
}
$Port = $Port.ToUpper().Trim()
if ($Port -notmatch '^COM') { $Port = "COM$Port" }
$logPath = Join-Path (Get-Location) $Log
"=== Journal $(Get-Date -Format s) sur $Port ===" | Out-File -FilePath $logPath -Encoding utf8
Write-Host "Moniteur serie sur $Port - journal : $logPath"
Write-Host "Ferme cette fenetre (ou Ctrl+C) pour arreter."
Write-Host ""
while ($true) {
    $sp = New-Object System.IO.Ports.SerialPort $Port, 115200, None, 8, One
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.ReadTimeout = 500
    try {
        $sp.Open()
        Write-Host "[connecte a $Port]" -ForegroundColor Green
        while ($true) {
            try {
                $line = $sp.ReadLine()
                $stamp = (Get-Date -Format "HH:mm:ss.fff")
                Write-Host $line
                "$stamp $line" | Out-File -FilePath $logPath -Append -Encoding utf8
            } catch [System.TimeoutException] { }
        }
    } catch {
        if ($sp.IsOpen) { try { $sp.Close() } catch {} }
        Write-Host "[port $Port indisponible - nouvelle tentative...]" -ForegroundColor Yellow
        "$(Get-Date -Format "HH:mm:ss.fff") [port perdu/attente]" | Out-File -FilePath $logPath -Append -Encoding utf8
        Start-Sleep -Milliseconds 700
    }
}
