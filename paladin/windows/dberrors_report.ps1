# ============================================================
# dberrors_report.ps1 — свод ошибок из DBErrors.log без заливки 875 МБ
# Запуск: dberrors_report.bat [путь к DBErrors.log]  (или просто bat — найдёт сам)
# Результат рядом с логом:
#   DBErrors_unique.txt  — уникальные ошибки + сколько раз каждая (главное!)
#   DBErrors_head100.txt — первые 100 строк (контекст загрузки)
#   DBErrors_tail100.txt — последние 100 строк (что спамит прямо сейчас)
# ============================================================
$ErrorActionPreference = "Stop"
Write-Host "=== DBErrors report ==="

$path = ""
if ($args.Count -ge 1 -and $args[0]) { $path = $args[0] }
if (-not $path -or -not (Test-Path $path)) { $path = Join-Path (Get-Location) "DBErrors.log" }
if (-not (Test-Path $path)) { $path = Join-Path $PSScriptRoot "DBErrors.log" }
if (-not (Test-Path $path)) {
    $hit = Get-ChildItem -Path "$env:USERPROFILE\Desktop" -Recurse -Filter "DBErrors.log" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($hit) { $path = $hit.FullName }
}
if (-not (Test-Path $path)) { Write-Host ">>> ОШИБКА: DBErrors.log не найден. Перетащи файл на dberrors_report.bat"; exit 1 }

$size = (Get-Item $path).Length
Write-Host (">>> файл: {0}  ({1:N0} МБ)" -f $path, ($size/1MB))

$patterns = @(
    @{p="doesn't have any ``ProcFlags``"; d="spell_proc без ProcFlags (наш маркер 432929 и ему подобные)"},
    @{p="does not exist";                 d="ссылка на несуществующий спелл/запись"},
    @{p="does not exist`"";               d="  (то же, в кавычках)"},
    @{p="listed in";                      d="запись в таблице ссылается на битое"},
    @{p="references invalid";             d="ссылка на невалидную запись"},
    @{p="has listed";                     d="таблица содержит битую строку"},
    @{p="not exist";                      d="  (общее 'не существует')"},
    @{p="Duplicate";                      d="дубликаты записей"},
    @{p="already has";                    d="повторная привязка"},
    @{p="Wrong";                          d="неверное значение"},
    @{p="invalid";                        d="  (общее 'невалидно')"},
    @{p="spellId";                        d="  (упоминания spellId)"},
    @{p="spell";                          d="  (любые spell-строки)"}
)

$counts = @{}
$uniq   = New-Object 'System.Collections.Hashtable'
$total  = [long]0
$srv    = $false
try { $srv = (Get-Process worldserver -ErrorAction SilentlyContinue) -ne $null } catch {}
if ($srv) { Write-Host ">>> ВНИМАНИЕ: worldserver ЗАПУЩЕН. Для чистого анализа останови его (файл может дописываться)." }

$sr = New-Object System.IO.StreamReader($path)
try {
    while ($null -ne ($line = $sr.ReadLine())) {
        $total++
        if (-not $uniq.ContainsKey($line)) { $uniq[$line] = [long]1 } else { $uniq[$line] = [long]($uniq[$line]) + [long]1 }
        if (($total % 2000000) -eq 0) { Write-Host (">>> обработано {0:N0} строк, уникальных {1:N0} ..." -f $total, $uniq.Count) }
    }
} finally { $sr.Close() }

foreach ($pat in $patterns) {
    $c = [long]0
    foreach ($k in $uniq.Keys) { if ($k -like ("*" + $pat.p + "*")) { $c += [long]$uniq[$k] } }
    Write-Host (">>> {0,-40} : {1:N0}" -f $pat.d, $c)
}

$outDir  = Split-Path $path
$uniqOut = Join-Path $outDir "DBErrors_unique.txt"
$top = $uniq.GetEnumerator() | Sort-Object { [long]$_.Value } -Descending | Select-Object -First 500
if ($uniq.Count -le 500) { $top = $uniq.GetEnumerator() | Sort-Object { [long]$_.Value } -Descending }
Set-Content -Path $uniqOut -Value ("# уникальных строк всего: " + $uniq.Count + " из " + $total) -Encoding UTF8
foreach ($e in $top) { Add-Content -Path $uniqOut -Value ("[" + $e.Value + "] " + $e.Key) -Encoding UTF8 }
Get-Content $path -TotalCount 100 | Set-Content (Join-Path $outDir "DBErrors_head100.txt") -Encoding UTF8
Get-Content $path -Tail 100       | Set-Content (Join-Path $outDir "DBErrors_tail100.txt") -Encoding UTF8

Write-Host (">>> ГОТОВО: уникальных строк {0:N0} из {1:N0} (показаны топ {2})" -f $uniq.Count, $total, $top.Count)
Write-Host (">>> ФАЙЛЫ: {0}" -f $uniqOut)
Write-Host (">>> пришли мне DBErrors_unique.txt (он маленький)")
