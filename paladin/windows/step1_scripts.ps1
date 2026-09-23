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
Write-Host "[1/4] маркеров в spell_paladin.cpp сейчас: $cur (нужно 0 или 10)"
if ($cur -eq 10) {
    Write-Host ">>> УЖЕ УСТАНОВЛЕНО — вставка пропущена"
} else {
    if ($cur -ne 0) {
        Write-Host ">>> файл неполный/с дубликатами — откатываю git'ом и ставлю заново"
        Push-Location $core
        git checkout -- 'src/server/scripts/Spells/spell_paladin.cpp' 2>$null
        Pop-Location
    }
    Write-Host "[2/4] Дописываю 10 файлов..."
    foreach ($f in $files) {
        $t = Get-Content -Raw $f
        $i = $t.IndexOf($marker)
        if ($i -lt 0) { Write-Host "[X] маркер не найден в $f"; Read-Host Enter; exit 1 }
        [IO.File]::AppendAllText($pal, "`r`n" + $t.Substring($i))
        Write-Host ("    добавлен: " + (Split-Path -Leaf $f))
    }
    $now = ([regex]::Matches((Get-Content -Raw $pal), [regex]::Escape($marker))).Count
    Write-Host "[3/4] маркеров стало: $now (нужно 10)"
    if ($now -eq 10) { Write-Host ">>> ШАГ 1.1 ВЫПОЛНЕН" -ForegroundColor Green }
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
