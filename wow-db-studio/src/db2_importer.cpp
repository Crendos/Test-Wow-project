#include "db2_importer.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

Db2Importer::Db2Importer(QObject *parent) : QObject(parent) {}
void Db2Importer::importUrl(const QUrl &url, const QString &table, QSqlDatabase db) {
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http")) { emit finished(false, tr("Укажите URL http/https.")); return; }
    if (!db.isOpen()) { emit finished(false, tr("Сначала подключитесь к MySQL.")); return; }
    QNetworkRequest request(url); request.setTransferTimeout(30000);
    auto *reply=m_network.get(request);
    connect(reply,&QNetworkReply::finished,this,[this,reply,table,db] {
        if(reply->error()!=QNetworkReply::NoError){ emit finished(false,reply->errorString()); reply->deleteLater(); return; }
        QByteArray bytes=reply->readAll(); QString e;
        const bool ok=bytes.trimmed().startsWith('{') || bytes.trimmed().startsWith('[')
            ? importJson(bytes,table,db,&e) : importCsv(bytes,table,db,&e);
        emit finished(ok, ok ? tr("Импорт завершён.") : e); reply->deleteLater();
    });
}
static bool safeTable(const QString &s) { for(QChar c:s) if(!c.isLetterOrNumber() && c!='_') return false; return !s.isEmpty(); }
bool Db2Importer::importJson(const QByteArray &data,const QString &table,QSqlDatabase db,QString *err) {
    if(!safeTable(table)){*err=tr("Недопустимое имя таблицы.");return false;}
    QJsonParseError pe; auto doc=QJsonDocument::fromJson(data,&pe); if(pe.error!=QJsonParseError::NoError){*err=pe.errorString();return false;}
    QJsonArray rows=doc.isArray()?doc.array():doc.object().value("rows").toArray(); if(rows.isEmpty()){*err=tr("Массив rows отсутствует или пуст.");return false;}
    auto first=rows.first().toObject(); QStringList cols; for(auto it=first.begin();it!=first.end();++it) if(safeTable(it.key())) cols<<it.key();
    if(cols.isEmpty()){*err=tr("Нет допустимых полей.");return false;}
    QStringList marks; for(int i=0;i<cols.size();++i) marks<<"?";
    QSqlQuery q(db); q.prepare(QString("INSERT INTO `%1` (`%2`) VALUES (%3)").arg(table,cols.join("`,`"),marks.join(',')));
    if(!db.transaction()){*err=db.lastError().text();return false;}
    for(const auto &v:rows){auto o=v.toObject();q.clear();q.prepare(QString("INSERT INTO `%1` (`%2`) VALUES (%3)").arg(table,cols.join("`,`"),marks.join(','))); for(auto &c:cols)q.addBindValue(o.value(c).toVariant());if(!q.exec()){db.rollback();*err=q.lastError().text();return false;}}
    if(!db.commit()){*err=db.lastError().text();return false;} return true;
}
bool Db2Importer::importCsv(const QByteArray &,const QString &,QSqlDatabase,QString *err) { *err=tr("CSV требует сопоставления колонок; используйте JSON export или реализуйте mapping в следующей версии."); return false; }
