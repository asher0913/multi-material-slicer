#pragma once

#include "ModelTypes.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>

class QProcess;

// Runs the whole export pipeline (STL slicing -> config.yaml -> backend merge)
// on a background thread so the UI stays responsive. Construct it with copies of
// the data it needs, move it to a QThread, and connect to its signals.
class SliceWorker : public QObject {
    Q_OBJECT

public:
    SliceWorker(QVector<ModelInstance> models, SliceSettings settings, QObject* parent = nullptr);
    void requestCancel();

public slots:
    void process();

signals:
    void logMessage(const QString& text);
    void progressChanged(int current, int total, const QString& stage);
    // success == true means the full pipeline (including the backend) completed with exit code 0.
    void finished(bool success, const QString& summary, const QString& outputDir);

private:
    QVector<ModelInstance> m_models;
    SliceSettings m_settings;
    std::atomic_bool m_cancelRequested { false };
};
