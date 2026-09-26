# memwatch.ps1 — мониторинг памяти worldserver/MySQL (диагностика «12 ГБ ОЗУ»)
# Запуск (PowerShell):  powershell -ExecutionPolicy Bypass -File memwatch.ps1
# Или двойной клик по memwatch.bat. Лог пишется рядом: memwatch.csv
# Остановка: Ctrl+C. Колонки: время; worldserver МБ (Private); mysqld МБ (Private);
#            всего занято ОС МБ; свободно МБ; число процессов worldserver.

$csv = Join-Path $PSScriptRoot "memwatch.csv"
if (-not (Test-Path $csv)) {
    "time,ws_private_mb,mysql_private_mb,os_used_mb,os_free_mb,ws_count" | Set-Content -Path $csv -Encoding UTF8
}
Write-Host "Мониторинг памяти каждые 30 с. Лог: $csv  (Ctrl+C = стоп)"
while ($true) {
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $ws = @(Get-Process worldserver -ErrorAction SilentlyContinue)
    $my = @(Get-Process mysqld -ErrorAction SilentlyContinue)
    $wsMb = 0; foreach ($p in $ws) { $wsMb += [math]::Round($p.PrivateMemorySize64 / 1MB) }
    $myMb = 0; foreach ($p in $my) { $myMb += [math]::Round($p.PrivateMemorySize64 / 1MB) }
    $os = Get-CimInstance Win32_OperatingSystem
    $osUsed = [math]::Round(($os.TotalVisibleMemorySize - $os.FreePhysicalMemory) / 1024)
    $osFree = [math]::Round($os.FreePhysicalMemory / 1024)
    $line = "$ts,$wsMb,$myMb,$osUsed,$osFree,$($ws.Count)"
    Add-Content -Path $csv -Value $line
    Write-Host $line
    Start-Sleep -Seconds 30
}
