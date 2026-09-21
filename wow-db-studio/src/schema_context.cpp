#include "schema_context.h"
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>

static bool safeIdentifier(const QString &s) { for(auto c:s) if(!c.isLetterOrNumber() && c!='_') return false; return !s.isEmpty(); }
static QStringList words(const QString &text) { return text.toLower().split(QRegularExpression("[^\\p{L}\\p{N}_]+"),Qt::SkipEmptyParts); }
QString SchemaContext::collect(const QSqlDatabase &db,const QString &task,QString *error) {
    if(!db.isOpen()){if(error)*error="Нет активного подключения к MySQL.";return {};}
    QSqlQuery list(db);if(!list.exec("SELECT table_name, COALESCE(table_comment,'') FROM information_schema.tables WHERE table_schema = DATABASE() AND table_type = 'BASE TABLE' ORDER BY table_name")){if(error)*error=list.lastError().text();return {};}
    struct Table { QString n,c;int score; };QVector<Table> tables;const auto terms=words(task);const QSet<QString> priority={"creature_template","creature","gameobject_template","gameobject","item_template","quest_template","smart_scripts","conditions","spell_template","instance_template","areatrigger_template","access_requirement","npc_text","page_text","gossip_menu","creature_loot_template"};
    QStringList names;while(list.next()){auto n=list.value(0).toString();int score=priority.contains(n)?3:0;const auto hay=(n+" "+list.value(1).toString()).toLower();for(const auto &term:terms)if(term.size()>2&&hay.contains(term))score+=2;tables.append({n,list.value(1).toString(),score});names<<n;}
    std::sort(tables.begin(),tables.end(),[](const Table&a,const Table&b){return a.score!=b.score?a.score>b.score:a.n<b.n;});
    QString out="MySQL schema snapshot (read-only). Database: "+db.databaseName()+"\nAll tables ("+QString::number(names.size())+"): "+names.join(", ").left(12000)+"\n\nRelevant CREATE TABLE definitions:\n";
    int selected=0;for(const auto &t:tables){if(selected>=12 || out.size()>=80000)break;if(t.score<=0&&selected>=8)break;if(!safeIdentifier(t.n))continue;QSqlQuery create(db);if(!create.exec("SHOW CREATE TABLE `"+t.n+"`"))continue;if(create.next()){out+="\n--- "+t.n;if(!t.c.isEmpty())out+=" — "+t.c;out+=" ---\n"+create.value(1).toString().left(5500)+"\n";++selected;}}
    if(selected==0)out+="No CREATE TABLE definitions could be read.\n";return out;
}
