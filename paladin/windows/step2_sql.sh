#!/usr/bin/env bash
# ============================================================================
# Паладин-фиксы — ШАГ 2: импорт 10 SQL в world-базу с отчётом по каждому.
# ЗАПУСК (одна строка; подставьте свои ПОЛЬЗОВАТЕЛЬ/ПАРОЛЬ/БАЗУ):
#   bash <(tr -d '\r' < "C:/paladin-fixes/paladin/windows/step2_sql.sh") root МОЙПАРОЛЬ acore_world
# Если папка фиксов в другом месте — добавьте её 4-м аргументом.
# Требуется, чтобы mysql.exe был доступен в PATH (или допишите полный путь).
# ============================================================================
set -u
U="${1:?использование: step2_sql.sh ПОЛЬЗОВАТЕЛЬ ПАРОЛЬ БАЗА [путь_к_paladin]}"
P="${2:?использование: step2_sql.sh ПОЛЬЗОВАТЕЛЬ ПАРОЛЬ БАЗА [путь_к_paladin]}"
DB="${3:?использование: step2_sql.sh ПОЛЬЗОВАТЕЛЬ ПАРОЛЬ БАЗА [путь_к_paladin]}"
FIX="${4:-C:/paladin-fixes/paladin}"

command -v mysql >/dev/null || { echo ">>> ОШИБКА: mysql не найден в PATH (можно указать полный путь в скрипте)"; exit 1; }
[ -d "$FIX" ] || { echo ">>> ОШИБКА: нет папки $FIX"; exit 1; }

D=0; F=0
for i in "" _2 _3 _4 _5 _6 _7 _8 _9 _10; do
  printf -- "--- импортирую paladin_class_fixes%s.sql ... " "${i:-1}"
  if mysql -u "$U" -p"$P" "$DB" < "$FIX/paladin_class_fixes${i}.sql" 2>/dev/null; then
    echo "OK"; D=$((D+1))
  else
    echo "ОШИБКА"; F=$((F+1))
  fi
done
echo "=== ИТОГ: успешно $D из 10, с ошибками $F ==="
[ "$F" = "0" ] && echo ">>> ШАГ 2 ВЫПОЛНЕН УСПЕШНО"
[ "$D" != "10" ] && echo ">>> файлы с ошибками прогоните повторно и пришлите текст ошибки без 2>/dev/null"

echo "=== Контроль содержимого базы ==="
mysql -u "$U" -p"$P" "$DB" -e "
SELECT CONCAT('1) бинды скриптов: ', COUNT(*), ' (нужно >50)') AS PROVERKA FROM spell_script_names WHERE ScriptName LIKE 'spell_pal%'
UNION ALL SELECT CONCAT('2) AT 6006: ', COUNT(*), ' (нужно 1)') FROM areatrigger_create_properties WHERE Id=6006 AND IsCustom=0
UNION ALL SELECT CONCAT('3) точки спирали: ', COUNT(*), ' (49=наша; 0=TDB)') FROM areatrigger_create_properties_spline_point WHERE AreaTriggerCreatePropertiesId=6006 AND IsCustom=0
UNION ALL SELECT CONCAT('4) проц-строки: ', COUNT(*), ' (нужно 2)') FROM spell_proc WHERE SpellId IN (157047,431474);"
