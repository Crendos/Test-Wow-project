#!/usr/bin/env bash
# ============================================================================
# Паладин-фиксы — ОБЩАЯ ПРОВЕРКА шагов 0–1 (+ сборки, если есть).
# ЗАПУСК из корня ядра:
#   bash <(tr -d '\r' < "C:/paladin-fixes/paladin/windows/check_all.sh")
# ============================================================================
set -u
FAIL=0

chk(){ # chk ЧИСЛО_ОЖИДАНИЕ ОПИСАНИЕ КОМАНДА...
  local want="$1" desc="$2"; shift 2
  local got; got=$("$@" 2>/dev/null | tail -1)
  if [ "$got" = "$want" ]; then echo ">>> OK: $desc = $got"; else echo ">>> НЕ СОВПАЛО: $desc = '${got}' (ожидалось $want)"; FAIL=1; fi
}

echo "=== Шаг 0: патч ядра ==="
[ -f src/server/game/Entities/Unit/Unit.cpp ] || { echo ">>> запустите из КОРНЯ ядра"; exit 1; }
chk 3 "блок spellBlockChance в Unit.cpp" grep -c "spellBlockChance" src/server/game/Entities/Unit/Unit.cpp
chk 1 "замена 529 в SpellAuraEffects.cpp" grep -c "HandleNoImmediateEffect,                         //529" src/server/game/Spells/Auras/SpellAuraEffects.cpp

echo "=== Шаг 1.1: партии ==="
chk 10 "маркеров CUT HERE в spell_paladin.cpp" grep -c "^// === CUT HERE" src/server/scripts/Spells/spell_paladin.cpp

echo "=== Шаг 1.2: лоадер ==="
chk 9 "объявлений ex2..ex10" grep -c "void AddSC_paladin_spell_scripts_ex" src/server/scripts/Spells/spell_script_loader.cpp
chk 9 "вызовов ex2..ex10" grep -c "    AddSC_paladin_spell_scripts_ex" src/server/scripts/Spells/spell_script_loader.cpp

echo "=== Шаг 3: сборка (если уже собирали) ==="
if [ -f build/bin/Release/worldserver.exe ]; then
  grep -qa "spell_pal_glory_of_the_vanguard_ex" build/bin/Release/worldserver.exe \
    && echo ">>> OK: скрипты внутри worldserver.exe" \
    || echo ">>> exe без скриптов — пересоберите после Шага 1"
else
  echo "— (exe ещё не собран — пропускаю)"
fi

[ "$FAIL" = "0" ] && echo "=== ВСЁ СХОДИТСЯ: шаги 0–1 выполнены ===" || echo "=== ЕСТЬ РАСХОЖДЕНИЯ — пришлите этот вывод целиком ==="
