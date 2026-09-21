#pragma once
#include <QMainWindow>
#include <QVector>
#include <QHash>
#include <QSet>
#include "database_service.h"
#include "network_guard.h"
#include "db2_importer.h"
#include "ai_service.h"
#include "knowledge_base.h"
#include "project_context.h"
#include "schema_context.h"
#include "core_repository_service.h"
#include "jarvis_engine.h"
#include "wago_service.h"
#include "core_detect_service.h"
#include "server_launcher.h"
#include "sql_wizard_service.h"
#include "content_editor_service.h"
#include "qa_bot_service.h"
#include "qa_retail_source.h"
#include "qa_sniff_source.h"
#include "retail_reference_service.h"
#include "retail_community_reference_service.h"
class QLineEdit; class QSpinBox; class QComboBox; class QTextEdit; class QListWidget; class QPushButton; class QLabel; class QTableWidget; class QFormLayout; class QCheckBox; class QTreeWidget; class QTabWidget; class QWidget; class QGroupBox;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public: MainWindow();
private:
    DatabaseService m_db; NetworkGuard m_guard; Db2Importer m_importer; AiService m_ai; KnowledgeBase m_knowledge; CoreRepositoryService m_coreRepositories; JarvisEngine m_jarvis; WagoService m_wago; ServerLauncher m_server; QaRetailSource m_retail; RetailReferenceService m_retailReference; RetailCommunityReferenceService m_retailCommunity;
    QLineEdit *host,*user,*password,*db2Url,*db2Table,*exeSource,*exeOutput,*findText,*replaceText,*aiEndpoint,*aiModel,*aiKey,*wowheadUrl,*coreRepository,*corePatch,*knowledgeTitle,*knowledgeTags,*detectFolder;
    QLineEdit *worldPath,*realmPath,*raHost,*raUser,*raPassword,*serverCommand;
    QCheckBox *rememberKey,*offlineMode,*serverOwnConsole;
    QSpinBox *port,*raPort; QComboBox *core,*coreBranch,*dbSelector,*wagoBuild,*wagoTableName,*wagoLocalTable,*wagoHotfixDb,*wagoLocale;
    QListWidget *wagoTableList = nullptr;
    QLineEdit *wagoTableFilter = nullptr;
    QLabel *wagoHotfixInfo = nullptr;
    QString m_wagoTablesBuild;
    QTableWidget *wagoMapping; QLabel *wagoStatus,*wagoNameHint = nullptr,*detectStatus,*worldStateLabel,*realmStateLabel;
    QStringList m_localColumns; QHash<QString,QString> m_localTypes;
    QStringList m_scriptCols; QSet<int> m_scriptFloatCols; int m_scriptCommentCol = -1;
    QTextEdit *sql,*log,*aiTask,*aiScript,*aiError,*aiResponse,*wowheadContext,*knowledgeText,*worldLog,*realmLog,*commandHelp;
    QTreeWidget *commandTree;
    QLabel *commandFilterHint;
    // Scripting tab
    QComboBox *entityType; QLineEdit *entityEntry; QTableWidget *entityFields,*scriptTable;
    QLabel *entityStatus;
    QString m_projectContext;
    QString m_schemaContext;
    QStringList m_jarvisUrls;
    QString m_detectedWagoBuild;
    DbProfile m_profile;
    // Script grid key: entryorguid, source_type, id
    struct ScriptKey { qint64 entryorguid = 0; int sourceType = 0; int id = 0; };
    QVector<ScriptKey> m_scriptKeys;
    QListWidget *tables;
    QPushButton *connectButton;
    void addLog(const QString &s); void refreshTables(); void setOnline(bool ok,const QString &message);
    void connectWithProfile(const DbProfile &profile);
    void loadDatabases(const QString &selectPreferred);
    void loadEntity(); void loadScripts(qint64 entryorguid,int sourceType);
    void addScriptRow(); void deleteScriptRow(); void saveScriptRow();
    void askAiForEntity();
    void refreshScriptColumns();
    void loadJarvisSettings(); void saveJarvisSettings();
    void runOfflineJarvis();
    void fetchWagoBuilds(); void analyzeWagoTable();
    void updateWagoNameMatch(); void ensureWagoTableList();
    void fillWagoTablePicker(const QStringList &names);
    void filterWagoTableList();
    void applyWagoTableSelection(const QString &name);
    void loadWagoCache();
    void updateHotfixInfoLabel();
    void loadLocalColumns(); void buildMapping(); void importWagoMapped();
    void performWagoImport();
    bool m_wagoImportPending = false;
    void showWagoError(const QString &title, const QString &body);
    bool ensureHotfixSchema(QString *error);
    bool hotfixTableExists(const QString &table) const;
    bool createHotfixTableFromWago(QString *error);
    void fillWagoLocalTables(const QStringList &list);
    void selectHotfixDatabase(const QStringList &dbs);
    void refreshWagoHotfixTables();
    QString hotfixSchema() const;
    void runCoreDetection();
    void applyCoreDetection(const CoreDetection &detected);
    void selectDetectedWagoBuild(const QString &build);
    void chooseDetectFolder();
    void buildServerTab(QTabWidget *tabs);
    void fillCommandTree();
    void updateCoreBranding();
    void persistErrorLog(const QString &category, const QString &title, const QString &body);
    void appendLiveLog(QTextEdit *te, const QString &s, const QString &fileCategory);
    QGroupBox *serverWorldCard = nullptr;
    QGroupBox *serverRealmCard = nullptr;
    QLabel *serverLaunchHint = nullptr;
    QLabel *contentEditorHint = nullptr;
    QPushButton *pickRealmBtn = nullptr;
    // Wizards (SQL templates) tab
    QComboBox *wizTemplate = nullptr;
    QTextEdit *wizSource = nullptr, *wizOutput = nullptr;
    QWidget *wizFieldsHost = nullptr;
    QFormLayout *wizFieldsLayout = nullptr;
    QHash<QString, QLineEdit *> m_wizFields;
    QVector<SqlWizardService::Template> m_wizTemplates;
    void buildWizardsTab(QTabWidget *tabs, QWidget *scriptSection = nullptr);
    void wizLoadTemplate(int index);
    void wizGenerate();
    // Content editor (создание сущностей + диагностика связей)
    QComboBox *ceEntity = nullptr;
    QWidget *ceFieldsHost = nullptr;
    QFormLayout *ceFieldsLayout = nullptr;
    QHash<QString, QLineEdit *> m_ceFields;
    QLineEdit *ceDiagInput = nullptr;
    QTextEdit *ceReport = nullptr;
    // WDBX / патч клиента (DB2)
    QComboBox *wdbxTask = nullptr;
    QLineEdit *wdbxClientFolder = nullptr;
    QTextEdit *wdbxOut = nullptr;
    void generateWdbxInstructions();
    void ceLoadEntity(int index);
    void ceGenerate();
    void ceDiagnose(int mode); // 0 = квест, 1 = NPC
    // Console tab (универсальная консоль + пакетный SQL)
    QTextEdit *consoleInput = nullptr, *consoleOutput = nullptr;
    void buildConsoleTab(QTabWidget *tabs);
    // Вкладка «Патч WoW.exe» (метод Arctium Launcher)
    QLineEdit *wowExePath = nullptr, *wowPortal = nullptr,
              *wowVersionUrl = nullptr, *wowCdnsUrl = nullptr, *wowExtraArgs = nullptr,
              *wowCopyOutput = nullptr;
    QSpinBox *wowPort = nullptr;
    QSpinBox *wowWaitUnpackMs = nullptr;
    QCheckBox *wowExpandPortal = nullptr, *wowLegacyRsa = nullptr, *wowVersionUrls = nullptr,
              *wowCheckTls = nullptr, *wowAutoDetect = nullptr, *wowBypassCert = nullptr,
              *wowWriteConfigWtf = nullptr;
    QTextEdit *wowLog = nullptr;
    QLabel *wowDataInfo = nullptr;
    void buildWowPatchTab(QTabWidget *tabs);
    void wowChooseExe();
    void wowReadPortal();
    void wowRunTlsCheck();
    void wowPatchFile();
    void wowPatchMemory();
    bool m_wowPatchBusy = false;
    void buildQaTab(QTabWidget *tabs);
    QTextEdit *qaTask = nullptr, *qaReport = nullptr;
    QComboBox *qaPreset = nullptr;
    QLineEdit *qaSniffPath = nullptr, *qaReferenceBuild = nullptr;
    QComboBox *qaScopeType = nullptr; QLineEdit *qaScopeName = nullptr; QSpinBox *qaScopeMap = nullptr; QSpinBox *qaScopeZone = nullptr; QLabel *qaScopeStatus = nullptr;
    QTableWidget *qaFixesTable = nullptr;
    QVector<QaFix> m_qaFixes;
    void runQaBots();
    void finishQaRun(const QaRetailCatalog &catalog);
    void onRetailCatalogReady(const QaRetailCatalog &catalog);
    void applySelectedQaFixes();
    bool m_qaBusy = false;
    QaRetailCatalog m_pendingQaCatalog;
    bool m_qaReferenceBusy = false;
    bool m_qaReferenceForRun = false;
    bool m_qaCommunityBusy = false;
    bool m_qaCommunityForRun = false;
    void continueQaAfterCommunity(const QString &build);
    QString composeQaTask() const;
    void resolveQaScopeFromCache();
    void consoleRunSql();
    void consoleAppend(const QString &line);
    void showPerformanceDialog();
    qint64 m_startupMs = 0, m_lastDbMs = -1, m_memKb = -1;
    int m_tableCount = 0;
    void loadServerSettings();
    void saveServerSettings();
    void sendPickedCommand();
    void analyzeServerLogs();
    QString collectServerLogFiles() const;
    void reduceCoreMemory();
};
