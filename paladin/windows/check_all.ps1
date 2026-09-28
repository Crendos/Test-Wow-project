# Paladin fixes — проверка шагов 0–1 (+сборки). Пути автоматически.
$ErrorActionPreference = 'Continue'
$core = @(
    'F:\Games\server\comp\Trinitycore',
    'F:\Games\server\comp\TrinityCore',
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
Chk ($n -eq 11) "Шаг 1.1: партий 11" "Шаг 1.1: партий $n (нужно 11)"
Chk ($pal -match 'PAL_REV6_20260928') 'Шаг 1.1: ревизия 28.09 (v6) на месте' 'Шаг 1.1: старые партии (нет ревизии 28.09 v6) — перезапустите step1_scripts.bat'
Chk ($pal -match 'PAL_MECH_REV_20260926') 'Шаг 1.1: механика 26.09 на месте' 'Шаг 1.1: старые партии — перезапустите step1_scripts.bat'
Chk (($pal -match 'PAL_NO_PROTECTED_TARGETINFO') -and ($pal -notmatch 'GetSpell\(\)->m_UniqueTargetInfo')) 'Шаг 1.1: нет обращения к protected-полю Spell' 'Шаг 1.1: в spell_paladin.cpp всё ещё старый Рассвет — перезапустите step1_scripts.bat'
$ld = Get-Content -Raw "$core\src\server\scripts\Spells\spell_script_loader.cpp"
$na = ([regex]::Matches($ld, [regex]::Escape('void AddSC_paladin_spell_scripts_ex'))).Count
Chk ($na -eq 9) "Шаг 1.2: объявлений 9" "Шаг 1.2: объявлений $na (нужно 9)"
$ncall = ([regex]::Matches($ld, '(?m)^\s*AddSC_paladin_spell_scripts_ex\d+\(\);')).Count
Chk ($ncall -ge 9) "Шаг 1.2: вызовов $ncall (нужно 9)" 'Шаг 1.2: вызовов нет/мало — скрипты НЕ регистрируются'
$exe = "$core\build\bin\Release\worldserver.exe"
if (Test-Path $exe) {
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe))
    $found = $text.Contains('spell_pal_glory_of_the_vanguard_ex')
    Chk $found 'Шаг 3: скрипты внутри worldserver.exe' 'Шаг 3: exe без скриптов — пересоберите'
} else { Write-Host "[—] exe ещё не собран — пропускаю" }

if ($fail -eq 0) { Write-Host "=== ВСЁ СХОДИТСЯ ===" -ForegroundColor Green }
else             { Write-Host "=== ЕСТЬ РАСХОЖДЕНИЯ — пришлите этот вывод целиком ===" -ForegroundColor Red }
Read-Host "Нажмите Enter для выхода"
