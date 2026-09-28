# Paladin fixes — ШАГ 1: дописать 11 партий в spell_paladin.cpp (+ проверить лоадер)
# Пути определяются автоматически. Запуск: двойной клик по step1_scripts.bat
$ErrorActionPreference = 'Stop'
$fix = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path "$fix\spell_paladin_class_fixes.cpp")) {
    $fix = (Read-Host "Не нашёл папку paladin — вставьте путь (правый клик = вставка)").Trim('"')
}
if (-not (Test-Path "$fix\spell_paladin_class_fixes.cpp")) { Write-Host "[X] файлы фиксов не найдены в $fix"; Read-Host Enter; exit 1 }

$core = @(
    (Join-Path (Split-Path $fix) 'Trinitycore'),
    (Join-Path (Split-Path $fix) 'TrinityCore'),
    'F:\Games\server\comp\Trinitycore',
    'F:\Games\server\comp\TrinityCore',
    "$env:USERPROFILE\Desktop\new\test\TrinityCore",
    "$env:USERPROFILE\Desktop\TrinityCore",
    "C:\TrinityCore", "C:\TrinityCore\TrinityCore",
    "D:\TrinityCore", "D:\TrinityCore\TrinityCore"
) | Where-Object { Test-Path "$_\src\server\scripts\Spells\spell_paladin.cpp" } | Select-Object -First 1
if (-not $core) {
    $core = (Read-Host "Не нашёл ядро — вставьте путь к КОРНЮ (папка с src\)").Trim('"')
}
if (-not (Test-Path "$core\src\server\scripts\Spells\spell_paladin.cpp")) { Write-Host "[X] ядро не найдено: $core"; Read-Host Enter; exit 1 }

$pal    = Join-Path $core 'src\server\scripts\Spells\spell_paladin.cpp'
$loader = Join-Path $core 'src\server\scripts\Spells\spell_script_loader.cpp'
$marker = '// === CUT HERE'
Write-Host "=== Шаг 1 ===" -ForegroundColor Cyan
Write-Host "[i] фиксы: $fix"
Write-Host "[i] ядро:  $core"

$files = @((Join-Path $fix 'spell_paladin_class_fixes.cpp')) + (2..11 | ForEach-Object { Join-Path $fix ("spell_paladin_class_fixes_{0}.cpp" -f $_) })
foreach ($f in $files) { if (-not (Test-Path $f)) { Write-Host "[X] нет файла: $f"; Read-Host Enter; exit 1 } }

$raw = Get-Content -Raw $pal
$cur = ([regex]::Matches($raw, [regex]::Escape($marker))).Count
$hasNew = $raw.Contains('MakeSpellArgs')
$hasAT    = $raw.Contains('RegisterAreaTriggerAI(at_pal_blessed_hammer)')
$hasGuard = $raw.Contains('procSpell->IsTriggered()')
$hasMech  = $raw.Contains('PAL_MECH_REV_20260926')
$hasSafe  = $raw.Contains('PAL_NO_PROTECTED_TARGETINFO')
$hasRev   = $raw.Contains('PAL_REV3_20260928')
$hasBad   = $raw.Contains('GetSpell()->m_UniqueTargetInfo')
Write-Host "[1/4] маркеров в spell_paladin.cpp сейчас: $cur (нужно 0 или 11)"
$needRollback = $false
if ($cur -eq 11) {
    if ($hasNew -and $hasAT -and $hasGuard -and $hasMech -and $hasSafe -and $hasRev -and -not $hasBad) {
        Write-Host ">>> УЖЕ УСТАНОВЛЕНО (последняя версия) — вставка пропущена"
    } else {
        Write-Host ">>> найдена УСТАРЕВШАЯ версия партий (нет PAL_REV3_20260928 / PAL_NO_PROTECTED_TARGETINFO или ещё есть protected-поле) — заменяю на новую"
        $needRollback = $true
    }
} elseif ($cur -ne 0) {
    Write-Host ">>> файл неполный/с дубликатами — нужен откат"
    $needRollback = $true
}
if ($needRollback) {
    Write-Host ">>> откатываю spell_paladin.cpp через git..."
    Push-Location $core
    git checkout -- 'src/server/scripts/Spells/spell_paladin.cpp' 2>$null
    Pop-Location
    $cur = ([regex]::Matches((Get-Content -Raw $pal), [regex]::Escape($marker))).Count
    if ($cur -ne 0) {
        $text = Get-Content -Raw $pal
        $idx = $text.IndexOf($marker)
        if ($idx -gt 0) {
            $kept = $text.Substring(0, $idx).TrimEnd()
            [IO.File]::WriteAllText($pal, $kept + "`r`n", (New-Object System.Text.UTF8Encoding $false))
            $cur = 0
            Write-Host ">>> git не откатил — обрезал файл до первого маркера партий"
        }
    }
    if ($cur -ne 0) {
        Write-Host "[X] не смог убрать старые партии из spell_paladin.cpp" -ForegroundColor Red
        Read-Host "Нажмите Enter для выхода"; exit 1
    }
    Write-Host ">>> откат выполнен (0 маркеров)"
}
if ($cur -eq 0) {
    Write-Host "[2/4] Дописываю 11 файлов..."
    foreach ($f in $files) {
        $t = Get-Content -Raw $f
        $i = $t.IndexOf($marker)
        if ($i -lt 0) { Write-Host "[X] маркер не найден в $f"; Read-Host Enter; exit 1 }
        [IO.File]::AppendAllText($pal, "`r`n" + $t.Substring($i), [Text.Encoding]::UTF8)
        Write-Host ("    добавлен: " + (Split-Path -Leaf $f))
    }
    $raw2 = Get-Content -Raw $pal
    $now = ([regex]::Matches($raw2, [regex]::Escape($marker))).Count
    $ms  = ([regex]::Matches($raw2, 'MakeSpellArgs')).Count
    $atN = ([regex]::Matches($raw2, [regex]::Escape('RegisterAreaTriggerAI(at_pal_blessed_hammer)'))).Count
    $gdN = ([regex]::Matches($raw2, [regex]::Escape('procSpell->IsTriggered()'))).Count
    $mech = $raw2.Contains('PAL_MECH_REV_20260926')
    $safe = $raw2.Contains('PAL_NO_PROTECTED_TARGETINFO') -and -not $raw2.Contains('GetSpell()->m_UniqueTargetInfo')
    Write-Host "[3/4] маркеров стало: $now (нужно 11)"
    Write-Host "[3b]  MakeSpellArgs в файле: $ms (ожидается 19)"
    Write-Host "[3c]  AT-скрипт молота: $atN (ожидается 1); анти-зацикливание: $gdN (ожидается 2)"
    Write-Host "[3d]  механика 26.09: $mech ; без protected-поля: $safe (ожидается True True)"
    if ($now -eq 11 -and $ms -eq 19 -and $atN -eq 1 -and $gdN -eq 2 -and $mech -and $safe) { Write-Host ">>> ШАГ 1.1 ВЫПОЛНЕН (последняя версия)" -ForegroundColor Green }
    else { Write-Host "[X] НЕ СХОДИТСЯ — пришлите этот вывод целиком" -ForegroundColor Red; Read-Host Enter; exit 1 }
}

Write-Host "[4/4] Лоадер"
$ld = Get-Content -Raw $loader
$decl = $ld -match 'void AddSC_paladin_spell_scripts_ex10\(\);'
$call = $ld -match '(?m)^\s*AddSC_paladin_spell_scripts_ex10\(\);'
if ($decl -and $call) {
    Write-Host ">>> лоадер: объявления И вызовы ex2..ex10 на месте" -ForegroundColor Green
} elseif ($decl) {
    Write-Host ">>> ЛОАДЕР: объявления ЕСТЬ, но ВЫЗОВОВ НЕТ (1.2б) — скрипты не зарегистрируются!" -ForegroundColor Yellow
    Write-Host "    файл: $loader"
    Write-Host '    после строки      AddSC_paladin_spell_scripts();'
    Write-Host '    добавьте 9 строк вызовов:      AddSC_paladin_spell_scripts_ex2(); ... _ex10();'
} else {
    Write-Host ">>> ЛОАДЕР ждёт ручных правок (Notepad++, 2 минуты):" -ForegroundColor Yellow
    Write-Host "    файл: $loader"
    Write-Host '    1.2а: после строки  void AddSC_paladin_spell_scripts();'
    Write-Host '          добавьте 9 строк: void AddSC_paladin_spell_scripts_ex2(); ... _ex10();'
    Write-Host '    1.2б: после строки  AddSC_paladin_spell_scripts();'
    Write-Host '          добавьте 9 строк вызовов:      AddSC_paladin_spell_scripts_ex2(); ... _ex10();'
}
Write-Host "=== Готово ===" -ForegroundColor Cyan
Read-Host "Нажмите Enter для выхода"
