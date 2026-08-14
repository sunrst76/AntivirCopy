#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QDateTime>

// ==================== РЕАЛИЗАЦИЯ CopyWorker ====================

CopyWorker::CopyWorker(int workerId, const QString &src, const QString &dst)
    : m_id(workerId), m_src(src), m_dst(dst) {}

int CopyWorker::countFiles(const QString &dirPath) {
    int count = 0;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        count++;
    }
    return count;
}

void CopyWorker::process() {
    QDir srcDir(m_src);
    QDir dstDir(m_dst);
    int totalFiles = countFiles(m_src);

    if (totalFiles == 0) {
        dstDir.mkpath(m_dst);
        emit progressChanged(m_id, 100);
        emit finished(m_id, true);
        return;
    }

    if (!dstDir.exists() && !dstDir.mkpath(m_dst)) {
        emit finished(m_id, false);
        return;
    }

    int copiedFiles = 0;
    QDirIterator it(m_src, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        it.next();
        QFileInfo srcFileInfo = it.fileInfo();
        QString relativePath = srcDir.relativeFilePath(srcFileInfo.absoluteFilePath());
        QString destPath = dstDir.filePath(relativePath);

        if (srcFileInfo.isDir()) {
            QDir subDir;
            if (!subDir.mkpath(destPath)) {
                emit finished(m_id, false);
                return;
            }
        } else {
            if (QFile::exists(destPath)) {
                QFileInfo destFileInfo(destPath);
                if (srcFileInfo.lastModified() <= destFileInfo.lastModified() && srcFileInfo.size() == destFileInfo.size()) {
                    copiedFiles++;
                    int progress = static_cast<int>((static_cast<double>(copiedFiles) / totalFiles) * 100);
                    emit progressChanged(m_id, progress);
                    continue;
                }
                if (!QFile::remove(destPath)) {
                    emit finished(m_id, false);
                    return;
                }
            }

            if (QFile::copy(srcFileInfo.absoluteFilePath(), destPath)) {
                copiedFiles++;
                int progress = static_cast<int>((static_cast<double>(copiedFiles) / totalFiles) * 100);
                emit progressChanged(m_id, progress);
            } else {
                emit finished(m_id, false);
                return;
            }
        }
    }

    emit progressChanged(m_id, 100);
    emit finished(m_id, true);
}


// ==================== РЕАЛИЗАЦИЯ MainWindow ====================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // СДЕЛАТЬ ОКНО ФИКСИРОВАННОГО РАЗМЕРА:
    // Берутся текущие ширина и высота, заданные в Qt Designer, и намертво закрепляются
    this->setFixedSize(this->size());

    // Сброс указателей потоков в безопасное состояние
    for (int i = 0; i < 3; ++i) {
        m_threads[i] = nullptr;
        m_workers[i] = nullptr;
    }

    // Инициализация шкал прогресса
    ui->progressBar_1->setValue(0);
    ui->progressBar_2->setValue(0);
    ui->progressBar_3->setValue(0);

    // --- Сигналы кнопок Блока №1 ---
    connect(ui->btnSelectSource_1, &QPushButton::clicked, this, &MainWindow::onSelectSourceClicked);
    connect(ui->btnSelectDest_1, &QPushButton::clicked, this, &MainWindow::onSelectDestClicked);
    connect(ui->btnStartCopy_1, &QPushButton::clicked, this, &MainWindow::onStartCopyClicked);

    // --- Сигналы кнопок Блока №2 ---
    connect(ui->btnSelectSource_2, &QPushButton::clicked, this, &MainWindow::onSelectSourceClicked);
    connect(ui->btnSelectDest_2, &QPushButton::clicked, this, &MainWindow::onSelectDestClicked);
    connect(ui->btnStartCopy_2, &QPushButton::clicked, this, &MainWindow::onStartCopyClicked);

    // --- Сигналы кнопок Блока №3 ---
    connect(ui->btnSelectSource_3, &QPushButton::clicked, this, &MainWindow::onSelectSourceClicked);
    connect(ui->btnSelectDest_3, &QPushButton::clicked, this, &MainWindow::onSelectDestClicked);
    connect(ui->btnStartCopy_3, &QPushButton::clicked, this, &MainWindow::onStartCopyClicked);

    // --- Связывание полей ручного ввода ---
    connect(ui->lineEditSource_1, &QLineEdit::textChanged, this, &MainWindow::onSourceTextChanged);
    connect(ui->lineEditDest_1, &QLineEdit::textChanged, this, &MainWindow::onDestTextChanged);
    connect(ui->lineEditSource_2, &QLineEdit::textChanged, this, &MainWindow::onSourceTextChanged);
    connect(ui->lineEditDest_2, &QLineEdit::textChanged, this, &MainWindow::onDestTextChanged);
    connect(ui->lineEditSource_3, &QLineEdit::textChanged, this, &MainWindow::onSourceTextChanged);
    connect(ui->lineEditDest_3, &QLineEdit::textChanged, this, &MainWindow::onDestTextChanged);

    // Автоматическая загрузка сохраненных данных
    loadSettings();
}

MainWindow::~MainWindow() {
    // Безопасная деструкция работающих потоков в фоне
    for (int i = 0; i < 3; ++i) {
        if (m_threads[i] && m_threads[i]->isRunning()) {
            m_threads[i]->quit();
            m_threads[i]->wait();
        }
    }
    delete ui;
}

void MainWindow::loadSettings() {
    QSettings settings("MyCompany", "FolderSyncApp");

    for (int i = 0; i < 3; ++i) {
        m_sourcePaths[i] = settings.value(QString("Block_%1/Source").arg(i)).toString();
        m_destPaths[i] = settings.value(QString("Block_%1/Dest").arg(i)).toString();
    }

    // ИСПРАВЛЕНО: Добавлены индексы, [1], [2] для массивов
    ui->lineEditSource_1->setText(m_sourcePaths[0]);
    ui->lineEditDest_1->setText(m_destPaths[0]);

    ui->lineEditSource_2->setText(m_sourcePaths[1]);
    ui->lineEditDest_2->setText(m_destPaths[1]);

    ui->lineEditSource_3->setText(m_sourcePaths[2]);
    ui->lineEditDest_3->setText(m_destPaths[2]);
}


// Слот для обработки прямого ручного набора в поле Source
void MainWindow::onSourceTextChanged(const QString &text) {
    QLineEdit *lineEdit = qobject_cast<QLineEdit*>(sender());
    if (!lineEdit) return;

    int idx = lineEdit->objectName().endsWith("_1") ? 0 : (lineEdit->objectName().endsWith("_2") ? 1 : 2);
    m_sourcePaths[idx] = text.trimmed();

    QSettings settings("MyCompany", "FolderSyncApp");
    settings.setValue(QString("Block_%1/Source").arg(idx), m_sourcePaths[idx]);
}

// Слот для обработки прямого ручного набора в поле Dest
void MainWindow::onDestTextChanged(const QString &text) {
    QLineEdit *lineEdit = qobject_cast<QLineEdit*>(sender());
    if (!lineEdit) return;

    int idx = lineEdit->objectName().endsWith("_1") ? 0 : (lineEdit->objectName().endsWith("_2") ? 1 : 2);
    m_destPaths[idx] = text.trimmed();

    QSettings settings("MyCompany", "FolderSyncApp");
    settings.setValue(QString("Block_%1/Dest").arg(idx), m_destPaths[idx]);
}

void MainWindow::onSelectSourceClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    int idx = btn->objectName().endsWith("_1") ? 0 : (btn->objectName().endsWith("_2") ? 1 : 2);

    QString path = QFileDialog::getExistingDirectory(this, "Выберите исходную папку", m_sourcePaths[idx]);
    if (!path.isEmpty()) {
        // Установка текста вызовет сигнал textChanged() и автоматически сохранит данные
        if (idx == 0) ui->lineEditSource_1->setText(path);
        else if (idx == 1) ui->lineEditSource_2->setText(path);
        else ui->lineEditSource_3->setText(path);
    }
}

void MainWindow::onSelectDestClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    int idx = btn->objectName().endsWith("_1") ? 0 : (btn->objectName().endsWith("_2") ? 1 : 2);

    QString path = QFileDialog::getExistingDirectory(this, "Выберите целевую папку", m_destPaths[idx]);
    if (!path.isEmpty()) {
        if (idx == 0) ui->lineEditDest_1->setText(path);
        else if (idx == 1) ui->lineEditDest_2->setText(path);
        else ui->lineEditDest_3->setText(path);
    }
}

void MainWindow::onStartCopyClicked() {
    QPushButton *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    int idx = btn->objectName().endsWith("_1") ? 0 : (btn->objectName().endsWith("_2") ? 1 : 2);

    if (m_sourcePaths[idx].isEmpty() || m_destPaths[idx].isEmpty()) {
        QMessageBox::warning(this, "Ошибка", QString("Блок %1: Заполните оба пути!").arg(idx + 1));
        return;
    }
    if (m_sourcePaths[idx] == m_destPaths[idx]) {
        QMessageBox::warning(this, "Ошибка", QString("Блок %1: Пути источника и назначения совпадают!").arg(idx + 1));
        return;
    }

    startBlockCopy(idx);
}

void MainWindow::startBlockCopy(int blockIdx) {
    // Визуальная блокировка элементов интерфейса конкретного блока
    if (blockIdx == 0) { ui->btnStartCopy_1->setEnabled(false); ui->progressBar_1->setValue(0); }
    else if (blockIdx == 1) { ui->btnStartCopy_2->setEnabled(false); ui->progressBar_2->setValue(0); }
    else { ui->btnStartCopy_3->setEnabled(false); ui->progressBar_3->setValue(0); }

    m_threads[blockIdx] = new QThread();
    m_workers[blockIdx] = new CopyWorker(blockIdx, m_sourcePaths[blockIdx], m_destPaths[blockIdx]);
    m_workers[blockIdx]->moveToThread(m_threads[blockIdx]);

    connect(m_threads[blockIdx], &QThread::started, m_workers[blockIdx], &CopyWorker::process);
    connect(m_workers[blockIdx], &CopyWorker::progressChanged, this, &MainWindow::onCopyProgress);
    connect(m_workers[blockIdx], &CopyWorker::finished, this, &MainWindow::onCopyFinished);

    // Освобождение памяти
    connect(m_workers[blockIdx], &CopyWorker::finished, m_threads[blockIdx], &QThread::quit);
    connect(m_workers[blockIdx], &QObject::destroyed, m_workers[blockIdx], &QObject::deleteLater);
    connect(m_threads[blockIdx], &QThread::finished, m_threads[blockIdx], &QThread::deleteLater);

    m_threads[blockIdx]->start();
}

void MainWindow::onCopyProgress(int workerId, int percent) {
    if (workerId == 0) {
        ui->progressBar_1->setValue(percent);
    } else if (workerId == 1) {
        ui->progressBar_2->setValue(percent);
    } else if (workerId == 2) {
        ui->progressBar_3->setValue(percent);
    }
}


void MainWindow::onCopyFinished(int workerId, bool success) {
    if (workerId == 0) ui->btnStartCopy_1->setEnabled(true);
    else if (workerId == 1) ui->btnStartCopy_2->setEnabled(true);
    else ui->btnStartCopy_3->setEnabled(true);

    if (success) {
        QMessageBox::information(this, "Успех", QString("Синхронизация Блока %1 завершена.").arg(workerId + 1));
    } else {
        QMessageBox::critical(this, "Ошибка", QString("В Блоке %1 произошел сбой.").arg(workerId + 1));
    }

    m_threads[workerId] = nullptr;
    m_workers[workerId] = nullptr;
}

