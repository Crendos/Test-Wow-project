-- ============================================================
-- playerbots: чистка «хвостов» после РУЧНОГО удаления персонажей/аккаунтов
-- ------------------------------------------------------------
-- Проблема: персонаж удалён только из таблицы `characters`, а связанные
-- строки (character_skills, character_spell, character_action, …) остались.
-- TC при следующем create может выдать ТОТ ЖЕ guid (max+1) →
-- [ERROR] [1062] Duplicate entry '…-…' → транзакция откатывается,
-- «персонаж НЕ закоммитился в characters».
--
-- Использование (Navicat → нужная БД → Новый запрос):
--   Шаг 1: выполнить блок-генератор;
--   Шаг 2: скопировать из результата единственную колонку `stmt`
--          (это уже готовые DELETE-ы);
--   Шаг 3: вставить в новое окно и выполнить;
--   Шаг 4: перезапустить worldserver (генератор guid перечитает max),
--          повторить create.
-- ============================================================

-- ------------------------------------------------------------
-- БД characters. Сироты: guid есть в связанных таблицах, но НЕТ в `characters`.
-- ВНИМАНИЕ: удалённые строки вернуть нельзя — выполняй на своих данных осознанно.
-- ------------------------------------------------------------
SET SESSION group_concat_max_len = 1000000;

SELECT GROUP_CONCAT(
         CONCAT('DELETE t FROM `', TABLE_NAME,
                '` t LEFT JOIN characters c ON c.guid = t.guid WHERE c.guid IS NULL;')
         ORDER BY TABLE_NAME SEPARATOR '\n') AS stmt
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = DATABASE()
  AND COLUMN_NAME = 'guid'
  AND TABLE_NAME <> 'characters';
-- Если результат пуст (NULL) — сирот в characters нет, чистить нечего.

-- Проверка после выполнения stmt: генератор должен вернуть NULL.
-- SELECT COUNT(*) FROM characters;   -- живые персонажи
-- SELECT guid, name, account FROM characters;

-- ------------------------------------------------------------
-- БД auth. Сироты аккаунтов в таблицах с колонкой account/account_id
-- (account_data, realmlist-related и пр.). Живые бот-аккаунты: 9000..9599.
-- ------------------------------------------------------------
SET SESSION group_concat_max_len = 1000000;

SELECT GROUP_CONCAT(
         CONCAT('DELETE t FROM `', TABLE_NAME,
                '` t LEFT JOIN account a ON a.id = t.`', COLUMN_NAME,
                '` WHERE a.id IS NULL;')
         ORDER BY TABLE_NAME SEPARATOR '\n') AS stmt
FROM information_schema.COLUMNS
WHERE TABLE_SCHEMA = DATABASE()
  AND COLUMN_NAME IN ('account', 'account_id')
  AND TABLE_NAME <> 'account';
-- Скопировать `stmt` → выполнить.

-- Живые бот-аккаунты (после удаления вчерашних должно остаться только нужное):
-- SELECT id, username FROM account WHERE id BETWEEN 9000 AND 9599;

-- ------------------------------------------------------------
-- БД auth. Фейковые battlenet-аккаунты ботов (почта *@playerbots.local),
-- чей game-аккаунт удалён. Строки-дети (battlenet_item_appearances и пр.)
-- после этого можно чистить тем же генератором по колонке battlenetAccountId:
-- ------------------------------------------------------------
DELETE b FROM battlenet_accounts b
LEFT JOIN account a ON a.id = b.id
WHERE a.id IS NULL
  AND b.email LIKE '%@playerbots.local';
