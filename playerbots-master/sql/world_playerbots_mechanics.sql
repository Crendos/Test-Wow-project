-- playerbots_mechanics — СЛОЙ 2 QA: ручные знания «как должна работать способность/талант».
--
-- КТО ЧИТАЕТ: ТОЛЬКО QA-боты (гейт IsQAAccount в PlayerbotDummyLog::WriteQaReport).
-- Обычные игровые боты эти знания не читают и не применяют.
-- КОГДА ЧИТАЕТ: один раз при закрытии сессии с манекеном (.playerbots dummy stop / завершение),
--   проверка идёт по таймлайну сессии, результат — строки в <session>.qa.txt.
-- КТО ЗАПОЛНЯЕТ: ты — после починки класса: починил → добавил строки своего класса →
--   QA-боты этого класса начинают ловить его баги; класс без строк = INFO mechanics_coverage.
--
-- КОЛОНКИ:
--   class_id   — класс (2=паладин; те же id, что в playerbots_class_knowledge)
--   spell_id   — спелл-триггер (для power_cost — сам проверяемый спелл)
--   check_type — тип проверки:
--                  proc_after — после успешного каста spell_id в окне window_ms
--                               должен случиться cast/урон arg (прок/доступность)
--                  aura_after — после успешного каста spell_id в окне window_ms
--                               должна появиться аура arg (self или цель)
--                  power_cost — при касте spell_id реально списывается arg ед. ресурса
--   arg        — ожидаемый spell_id (proc_after/aura_after) либо сумма списания (power_cost)
--   window_ms  — окно проверки в мс (для power_cost не используется)
--   note       — текст для отчёта .qa.txt (зачем эта механика)
--
-- Применение (БД мира, x64 Native Tools или любой mysql-клиент):
--   mysql -h <host> -u <user> -p <world_db> < sql\world_playerbots_mechanics.sql
-- (файл идемпотентный: CREATE IF NOT EXISTS + INSERT IGNORE)

CREATE TABLE IF NOT EXISTS playerbots_mechanics (
    class_id   TINYINT UNSIGNED NOT NULL,
    spell_id   INT UNSIGNED      NOT NULL,
    check_type VARCHAR(24)       NOT NULL,
    arg        INT UNSIGNED      NOT NULL,
    window_ms  INT UNSIGNED      NOT NULL DEFAULT 5000,
    note       VARCHAR(255)      NOT NULL DEFAULT '',
    PRIMARY KEY (class_id, spell_id, check_type, arg)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- СИД: паладин (class_id=2), контент 12.1.0.69497 (Midnight).
-- Empyrean Hammer/Hammer of Light и пр. сверяй на wowhead при обновлении патчей.
INSERT IGNORE INTO playerbots_mechanics (class_id, spell_id, check_type, arg, window_ms, note) VALUES
-- Templar: Wake of Ashes (255937) открывает каст Hammer of Light (keystone-эффект дерева)
(2, 255937, 'proc_after', 429826, 20000, 'Templar: Wake of Ashes открывает Hammer of Light'),
-- Templar: цена keystone-спелла
(2, 429826, 'power_cost', 3, 0, 'Hammer of Light: цена 3 Holy Power'),
-- Herald of the Sun: DoT-ауры, которые обязаны висеть после каста
(2, 431413, 'aura_after', 431413, 6000, 'Sun Sear: дебафф на цели после каста'),
(2, 431377, 'aura_after', 431377, 6000, 'Dawnlight: аура на цели после каста'),
-- Herald of the Sun: аура от Shake the Heavens
(2, 431533, 'aura_after', 431533, 8000, 'Shake the Heavens: аура после каста');
