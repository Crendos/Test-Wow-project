#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>

// Универсальный мастер SQL-шаблонов.
// Шаблон — это текст SQL с плейсхолдерами вида @TOKEN (например @QUEST, @ITEM, @MAP).
// Сервис находит уникальные плейсхолдеры и подставляет вместо них значения.
// Значения подставляются «как есть» (raw), поэтому числовые ID вставляются числами,
// а строковые литералы в шаблонах уже обрамлены кавычками.
class SqlWizardService {
public:
    struct Template { QString name; QString sql; };

    // Небольшой набор встроенных коротких шаблонов (полные — загрузкой из файла).
    static QVector<Template> builtin();

    // Загрузка шаблона из .sql файла.
    static bool loadFile(const QString &path, QString *sql, QString *error);

    // Уникальные плейсхолдеры (@TOKEN -> TOKEN) в порядке появления.
    static QStringList placeholders(const QString &sql);

    // Подстановка: keys — имена токенов БЕЗ '@'.
    static QString generate(const QString &sql, const QMap<QString, QString> &values);
};
