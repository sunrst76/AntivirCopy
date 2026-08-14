#include <QElapsedTimer>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QDateTime>
#include <QCoreApplication>
#include <QElapsedTimer> // Библиотека для замера скорости
#include <QDebug>
#include <QStorageInfo>

CopyWorker::CopyWorker(int workerId, const QString &src, const QString &dst)
    : m_id(workerId), m_src(src), m_dst(dst) {}

void CopyWorker::cancel() {
    m_cancelRequested = true;
}

// Подсчет общего объема папки в байтах
qint64 CopyWorker::countTotalBytes(const QString &dirPath) {
    qint64 totalBytes = 0;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        totalBytes += it.fileInfo().size();
    }
    return totalBytes;
}

void CopyWorker::process() {
    QDir srcDir(m_src);
    QDir dstDir(m_dst);

    qint64 totalBytes = countTotalBytes(m_src);

    if (totalBytes == 0) {
        dstDir.mkpath(m_dst);
        emit statusChanged(m_id, "Папка пуста", 0, 0, 0, 0);
        emit progressChanged(m_id, 100);
        emit finished(m_id, true);
        return;
    }

    if (!dstDir.exists() && !dstDir.mkpath(m_dst)) {
        emit finished(m_id, false);
        return;
    }

    // Проверяем свободное место на диске назначения
    QStorageInfo destDrive(m_dst);
    if (destDrive.bytesAvailable() < totalBytes) {
        emit statusChanged(m_id, "Ошибка: Недостаточно места на целевом диске!", 0, totalBytes, 0, 0);
        emit finished(m_id, false);
        return;
    }

    qint64 copiedBytes = 0;
    QDirIterator it(m_src, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    QElapsedTimer timer;
    timer.start();

    while (it.hasNext()) {
        QCoreApplication::processEvents();

        if (m_cancelRequested) {
            emit statusChanged(m_id, "Отменено", 0, totalBytes, 0, 0);
            emit finished(m_id, false);
            return;
        }

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
            qint64 fileSize = srcFileInfo.size();
            bool isSkipped = false;

            // Проверка на пропуск существующего файла
            if (QFile::exists(destPath)) {
                QFileInfo destFileInfo(destPath);
                if (srcFileInfo.lastModified() <= destFileInfo.lastModified() && fileSize == destFileInfo.size()) {
                    copiedBytes += fileSize;
                    isSkipped = true;

                    // Обновляем прогресс при пропуске
                    int progress = static_cast<int>((static_cast<double>(copiedBytes) / totalBytes) * 100);
                    double elapsedSec = static_cast<double>(timer.elapsed()) / 1000.0;
                    qint64 speedBytesSec = (elapsedSec > 0.05) ? static_cast<qint64>(copiedBytes / elapsedSec) : 0;
                    qint64 remainingBytes = totalBytes - copiedBytes;

                    emit statusChanged(m_id, srcFileInfo.fileName(), copiedBytes, totalBytes, speedBytesSec, remainingBytes);
                    emit progressChanged(m_id, progress);
                } else {
                    if (!QFile::remove(destPath)) {
                        emit finished(m_id, false);
                        return;
                    }
                }
            }

            // Если файл не пропущен, запускаем поблочное копирование
            if (!isSkipped) {
                QFile srcFile(srcFileInfo.absoluteFilePath());
                QFile destFile(destPath);

                if (!srcFile.open(QIODevice::ReadOnly) || !destFile.open(QIODevice::WriteOnly)) {
                    emit finished(m_id, false);
                    return;
                }

                // Размер блока чтения: 4 Мегабайта
                const qint64 bufferSize = 4 * 1024 * 1024;
                QByteArray buffer;

                while (!srcFile.atEnd()) {
                    // Проверяем отмену прямо во время копирования одного большого файла!
                    QCoreApplication::processEvents();
                    if (m_cancelRequested) {
                        srcFile.close();
                        destFile.close();
                        destFile.remove(); // Удаляем недокопированный поврежденный файл
                        emit statusChanged(m_id, "Отменено", 0, totalBytes, 0, 0);
                        emit finished(m_id, false);
                        return;
                    }

                    buffer = srcFile.read(bufferSize);
                    qint64 written = destFile.write(buffer);

                    if (written != buffer.size()) {
                        emit finished(m_id, false);
                        return;
                    }

                    // Прибавляем только что записанные байты к общему счетчику
                    copiedBytes += written;

                    // Мгновенно пересчитываем скорость и прогресс для плавности шкалы
                    int progress = static_cast<int>((static_cast<double>(copiedBytes) / totalBytes) * 100);
                    double elapsedSec = static_cast<double>(timer.elapsed()) / 1000.0;
                    qint64 speedBytesSec = (elapsedSec > 0.05) ? static_cast<qint64>(copiedBytes / elapsedSec) : 0;
                    qint64 remainingBytes = totalBytes - copiedBytes;
                    if (remainingBytes < 0) remainingBytes = 0;

                    // Отправляем данные в интерфейс (теперь это происходит каждые 4 Мб)
                    emit statusChanged(m_id, srcFileInfo.fileName(), copiedBytes, totalBytes, speedBytesSec, remainingBytes);
                    emit progressChanged(m_id, progress);
                }

                srcFile.close();
                destFile.close();

                // Переносим дату изменения оригинального файла на скопированный
                destFile.setFileTime(srcFileInfo.lastModified(), QFileDevice::FileModificationTime);
            }
        }
    }

    double finalElapsedSec = static_cast<double>(timer.elapsed()) / 1000.0;
    qint64 finalSpeed = (finalElapsedSec > 0) ? static_cast<qint64>(totalBytes / finalElapsedSec) : 0;

    emit statusChanged(m_id, "Готово", totalBytes, totalBytes, finalSpeed, 0);
    emit progressChanged(m_id, 100);
    emit finished(m_id, true);
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

    bool isRunning = (m_threads[idx] && m_threads[idx]->isRunning());
    ui->btnStartCopy->setEnabled(!isRunning);
    ui->btnCancelCopy->setEnabled(isRunning);

    // Сбрасываем текст
    if (isRunning) {
        ui->lblStatus->setText("Выполняется расчет объема и копирование...");
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

void MainWindow::onCopyStatusChanged(int workerId, const QString &statusText, qint64 copiedBytes, qint64 totalBytes, qint64 speedBytesSec, qint64 remainingBytes) {
    if (workerId == getCurrentProfileIdx()) {
        QString formattedTotal = formatSize(totalBytes);
        QString formattedSpeed = formatSize(speedBytesSec, true);

        if (statusText == "Готово" || statusText == "Отменено" || statusText == "Папка пуста") {
            ui->lblStatus->setText(QString("%1. Всего: %2 (Ср. скорость: %3)")
                                       .arg(statusText, formattedTotal, formattedSpeed));
        } else {
            QString formattedCopied = formatSize(copiedBytes);

            // Расчет оставшегося времени
            QString timeString;
            if (speedBytesSec > 0) {
                int remainingSeconds = static_cast<int>(remainingBytes / speedBytesSec);
                timeString = formatTime(remainingSeconds);
            } else {
                timeString = "вычисляется...";
            }

            // Выводим информацию в 4 строки без предупреждений Clazy
            QString info = QString("Файл: %1\nПрогресс: %2 из %3\nСкорость: %4\nОсталось времени: %5")
                               .arg(statusText, formattedCopied, formattedTotal, formattedSpeed, timeString);

            ui->lblStatus->setText(info);
        }
    }
}

QString MainWindow::formatSize(qint64 bytes, bool isSpeed) const {
    double size = static_cast<double>(bytes);
    QString unit = "Б";

    if (size >= 1024.0) { size /= 1024.0; unit = "Кб"; }
    if (size >= 1024.0) { size /= 1024.0; unit = "Мб"; }
    if (size >= 1024.0) { size /= 1024.0; unit = "Гб"; }

    QString speedSuffix = isSpeed ? "/с" : "";
    // Сначала преобразуем число size в строку с 2 знаками после запятой
    QString sizeStr = QString::number(size, 'f', 2);

    // Теперь передаем все три строки за один вызов .arg()
    return QString("%1 %2%3").arg(sizeStr, unit, speedSuffix);

}

QString MainWindow::formatTime(int seconds) const {
    if (seconds <= 0) return "вычисляется...";

    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;

    if (h > 0) {
        return QString("%1 ч %2 мин").arg(QString::number(h), QString::number(m));
    } else if (m > 0) {
        return QString("%1 мин %2 сек").arg(QString::number(m), QString::number(s));
    } else {
        return QString("%1 сек").arg(QString::number(s));
    }
}

