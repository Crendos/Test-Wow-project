# Paladin fixes — ШАГ 1: дописать 10 партий в spell_paladin.cpp (+ проверить лоадер)
# Пути определяются автоматически. Запуск: двойной клик по step1_scripts.bat
$ErrorActionPreference = 'Stop'
$fix = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path "$fix\spell_paladin_class_fixes.cpp")) {
    $fix = (Read-Host "Не нашёл папку paladin — вставьте путь (правый клик = вставка)").Trim('"')
}
if (-not (Test-Path "$fix\spell_paladin_class_fixes.cpp")) { Write-Host "[X] файлы фиксов не найдены в $fix"; Read-Host Enter; exit 1 }

$core = @(
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

$files = @((Join-Path $fix 'spell_paladin_class_fixes.cpp')) + (2..10 | ForEach-Object { Join-Path $fix ("spell_paladin_class_fixes_{0}.cpp" -f $_) })
foreach ($f in $files) { if (-not (Test-Path $f)) { Write-Host "[X] нет файла: $f"; Read-Host Enter; exit 1 } }

$raw = Get-Content -Raw $pal
$cur = ([regex]::Matches($raw, [regex]::Escape($marker))).Count
$hasNew = $raw.Contains('MakeSpellArgs')
Write-Host "[1/4] маркеров в spell_paladin.cpp сейчас: $cur (нужно 0 или 10)"
$needRollback = $false
if ($cur -eq 10) {
    if ($hasNew) {
        Write-Host ">>> УЖЕ УСТАНОВЛЕНО (версия MakeSpellArgs) — вставка пропущена"
    } else {
        Write-Host ">>> найдена СТАРАЯ версия партий (MSVC-несовместимая) — заменяю на новую"
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
        Write-Host "[X] git не смог откатить файл — выполните ВРУЧНУЮ в этой консоли:" -ForegroundColor Red
        Write-Host "    cd /d `"$core`""
        Write-Host "    git checkout -- src/server/scripts/Spells/spell_paladin.cpp"
        Write-Host "    затем запустите step1_scripts.bat заново"
        Read-Host "Нажмите Enter для выхода"; exit 1
    }
    Write-Host ">>> откат выполнен (0 маркеров)"
}
if ($cur -eq 0) {
    Write-Host "[2/4] Дописываю 10 файлов..."
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
    Write-Host "[3/4] маркеров стало: $now (нужно 10)"
    Write-Host "[3b]  MakeSpellArgs в файле: $ms (ожидается 18)"
    if ($now -eq 10 -and $ms -eq 18) { Write-Host ">>> ШАГ 1.1 ВЫПОЛНЕН (MSVC-совместимая версия)" -ForegroundColor Green }
    else { Write-Host "[X] НЕ СХОДИТСЯ — пришлите этот вывод целиком" -ForegroundColor Red; Read-Host Enter; exit 1 }
}

Write-Host "[4/4] Лоадер"
$ld = Get-Content -Raw $loader
if ($ld -match 'void AddSC_paladin_spell_scripts_ex10\(\);') {
    Write-Host ">>> лоадер: объявления ex2..ex10 на месте" -ForegroundColor Green
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
