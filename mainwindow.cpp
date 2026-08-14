#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QDateTime>
#include <QDebug>
#include <QCoreApplication>

// ==================== РЕАЛИЗАЦИЯ CopyWorker ====================
// (Код CopyWorker остается без изменений)
CopyWorker::CopyWorker(int workerId, const QString &src, const QString &dst)
    : m_id(workerId), m_src(src), m_dst(dst) {}

int CopyWorker::countFiles(const QString &dirPath) {
    int count = 0;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); count++; }
    return count;
}

void CopyWorker::cancel() {
    m_cancelRequested = true;
}

void CopyWorker::process() {
    QDir srcDir(m_src); QDir dstDir(m_dst);
    int totalFiles = countFiles(m_src);

    if (totalFiles == 0) {
        emit statusChanged(m_id, "Папка пуста. Завершение...");
        dstDir.mkpath(m_dst);
        emit progressChanged(m_id, 100); emit finished(m_id, true);
        return;
    }
    if (!dstDir.exists() && !dstDir.mkpath(m_dst)) { emit finished(m_id, false); return; }

    int copiedFiles = 0;
    QDirIterator it(m_src, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QCoreApplication::processEvents();
        if (m_cancelRequested) {
            emit statusChanged(m_id, "Копирование отменено пользователем.");
            emit finished(m_id, false);
            return;
        }

        it.next();
        QFileInfo srcFileInfo = it.fileInfo();
        QString relativePath = srcDir.relativeFilePath(srcFileInfo.absoluteFilePath());
        QString destPath = dstDir.filePath(relativePath);

        if (srcFileInfo.isDir()) {
            // Отправляем статус о создании папки
            emit statusChanged(m_id, QString("Создание папки: %1").arg(srcFileInfo.fileName()));
            QDir subDir; if (!subDir.mkpath(destPath)) { emit finished(m_id, false); return; }
        } else {
            // ОТПРАВЛЯЕМ СТАТУС: Имя текущего копируемого файла
            emit statusChanged(m_id, QString("Копирование: %1").arg(srcFileInfo.fileName()));

            if (QFile::exists(destPath)) {
                QFileInfo destFileInfo(destPath);
                if (srcFileInfo.lastModified() <= destFileInfo.lastModified() && srcFileInfo.size() == destFileInfo.size()) {
                    copiedFiles++;
                    emit progressChanged(m_id, static_cast<int>((static_cast<double>(copiedFiles) / totalFiles) * 100));
                    continue;
                }
                if (!QFile::remove(destPath)) { emit finished(m_id, false); return; }
            }
            if (QFile::copy(srcFileInfo.absoluteFilePath(), destPath)) {
                copiedFiles++;
                emit progressChanged(m_id, static_cast<int>((static_cast<double>(copiedFiles) / totalFiles) * 100));
            } else { emit finished(m_id, false); return; }
        }
    }
    emit statusChanged(m_id, "Готово.");
    emit progressChanged(m_id, 100); emit finished(m_id, true);
}


// ==================== РЕАЛИЗАЦИЯ MainWindow ====================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    this->setFixedSize(this->size());

    // Инициализация массивов данных
    for (int i = 0; i < 3; ++i) {
        m_threads[i] = nullptr;
        m_workers[i] = nullptr;
    }

    // Первоначальная настройка UI элементов
    ui->progressBar_1->setValue(0);
    ui->progressBar_2->setValue(0);
    ui->progressBar_3->setValue(0);

    // Подключение кнопок управления (теперь они ОДНИ на форме)
    connect(ui->btnSelectSource, &QPushButton::clicked, this, &MainWindow::onSelectSourceClicked);
    connect(ui->btnSelectDest, &QPushButton::clicked, this, &MainWindow::onSelectDestClicked);
    connect(ui->btnStartCopy, &QPushButton::clicked, this, &MainWindow::onStartCopyClicked);

    // Подключение единственных полей ввода
    connect(ui->lineEditSource, &QLineEdit::textChanged, this, &MainWindow::onSourceTextChanged);
    connect(ui->lineEditDest, &QLineEdit::textChanged, this, &MainWindow::onDestTextChanged);

    // Подключение переключателей (RadioButtons вместо Checkboxes)
    // В Qt Designer назовите их: radioButton_1, radioButton_2, radioButton_3
    connect(ui->radioButton_1, &QRadioButton::toggled, this, &MainWindow::onProfileChanged);
    connect(ui->radioButton_2, &QRadioButton::toggled, this, &MainWindow::onProfileChanged);
    connect(ui->radioButton_3, &QRadioButton::toggled, this, &MainWindow::onProfileChanged);

    // В конструктор MainWindow:
    connect(ui->btnCancelCopy, &QPushButton::clicked, this, &MainWindow::onCancelCopyClicked);

    // По умолчанию кнопка отмены выключена, пока ничего не копируется
    ui->btnCancelCopy->setEnabled(false);

    // Загружаем сохраненные пути
    loadSettings();

    // Принудительно триггерим обновление текста в полях под выбранный профиль
    ui->radioButton_1->setChecked(true);
    onProfileChanged();
}

MainWindow::~MainWindow() {
    for (int i = 0; i < 3; ++i) {
        if (m_threads[i] && m_threads[i]->isRunning()) {
            m_threads[i]->quit(); m_threads[i]->wait();
        }
    }
    delete ui;
}

// Вспомогательный метод: возвращает индекс (0, 1 или 2) выбранного сейчас профиля
int MainWindow::getCurrentProfileIdx() const {
    if (ui->radioButton_2->isChecked()) return 1;
    if (ui->radioButton_3->isChecked()) return 2;
    return 0; // По умолчанию первый вариант
}

void MainWindow::loadSettings() {
    QSettings settings("MyCompany", "FolderSyncApp");
    for (int i = 0; i < 3; ++i) {
        m_sourcePaths[i] = settings.value(QString("Block_%1/Source").arg(i)).toString();
        m_destPaths[i] = settings.value(QString("Block_%1/Dest").arg(i)).toString();
    }
}

// Срабатывает при ручном изменении текста в поле Source
void MainWindow::onSourceTextChanged(const QString &text) {
    int idx = getCurrentProfileIdx();
    m_sourcePaths[idx] = text.trimmed();

    QSettings settings("MyCompany", "FolderSyncApp");
    settings.setValue(QString("Block_%1/Source").arg(idx), m_sourcePaths[idx]);
}

// Срабатывает при ручном изменении текста в поле Dest
void MainWindow::onDestTextChanged(const QString &text) {
    int idx = getCurrentProfileIdx();
    m_destPaths[idx] = text.trimmed();

    QSettings settings("MyCompany", "FolderSyncApp");
    settings.setValue(QString("Block_%1/Dest").arg(idx), m_destPaths[idx]);
}

// Нажатие на кнопку выбора папки-источника
void MainWindow::onSelectSourceClicked() {
    int idx = getCurrentProfileIdx();
    QString path = QFileDialog::getExistingDirectory(this, "Выберите исходную папку", m_sourcePaths[idx]);
    if (!path.isEmpty()) {
        ui->lineEditSource->setText(path); // Это само вызовет сигнал onSourceTextChanged и сохранит данные
    }
}

// Нажатие на кнопку выбора папки назначения
void MainWindow::onSelectDestClicked() {
    int idx = getCurrentProfileIdx();
    QString path = QFileDialog::getExistingDirectory(this, "Выберите целевую папку", m_destPaths[idx]);
    if (!path.isEmpty()) {
        ui->lineEditDest->setText(path);
    }
}

void MainWindow::startBlockCopy(int blockIdx) {
    if (m_sourcePaths[blockIdx].isEmpty() || m_destPaths[blockIdx].isEmpty()) return;
    if (m_threads[blockIdx] && m_threads[blockIdx]->isRunning()) return;

    if (blockIdx == getCurrentProfileIdx()) {
        ui->btnStartCopy->setEnabled(false);
        ui->btnCancelCopy->setEnabled(true);
    }

    m_workers[blockIdx] = new CopyWorker(blockIdx, m_sourcePaths[blockIdx], m_destPaths[blockIdx]);
    m_threads[blockIdx] = new QThread();
    m_workers[blockIdx]->moveToThread(m_threads[blockIdx]);

    connect(m_threads[blockIdx], &QThread::started, m_workers[blockIdx], &CopyWorker::process);
    connect(m_workers[blockIdx], &CopyWorker::progressChanged, this, &MainWindow::onCopyProgress);
    connect(m_workers[blockIdx], &CopyWorker::statusChanged, this, &MainWindow::onCopyStatusChanged); // <-- ДОБАВИТЬ ЭТУ СТРОКУ
    connect(m_workers[blockIdx], &CopyWorker::finished, this, &MainWindow::onCopyFinished);

    // НАДЕЖНОЕ СВЯЗЫВАНИЕ ОТМЕНЫ: При генерации сигнала requestCancel, у воркера вызовется слот cancel
    connect(this, &MainWindow::requestCancel, m_workers[blockIdx], [this, blockIdx](int targetId){
        if (targetId == blockIdx && m_workers[blockIdx]) {
            m_workers[blockIdx]->cancel();
        }
    }, Qt::DirectConnection); // Прямое соединение мгновенно меняет флаг в памяти

    connect(m_workers[blockIdx], &CopyWorker::finished, m_threads[blockIdx], &QThread::quit);
    connect(m_workers[blockIdx], &QObject::destroyed, m_workers[blockIdx], &QObject::deleteLater);
    connect(m_threads[blockIdx], &QThread::finished, m_threads[blockIdx], &QObject::deleteLater);

    m_threads[blockIdx]->start();
}

// Обновленный слот кнопки "Отмена"
void MainWindow::onCancelCopyClicked() {
    int idx = getCurrentProfileIdx();

    if (m_threads[idx] && m_threads[idx]->isRunning()) {
        ui->btnCancelCopy->setEnabled(false); // Сразу гасим кнопку в UI
        emit requestCancel(idx);             // Отправляем сигнал отмены воркеру
    }
}

void MainWindow::onCopyFinished(int workerId, bool success) {
    if (workerId < 0 || workerId >= 3) return;

    // Сбрасываем состояние кнопок, если завершился текущий активный профиль
    if (workerId == getCurrentProfileIdx()) {
        ui->btnStartCopy->setEnabled(true);
        ui->btnCancelCopy->setEnabled(false); // Выключаем отмену
    }

    m_threads[workerId] = nullptr;
    m_workers[workerId] = nullptr;

    // Не выводим сообщение об успехе, если это была отмена
    if (success) {
        QMessageBox::information(this, "Успех", QString("Синхронизация профиля №%1 завершена!").arg(workerId + 1));
    } else {
        // Можно выводить ошибку, только если отмена не запрашивалась.
        // Но для простоты оставим общее уведомление об остановке процесса.
        QMessageBox::warning(this, "Остановка", QString("Копирование профиля №%1 остановлено или завершилось ошибкой.").arg(workerId + 1));
    }
}

// Метод обновления UI при переключении радиокнопок
void MainWindow::onProfileChanged() {
    int idx = getCurrentProfileIdx();

    ui->lineEditSource->blockSignals(true);
    ui->lineEditDest->blockSignals(true);

    ui->lineEditSource->setText(m_sourcePaths[idx]);
    ui->lineEditDest->setText(m_destPaths[idx]);

    ui->lineEditSource->blockSignals(false);
    ui->lineEditDest->blockSignals(false);

    // ДИНАМИЧЕСКИ ОБНОВЛЯЕМ КНОПКИ ПРИ ПЕРЕКЛЮЧЕНИИ РАДИОКНОПОК:
    // Если этот профиль сейчас активно копируется в фоне — показываем кнопку отмены активной
    bool isRunning = (m_threads[idx] && m_threads[idx]->isRunning());
    ui->btnStartCopy->setEnabled(!isRunning);
    ui->btnCancelCopy->setEnabled(isRunning);

    // Сбрасываем текст статуса или пишем, что поток активен
    if (m_threads[idx] && m_threads[idx]->isRunning()) {
        ui->lblStatus->setText("Выполняется копирование...");
    } else {
        ui->lblStatus->setText("Ожидание запуска...");
    }
}

void MainWindow::onStartCopyClicked() {
    int idx = getCurrentProfileIdx();
    startBlockCopy(idx);
}

void MainWindow::onCopyProgress(int workerId, int percent) {
    QProgressBar* progressBars[] = { ui->progressBar_1, ui->progressBar_2, ui->progressBar_3 };
    if (workerId >= 0 && workerId < 3) {
        progressBars[workerId]->setValue(percent);
    }
}

void MainWindow::onCopyStatusChanged(int workerId, const QString &statusText) {
    // Выводим статус только в том случае, если этот поток соответствует выбранному сейчас профилю
    if (workerId == getCurrentProfileIdx()) {
        ui->lblStatus->setText(statusText);
    }

    // Опционально: выводим в общую консоль отладки Qt Creator
    qDebug() << "Поток" << workerId << ":" << statusText;
}

