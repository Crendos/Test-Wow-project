# Paladin fixes — ШАГ 0b: правка ЯДРА (Spell.cpp) для скриптов, которые смотрят крит
# (SpellScript::IsHitCrit) на «летящих» спеллах: Правосудие, Молот гнева, Эмпирейский молот и др.
# Без этой правки worldserver падает: ASSERTION FAILED SpellScript.cpp:657 в SpellScript::IsHitCrit
# (itr != m_spell->m_UniqueTargetInfo.end()).
# Идемпотентно: повторный запуск ничего не портит. Запуск: двойной клик по step0b_core_critfix.bat
$ErrorActionPreference = 'Stop'

$core = @(
    'F:\Games\server\comp\Trinitycore',
    'F:\Games\server\comp\TrinityCore',
    "$env:USERPROFILE\Desktop\new\test\TrinityCore",
    "$env:USERPROFILE\Desktop\TrinityCore",
    'C:\TrinityCore', 'C:\TrinityCore\TrinityCore',
    'D:\TrinityCore', 'D:\TrinityCore\TrinityCore'
) | Where-Object { Test-Path "$_\src\server\game\Spells\Spell.cpp" } | Select-Object -First 1
if (-not $core) {
    $core = (Read-Host "Не нашёл ядро — вставьте путь к КОРНЮ (папка с src\)").Trim('"')
}
$file = Join-Path $core 'src\server\game\Spells\Spell.cpp'
if (-not (Test-Path $file)) { Write-Host "[X] не нашёл $file"; Read-Host "Enter"; exit 1 }

Write-Host "=== Шаг 0b: правка ядра ===" -ForegroundColor Cyan
Write-Host "[i] ядро: $core"

$raw = [IO.File]::ReadAllText($file)
if ($raw.Contains('PAL_CORE_CRITFIX_20260928')) {
    Write-Host ">>> правка ядра УЖЕ на месте (PAL_CORE_CRITFIX_20260928) — ничего не меняю" -ForegroundColor Green
    Read-Host "Нажмите Enter для выхода"
    exit 0
}

# Старый блок: цели «долетают» и вынимаются из m_UniqueTargetInfo ДО обработки — из-за этого
# SpellScript::IsHitCrit() не находит текущую цель и падает на ASSERT.
$pattern = '(?s)[ \t]*std::vector<TargetInfo> delayedTargets;.*?DoProcessTargetContainer\(delayedTargets\);'
$m = [regex]::Match($raw, $pattern)
if (-not $m.Success) {
    Write-Host "[X] не нашёл блок handle_delayed в Spell.cpp — ядро другой ревизии." -ForegroundColor Red
    Write-Host "    Пришлите это сообщение: правку сделаем вручную (см. core_patch\0002-core-IsHitCrit-delayed-targets.patch)." -ForegroundColor Yellow
    Read-Host "Нажмите Enter для выхода"
    exit 1
}

$new = @'
        // PAL_CORE_CRITFIX_20260928:
        // Targets that the missile already reached are processed directly in m_UniqueTargetInfo and
        // removed from the list only AFTER processing. Previously they were moved into a local vector
        // BEFORE processing, so SpellScript::IsHitCrit() could not find the current target in
        // m_UniqueTargetInfo and hit ASSERT (itr != m_UniqueTargetInfo.end()) - worldserver crashed
        // on any spell with travel time (Judgment, Hammer of Wrath, Empyrean Hammer, ...).
        // Processing order and next_time logic are unchanged.
        std::vector<size_t> readyTargets;
        readyTargets.reserve(m_UniqueTargetInfo.size());
        for (TargetInfo const& target : m_UniqueTargetInfo)
        {
            if (ignoreTargetInfoTimeDelay || target.TimeDelay <= t_offset)
                readyTargets.push_back(size_t(&target - m_UniqueTargetInfo.data()));
            else if (!single_missile && (next_time == 0 || target.TimeDelay < next_time))
                next_time = target.TimeDelay;
        }

        for (size_t index : readyTargets)
        {
            TargetInfo& target = m_UniqueTargetInfo[index];
            target.TimeDelay = t_offset;
            target.PreprocessTarget(this);
        }

        for (SpellEffectInfo const& spellEffectInfo : m_spellInfo->GetEffects())
            for (size_t index : readyTargets)
            {
                TargetInfo& target = m_UniqueTargetInfo[index];
                if (target.EffectMask & (1 << spellEffectInfo.EffectIndex))
                    target.DoTargetSpellHit(this, spellEffectInfo);
            }

        for (size_t index : readyTargets)
            m_UniqueTargetInfo[index].DoDamageAndTriggers(this);

        // remove already processed targets (previously it was done before processing)
        for (auto itr = readyTargets.rbegin(); itr != readyTargets.rend(); ++itr)
            m_UniqueTargetInfo.erase(m_UniqueTargetInfo.begin() + std::ptrdiff_t(*itr));
'@
$new = $new.TrimEnd()
$nl = if ($raw.Contains("`r`n")) { "`r`n" } else { "`n" }
$new = $new -replace "`r?`n", $nl

Copy-Item -Path $file -Destination "$file.bak_critfix" -Force
$patched = $raw.Substring(0, $m.Index) + $new + $raw.Substring($m.Index + $m.Length)
[IO.File]::WriteAllText($file, $patched, (New-Object System.Text.UTF8Encoding $false))

$check = [IO.File]::ReadAllText($file)
if (-not $check.Contains('PAL_CORE_CRITFIX_20260928')) {
    Write-Host "[X] запись не удалась — восстановите Spell.cpp из Spell.cpp.bak_critfix" -ForegroundColor Red
    Read-Host "Нажмите Enter для выхода"
    exit 1
}

Write-Host ">>> Spell.cpp пропатчен (резервная копия: Spell.cpp.bak_critfix)" -ForegroundColor Green
Write-Host ">>> Теперь пересоберите ядро: cmake --build build --config Release -j 8"
Write-Host ">>> После сборки: paladin\windows\check_all.bat — строка про правку ядра должна быть [OK]"
Read-Host "Нажмите Enter для выхода"
