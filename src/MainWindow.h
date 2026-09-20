#pragma once

#include "ModelTypes.h"
#include "PresetLibrary.h"

#include <QMainWindow>
#include <QMap>
#include <QStringList>
#include <QVector>

QT_BEGIN_NAMESPACE
class QThread;
class QComboBox;
class QTreeWidgetItem;
QT_END_NAMESPACE

class SliceWorker;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void importModels();
    void importStepAssembly();
    void duplicateSelectedModel();
    void removeSelectedModel();
    void selectedModelChanged();
    void selectedModelEdited();
    void materialCountChanged();
    void machineChanged();
    void browseOutputDir();
    void importConfigYaml();
    void runWorkflow();
    void cancelWorkflow();
    void workerProgress(int current, int total, const QString& stage);
    void workerFinished(bool success, const QString& summary, const QString& outputDir);

private:
    int selectedModelIndex() const;
    int modelIndexById(const QString& id) const;
    QString selectedAssemblyId() const;
    bool selectedItemIsAssembly() const;
    SliceSettings collectSettings() const;
    int importModelFiles(const QStringList& paths);
    int importStepAssemblyFile(const QString& path);
    void updateMaterialCombo();
    void rebuildMaterialTable();
    void applyMaterialPresetToRow(int row);
    void populateMaterialPresetCombo(QComboBox* combo) const;
    void initializeAdvancedParamTable();
    QMap<QString, QString> collectAdvancedParamsFromTable() const;
    void applyAdvancedParamsToTable(const QMap<QString, QString>& values);
    bool applyConfigYamlFile(const QString& path, QString* errorMessage);
    void refreshModelList();
    void setModelTreeCurrentModel(int modelIndex);
    void setModelTreeCurrentAssembly(const QString& assemblyId);
    void loadSelectedModelToControls();
    void setTransformEditorsEnabled(bool enabled);
    void refreshAssemblyTransforms();
    void refreshAssemblyTransformsRecursive(const QString& assemblyId, const QMatrix4x4& parentMatrix);
    void removeAssembly(const QString& assemblyId);
    void appendLog(const QString& text);
    void setRunningState(bool running);
    QColor materialColor(int materialIndex) const;
    QColor materialColorForSlot(int slot) const;
    QString defaultMergeToolPath() const;
    QString defaultStepToolPath() const;
    QString defaultPythonPath() const;

    // Preset library
    bool loadPresetLibrary();
    bool promptForConfig();
    QStringList candidateConfigPaths() const;
    void populateMachineCombo();
    const MachinePreset* currentMachine() const;
    void applyMachineToUi(const MachinePreset& machine);
    void setConfigDependentEnabled(bool enabled);

    void runSelfTest();

    Ui::MainWindow* ui = nullptr;
    QVector<ModelInstance> m_models;
    QMap<QString, Transform> m_assemblyTransforms;
    QMap<QString, QString> m_assemblyNames;
    QStringList m_assemblyOrder;
    QMap<QString, QStringList> m_assemblyChildren;
    PresetLibrary m_presets;
    bool m_configReady = false;
    bool m_loadingSelection = false;
    bool m_rebuildingTable = false;
    bool m_rebuildingModelTree = false;
    quint64 m_nextAssemblyId = 1;
    QThread* m_workerThread = nullptr;
    SliceWorker* m_worker = nullptr;
    bool m_selfTest = false;
    quint64 m_nextModelId = 1;
    QString m_configPathOverride;
};
