# Paladin fixes — проверка шагов 0–1 (+сборки). Пути автоматически.
$ErrorActionPreference = 'Continue'
$core = @(
    "$env:USERPROFILE\Desktop\new\test\TrinityCore",
    "$env:USERPROFILE\Desktop\TrinityCore",
    "C:\TrinityCore", "C:\TrinityCore\TrinityCore",
    "D:\TrinityCore", "D:\TrinityCore\TrinityCore"
) | Where-Object { Test-Path "$_\src\server\scripts\Spells\spell_paladin.cpp" } | Select-Object -First 1
if (-not $core) { $core = (Read-Host "Путь к корню ядра").Trim('"') }
$fail = 0
function Chk($ok, $okMsg, $badMsg) {
    if ($ok) { Write-Host "[OK] $okMsg" -ForegroundColor Green }
    else     { Write-Host "[!!] $badMsg" -ForegroundColor Red; $script:fail = 1 }
}
Write-Host "=== Проверка (ядро: $core) ===" -ForegroundColor Cyan
$u = Get-Content -Raw "$core\src\server\game\Entities\Unit\Unit.cpp"
Chk ($u -match 'spellBlockChance') 'Шаг 0: Unit.cpp пропатчен' 'Шаг 0: Unit.cpp БЕЗ патча'
$a = Get-Content -Raw "$core\src\server\game\Spells\Auras\SpellAuraEffects.cpp"
Chk ($a -match 'HandleNoImmediateEffect,\s*//529') 'Шаг 0: 529 заменён' 'Шаг 0: 529 не заменён'
$pal = Get-Content -Raw "$core\src\server\scripts\Spells\spell_paladin.cpp"
$n = ([regex]::Matches($pal, [regex]::Escape('// === CUT HERE'))).Count
Chk ($n -eq 10) "Шаг 1.1: партий 10" "Шаг 1.1: партий $n (нужно 10)"
$ld = Get-Content -Raw "$core\src\server\scripts\Spells\spell_script_loader.cpp"
$na = ([regex]::Matches($ld, [regex]::Escape('void AddSC_paladin_spell_scripts_ex'))).Count
Chk ($na -eq 9) "Шаг 1.2: объявлений 9" "Шаг 1.2: объявлений $na (нужно 9)"
Chk ($ld -match 'AddSC_paladin_spell_scripts_ex2\(\);') 'Шаг 1.2: вызовы на месте' 'Шаг 1.2: вызовов нет'
$exe = "$core\build\bin\Release\worldserver.exe"
if (Test-Path $exe) {
    $bytes = [IO.File]::ReadAllBytes($exe)
    $needle = [Text.Encoding]::ASCII.GetBytes('spell_pal_glory_of_the_vanguard_ex')
    $found = $false
    for ($i = 0; $i -le $bytes.Length - $needle.Length; $i += 4096) {
        $start = [Math]::Max(0, $i - $needle.Length)
        $len = [Math]::Min(8192, $bytes.Length - $start)
        $chunk = New-Object byte[] $len
        [Array]::Copy($bytes, $start, $chunk, 0, $len)
        for ($j = 0; $j -le $len - $needle.Length; $j++) {
            $m = $true
            for ($k = 0; $k -lt $needle.Length; $k++) { if ($chunk[$j+$k] -ne $needle[$k]) { $m = $false; break } }
            if ($m) { $found = $true; break }
        }
        if ($found) { break }
    }
    Chk $found 'Шаг 3: скрипты внутри worldserver.exe' 'Шаг 3: exe без скриптов — пересоберите'
} else { Write-Host "[—] exe ещё не собран — пропускаю" }

if ($fail -eq 0) { Write-Host "=== ВСЁ СХОДИТСЯ ===" -ForegroundColor Green }
else             { Write-Host "=== ЕСТЬ РАСХОЖДЕНИЯ — пришлите этот вывод целиком ===" -ForegroundColor Red }
Read-Host "Нажмите Enter для выхода"
