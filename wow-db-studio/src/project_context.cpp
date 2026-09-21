#include "project_context.h"
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

ProjectContextResult ProjectContext::loadFolder(const QString &folder,int maxFiles,qsizetype maxChars) {
    ProjectContextResult out; const QSet<QString> ext={"cpp","cxx","cc","c","h","hpp","hh","sql","conf","ini","json","txt","md","cmake"};
    QDirIterator it(folder,QDir::Files,QDirIterator::Subdirectories); const auto root=QFileInfo(folder).absoluteFilePath();
    while(it.hasNext()) { const auto path=it.next();const QFileInfo info(path);if(info.size()>512*1024)continue;const auto name=info.fileName();if(!ext.contains(info.suffix().toLower()) && name!="CMakeLists.txt")continue;
        QFile f(path);if(!f.open(QIODevice::ReadOnly|QIODevice::Text))continue;auto text=QString::fromUtf8(f.readAll());if(text.contains(QChar::ReplacementCharacter))continue;
        const QString rel=QDir(root).relativeFilePath(path);const QString block="\n===== ФАЙЛ: "+rel+" =====\n"+text+"\n";
        if(out.files>=maxFiles || out.content.size()+block.size()>maxChars){out.truncated=true;break;}out.content+=block;++out.files;
    } return out;
}
