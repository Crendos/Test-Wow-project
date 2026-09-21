#include "core_repository_service.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
CoreRepositoryService::CoreRepositoryService(QObject *parent):QObject(parent){}
CoreRepository CoreRepositoryService::trinity(){return {"TrinityCore","https://github.com/TrinityCore/TrinityCore.git","https://api.github.com/repos/TrinityCore/TrinityCore/branches?per_page=100","https://raw.githubusercontent.com/TrinityCore/TrinityCore/master/README.md","C++"};}
CoreRepository CoreRepositoryService::cypher(){return {"CypherCore","https://github.com/CypherCore/CypherCore.git","https://api.github.com/repos/CypherCore/CypherCore/branches?per_page=100","https://raw.githubusercontent.com/CypherCore/CypherCore/master/README.md","C#"};}
CoreRepository CoreRepositoryService::nordrassil(){return {"Nordrassil Core","https://github.com/Legends-of-Azeroth/legion-Nordrassil-core.git","https://api.github.com/repos/Legends-of-Azeroth/legion-Nordrassil-core/branches?per_page=100","https://raw.githubusercontent.com/Legends-of-Azeroth/legion-Nordrassil-core/master/README.md","C++"};}
CoreRepository CoreRepositoryService::forProfile(const QString &profile){
    if (profile.startsWith(QLatin1String("CypherCore"))) return cypher();
    if (profile.contains(QLatin1String("Nordrassil"), Qt::CaseInsensitive)) return nordrassil();
    return trinity();
}
void CoreRepositoryService::fetchBranches(const CoreRepository &repo){QNetworkRequest req{QUrl(repo.apiUrl)};req.setRawHeader("Accept","application/vnd.github+json");req.setRawHeader("User-Agent","WoW-DB-Studio/0.1");req.setTransferTimeout(15000);auto *r=m_network.get(req);connect(r,&QNetworkReply::finished,this,[this,r]{const auto bytes=r->readAll();if(r->error()!=QNetworkReply::NoError){emit failed("GitHub: "+r->errorString());r->deleteLater();return;}const auto arr=QJsonDocument::fromJson(bytes).array();QStringList result;for(const auto &v:arr){const auto n=v.toObject().value("name").toString();if(!n.isEmpty())result<<n;}if(result.isEmpty())emit failed("GitHub не вернул ветви или ограничил запрос.");else emit branchesLoaded(result);r->deleteLater();});}

void CoreRepositoryService::fetchPatchInfo(const CoreRepository &repo) {
    QNetworkRequest req{QUrl(repo.readmeUrl)};req.setRawHeader("User-Agent","WoW-DB-Studio/0.1");req.setTransferTimeout(15000);auto *r=m_network.get(req);connect(r,&QNetworkReply::finished,this,[this,r,repo]{const auto body=QString::fromUtf8(r->readAll());if(r->error()!=QNetworkReply::NoError){emit failed("GitHub README: "+r->errorString());r->deleteLater();return;}QString summary;QRegularExpression tc("master\\s*=\\s*([0-9]+(?:\\.[0-9]+){1,3})",QRegularExpression::CaseInsensitiveOption);
    QRegularExpression cy("(?:current support game version is|supported game version)\\s*:?\\s*([0-9]+(?:\\.[0-9]+){1,3})",QRegularExpression::CaseInsensitiveOption);
    QRegularExpression nr("Version\\s+([0-9]+(?:\\.[0-9]+){1,3})",QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m;
    if (repo.displayName.contains(QLatin1String("Nordrassil"))) m = nr.match(body);
    else if (repo.displayName == QLatin1String("TrinityCore")) m = tc.match(body);
    else m = cy.match(body);
    if (!m.hasMatch()) m = nr.match(body);
    summary=m.hasMatch()?"Актуальная версия по README: "+m.captured(1):"README загружен, версия не распознана автоматически.";
    if (repo.displayName.contains(QLatin1String("Nordrassil")) && summary.contains("7.3.5"))
        summary += " · Legion Nordrassil (fork TrinityCore 7.3.5)";emit patchInfoLoaded(summary);r->deleteLater();});
}
