#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

class DatabaseService;

// Одно поле сущности (колонка в таблице БД).
struct ContentEntityField {
    QString column;       // имя колонки, например "entry"
    QString label;        // подпись в форме, например "Entry / ID"
    QString placeholder;  // подсказка значения
    bool required = false;
};

// Описание создаваемой сущности: таблица + набор полей.
struct ContentEntitySpec {
    QString key;          // "item" / "creature" / "gameobject" / "quest" / "loot"
    QString title;        // "Предмет"
    QString table;        // "item_template"
    QVector<ContentEntityField> fields;
};

// Сервис «Редактора контента»: знает типовые сущности ядра, читает ЖИВУЮ схему MySQL,
// проверяет совместимость таблиц/колонок, генерирует INSERT и диагностирует связи
// (квест ↔ NPC ↔ лут) и триггеры (smart_scripts). Всё работает против подключённой БД.
class ContentEditorService {
public:
    // Типовые сущности (предмет, NPC, объект, квест, лут) под TrinityCore-линию.
    static QVector<ContentEntitySpec> specs();
    static const ContentEntitySpec *specByKey(const QString &key);

    // --- Живая схема ---
    static bool tableExists(DatabaseService &db, const QString &table);
    static QStringList tableColumns(DatabaseService &db, const QString &table, bool *ok = nullptr);
    // Таблицы, похожие по имени (если нужной нет — предложить совместимую).
    static QStringList similarTables(DatabaseService &db, const QString &table);

    // --- Генерация INSERT с валидацией по живой схеме ---
    // db может быть nullptr (тогда без валидации). *report — человекочитаемые предупреждения.
    static QString buildInsert(const ContentEntitySpec &spec, const QMap<QString, QString> &values,
                               DatabaseService *db, QString *report);

    // --- Диагностика связей и триггеров ---
    static QString diagnoseQuest(DatabaseService &db, int questId);
    static QString diagnoseNpc(DatabaseService &db, int entry);
};
