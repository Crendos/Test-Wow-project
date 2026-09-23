#!/usr/bin/env bash
# ============================================================================
# Паладин-фиксы — ШАГ 1: вставить 10 партий в spell_paladin.cpp + проверка.
# ЗАПУСК (одна строка, из Git Bash; набор руками, копировать не надо):
#   cd "C:/путь/к/TrinityCore"
#   bash <(tr -d '\r' < "C:/paladin-fixes/paladin/windows/step1_scripts.sh")
# Скрипт сам подсунет путь к папке фиксов (можно переопределить 1-м аргументом).
# ============================================================================
set -u
FIX="${1:-C:/paladin-fixes/paladin}"
F="src/server/scripts/Spells/spell_paladin.cpp"

[ -f "$F" ] || { echo ">>> ОШИБКА: не найден $F — сначала cd в КОРЕНЬ ядра"; exit 1; }
[ -d "$FIX" ] || { echo ">>> ОШИБКА: не найдена папка фиксов $FIX (передайте её пути 1-м аргументом)"; exit 1; }

echo "=== [1/3] Проверяю текущее состояние... ==="
N=$(grep -c "=== CUT HERE ===" "$F")
echo ">>> сейчас партий в файле: $N (нужно 10)"
if [ "$N" = "10" ]; then echo ">>> УЖЕ УСТАНОВЛЕНО, ничего не делаю. Если хотите переустановить: git checkout -- $F"; exit 0; fi
if [ "$N" != "0" ]; then echo ">>> файл частично/поверх заполнен. Чиню автоматически: git checkout -- $F"; git checkout -- "$F"; fi

echo "=== [2/3] Дописываю 10 файлов из $FIX ... ==="
for i in "" _2 _3 _4 _5 _6 _7 _8 _9 _10; do
  sed -n '/=== CUT HERE ===/,$p' "$FIX/spell_paladin_class_fixes${i}.cpp" >> "$F" \
    && echo "    добавлен: spell_paladin_class_fixes${i}.cpp" \
    || echo ">>> ОШИБКА чтения: spell_paladin_class_fixes${i}.cpp"
done

echo "=== [3/3] Контроль... ==="
N=$(grep -c "=== CUT HERE ===" "$F")
echo ">>> партий в файле: $N (нужно 10)"
if [ "$N" = "10" ]; then
  echo ">>> ШАГ 1 ВЫПОЛНЕН УСПЕШНО"
else
  echo ">>> ЧТО-ТО НЕ ТАК: партий $N. Верните файл и повторите:"
  echo "    git checkout -- $F"
fi
echo "--- последние 2 строки файла (должен быть ex10): ---"
tail -2 "$F"
