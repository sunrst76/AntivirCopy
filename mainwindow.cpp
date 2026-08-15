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
#include <QElapsedTimer>
#include <QDebug>
#include <QStorageInfo>


// ==================== РЕАЛИЗАЦИЯ CopyWorker ====================

CopyWorker::CopyWorker(int workerId, const QString &src, const QString &dst)
    : m_id(workerId), m_src(src), m_dst(dst) {}

void CopyWorker::cancel() {
    m_cancelRequested = true;
}

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

    // ТАЙМЕР ДЛЯ ОГРАНИЧЕНИЯ ЧАСТОТЫ ОБНОВЛЕНИЯ ИНТЕРФЕЙСА (Дросселирование)
    QElapsedTimer uiThrottleTimer;
    uiThrottleTimer.start();

    int lastEmittedProgress = -1;

    while (it.hasNext()) {
        // В фоновом потоке processEvents обрабатывает только сигналы отмены (cancel)
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

            if (QFile::exists(destPath)) {
                QFileInfo destFileInfo(destPath);
                if (srcFileInfo.lastModified() <= destFileInfo.lastModified() && fileSize == destFileInfo.size()) {
                    copiedBytes += fileSize;
                    isSkipped = true;

                    // ОГРАНИЧЕНИЕ ПРИ ПРОПУСКЕ ФАЙЛОВ
                    int progress = static_cast<int>((static_cast<double>(copiedBytes) / totalBytes) * 100);
                    double elapsedSec = static_cast<double>(timer.elapsed()) / 1000.0;
                    qint64 speedBytesSec = (elapsedSec > 0.05) ? static_cast<qint64>(copiedBytes / elapsedSec) : 0;
                    qint64 remainingBytes = qMax(0LL, totalBytes - copiedBytes);

                    // Отправляем UI-сигналы только если изменился процент ИЛИ прошло более 100 мс
                    if (progress != lastEmittedProgress || uiThrottleTimer.elapsed() > 100) {
                        lastEmittedProgress = progress;
                        uiThrottleTimer.restart();
                        emit statusChanged(m_id, srcFileInfo.fileName(), copiedBytes, totalBytes, speedBytesSec, remainingBytes);
                        emit progressChanged(m_id, progress);
                    }
                } else {
                    if (!QFile::remove(destPath)) {
                        emit finished(m_id, false);
                        return;
                    }
                }
            }

            if (!isSkipped) {
                QFile srcFile(srcFileInfo.absoluteFilePath());
                QFile destFile(destPath);

                if (!srcFile.open(QIODevice::ReadOnly) || !destFile.open(QIODevice::WriteOnly)) {
                    emit finished(m_id, false);
                    return;
                }

                const qint64 bufferSize = 4 * 1024 * 1024;
                QByteArray buffer;

                while (!srcFile.atEnd()) {
                    if (m_cancelRequested) {
                        srcFile.close();
                        destFile.close();
                        destFile.remove();
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

                    copiedBytes += written;

                    int progress = static_cast<int>((static_cast<double>(copiedBytes) / totalBytes) * 100);
                    double elapsedSec = static_cast<double>(timer.elapsed()) / 1000.0;
                    qint64 speedBytesSec = (elapsedSec > 0.05) ? static_cast<qint64>(copiedBytes / elapsedSec) : 0;
                    qint64 remainingBytes = qMax(0LL, totalBytes - copiedBytes);

                    // ОГРАНИЧЕНИЕ ВНУТРИ ПОБЛОЧНОГО КОПИРОВАНИЯ БОЛЬШОГО ФАЙЛА
                    if (progress != lastEmittedProgress || uiThrottleTimer.elapsed() > 100) {
                        lastEmittedProgress = progress;
                        uiThrottleTimer.restart();
                        emit statusChanged(m_id, srcFileInfo.fileName(), copiedBytes, totalBytes, speedBytesSec, remainingBytes);
                        emit progressChanged(m_id, progress);
                    }
                }

                srcFile.close();
                destFile.close();
                destFile.setFileTime(srcFileInfo.lastModified(), QFileDevice::FileModificationTime);
            }
        }
    }

    double finalElapsedSec = static_cast<double>(timer.elapsed()) / 1000.0;
    qint64 finalSpeed = (finalElapsedSec > 0) ? static_cast<qint64>(totalBytes / finalElapsedSec) : 0;

    // Финальный статус отправляем ВСЕГДА железно
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

    // Установка иконки приложения из файла ресурсов
    this->setWindowIcon(QIcon(":/free-icon-antivirus-1905446.png"));


    this->setWindowTitle("Копирование антивирусных баз ver 2.02");
    this->setFixedSize(this->size());

    // Инициализация массивов указателей
    for (int i = 0; i < 3; ++i) {
        m_threads[i] = nullptr;
        m_workers[i] = nullptr;
        m_lastExecutionTime[i] = QTime();
    }

    ui->progressBar_1->setValue(0);
    ui->progressBar_2->setValue(0);
    ui->progressBar_3->setValue(0);
    ui->btnCancelCopy->setEnabled(false);

    // Связывание базовых элементов управления
    connect(ui->btnSelectSource, &QPushButton::clicked, this, &MainWindow::onSelectSourceClicked);
    connect(ui->btnSelectDest, &QPushButton::clicked, this, &MainWindow::onSelectDestClicked);
    connect(ui->btnStartCopy, &QPushButton::clicked, this, &MainWindow::onStartCopyClicked);
    connect(ui->btnCancelCopy, &QPushButton::clicked, this, &MainWindow::onCancelCopyClicked);

    connect(ui->lineEditSource, &QLineEdit::textChanged, this, &MainWindow::onSourceTextChanged);
    connect(ui->lineEditDest, &QLineEdit::textChanged, this, &MainWindow::onDestTextChanged);

    // Связывание радиокнопок
    connect(ui->radioButton_1, &QRadioButton::toggled, this, &MainWindow::onProfileChanged);
    connect(ui->radioButton_2, &QRadioButton::toggled, this, &MainWindow::onProfileChanged);
    connect(ui->radioButton_3, &QRadioButton::toggled, this, &MainWindow::onProfileChanged);



    // Связывание элементов планировщика
    connect(ui->comboScheduleMode, &QComboBox::currentIndexChanged, this, &MainWindow::onScheduleTypeChanged);
    connect(ui->spinBoxInterval, &QSpinBox::valueChanged, this, &MainWindow::onScheduleTypeChanged);
    connect(ui->timeEditExact, &QTimeEdit::userTimeChanged, this, &MainWindow::onScheduleTypeChanged);

    loadSettings();

    // ЖЕЛЕЗНЫЙ ЗАПУСК СИСТЕМНОГО ТАЙМЕРА (каждые 3 секунды)
    this->startTimer(3000);

    // Первичная инициализация состояния интерфейса
    ui->radioButton_1->setChecked(true);
    onProfileChanged();

    // Связываем новую кнопку "Выход" с нашим слотом
    connect(ui->btnExitApp, &QPushButton::clicked, this, &MainWindow::onExitButtonClicked);

    // НАСТРОЙКА СИСТЕМНОГО ТРЕЯ
    m_trayIcon = new QSystemTrayIcon(this);

    // В качестве иконки для трея берем стандартную системную иконку Qt (или вашу кастомную)
    m_trayIcon->setIcon(this->style()->standardIcon(QStyle::SP_BrowserReload));
    m_trayIcon->setToolTip("Копирование антивирусных баз ver 2.02");

    // Создаем контекстное меню, которое будет всплывать при клике правой кнопкой мыши в трее
    QMenu* trayMenu = new QMenu(this);
    QAction* restoreAction = trayMenu->addAction("Открыть окно");
    QAction* exitAction = trayMenu->addAction("Выход из программы");
    m_trayIcon->setContextMenu(trayMenu);

    // Связываем действия меню трея с кодом
    connect(restoreAction, &QAction::triggered, this, &MainWindow::showNormal);
    connect(exitAction, &QAction::triggered, this, &MainWindow::onExitButtonClicked);

    // Связываем двойной клик по иконке трея с разворачиванием окна
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);

    // Делаем иконку видимой в панели задач возле часов
    m_trayIcon->show();

}

MainWindow::~MainWindow() {
    for (int i = 0; i < 3; ++i) {
        if (m_threads[i] && m_threads[i]->isRunning()) {
            m_threads[i]->quit();
            m_threads[i]->wait();
        }
    }
    delete ui;
}

int MainWindow::getCurrentProfileIdx() const {
    if (ui->radioButton_2->isChecked()) return 1;
    if (ui->radioButton_3->isChecked()) return 2;
    return 0;
}

QString MainWindow::formatSize(qint64 bytes, bool isSpeed) const {
    double size = static_cast<double>(bytes);
    QString unit = "Б";

    if (size >= 1024.0) { size /= 1024.0; unit = "Кб"; }
    if (size >= 1024.0) { size /= 1024.0; unit = "Мб"; }
    if (size >= 1024.0) { size /= 1024.0; unit = "Гб"; }

    QString sizeStr = QString::number(size, 'f', 2);
    QString speedSuffix = isSpeed ? "/с" : "";
    return QString("%1 %2%3").arg(sizeStr, unit, speedSuffix);
}

QString MainWindow::formatTime(int seconds) const {
    if (seconds <= 0) return "вычисляется...";
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;

    if (h > 0) return QString("%1 ч %2 мин").arg(QString::number(h), QString::number(m));
    if (m > 0) return QString("%1 мин %2 сек").arg(QString::number(m), QString::number(s));
    return QString("%1 сек").arg(QString::number(s));
}

void MainWindow::timerEvent(QTimerEvent *event) {
    Q_UNUSED(event);
    this->onTimerTick(); // Прямой вызов обработчика расписания
}

void MainWindow::onTimerTick() {
    this->setWindowTitle("Копирование антивирусных баз ver 2.02");

    QTime currentTime = QTime::currentTime();

    for (int i = 0; i < 3; ++i) {
        // ЧИТАЕМ ИЗ ОЗУ
        int mode = m_schedConfigs[i].mode;
        if (mode == 0) continue;

        // Режим 1: По интервалу (в минутах)
        if (mode == 1) {
            int intervalMinutes = m_schedConfigs[i].interval;

            if (!m_lastExecutionTime[i].isValid()) {
                // ИСПРАВЛЕНО: Теперь мы НЕ запускаем копирование мгновенно!
                // Мы просто фиксируем текущее время как стартовую точку отсчета.
                m_lastExecutionTime[i] = currentTime;
                qDebug() << "Планировщик: Отсчет пошел для профиля №" << (i + 1)
                         << ". Первый запуск будет через" << intervalMinutes << "мин.";
            } else {
                // Считаем, сколько минут прошло с момента фиксации времени
                int elapsedMinutes = m_lastExecutionTime[i].secsTo(currentTime) / 60;
                if (elapsedMinutes < 0) elapsedMinutes += 1440; // Коррекция перехода через полночь

                // Запуск произойдет только тогда, когда прошло нужное количество минут
                if (elapsedMinutes >= intervalMinutes) {
                    m_lastExecutionTime[i] = currentTime; // Сбрасываем точку отсчета для следующего цикла
                    qDebug() << "Планировщик: Время интервала истекло. Запуск профиля №" << (i + 1);
                    startBlockCopy(i);
                }
            }
        }
        else if (mode == 2) {
            QTime targetTime = m_schedConfigs[i].time;
            if (currentTime.hour() == targetTime.hour() && currentTime.minute() == targetTime.minute()) {
                if (m_lastExecutionTime[i].hour() != currentTime.hour() ||
                    m_lastExecutionTime[i].minute() != currentTime.minute()) {
                    m_lastExecutionTime[i] = currentTime;
                    startBlockCopy(i);
                }
            }
        }
    }
}


void MainWindow::loadSettings() {
    QSettings settings("MyCompany", "FolderSyncApp");
    for (int i = 0; i < 3; ++i) {
        // Пути к папкам мы продолжаем сохранять и загружать, чтобы их не вводить заново
        m_sourcePaths[i] = settings.value(QString("Block_%1/Source").arg(i)).toString();
        m_destPaths[i] = settings.value(QString("Block_%1/Dest").arg(i)).toString();

        // ==================== ЖЕСТКИЙ СБРОС ПЛАНИРОВЩИКА ====================
        // Принудительно выставляем режим 0 (Отключен) для всех профилей при каждом запуске окна
        m_schedConfigs[i].mode = 0;

        // Интервал и время точного запуска можно подгрузить из памяти на случай,
        // если пользователь потом решит включить планировщик кликом
        m_schedConfigs[i].interval = settings.value(QString("Block_%1/SchedInterval").arg(i), 60).toInt();
        m_schedConfigs[i].time = settings.value(QString("Block_%1/SchedTime").arg(i), QTime(18, 0)).toTime();

        // Загружаем дату последней синхронизации
        m_lastSyncTimes[i] = settings.value(QString("Block_%1/LastSync").arg(i), "не проводилась").toString();
    }
}


void MainWindow::onSourceTextChanged(const QString &text) {
    int idx = getCurrentProfileIdx();
    m_sourcePaths[idx] = text.trimmed();
    QSettings settings("MyCompany", "FolderSyncApp");
    settings.setValue(QString("Block_%1/Source").arg(idx), m_sourcePaths[idx]);
}

void MainWindow::onDestTextChanged(const QString &text) {
    int idx = getCurrentProfileIdx();
    m_destPaths[idx] = text.trimmed();
    QSettings settings("MyCompany", "FolderSyncApp");
    settings.setValue(QString("Block_%1/Dest").arg(idx), m_destPaths[idx]);
}

void MainWindow::onSelectSourceClicked() {
    int idx = getCurrentProfileIdx();
    QString path = QFileDialog::getExistingDirectory(this, "Выберите исходную папку", m_sourcePaths[idx]);
    if (!path.isEmpty()) ui->lineEditSource->setText(path);
}

void MainWindow::onSelectDestClicked() {
    int idx = getCurrentProfileIdx();
    QString path = QFileDialog::getExistingDirectory(this, "Выберите целевую папку", m_destPaths[idx]);
    if (!path.isEmpty()) ui->lineEditDest->setText(path);
}

void MainWindow::startBlockCopy(int blockIdx) {
    // Жесткая защита от повторного старта
    if (m_threads[blockIdx] != nullptr || m_workers[blockIdx] != nullptr) {
        qDebug() << "Предотвращен повторный запуск активного профиля №" << (blockIdx + 1);
        return;
    }

    if (m_sourcePaths[blockIdx].isEmpty() || m_destPaths[blockIdx].isEmpty()) return;

    if (blockIdx == getCurrentProfileIdx()) {
        ui->btnStartCopy->setEnabled(false);
        ui->btnCancelCopy->setEnabled(true);
    }

    m_workers[blockIdx] = new CopyWorker(blockIdx, m_sourcePaths[blockIdx], m_destPaths[blockIdx]);
    m_threads[blockIdx] = new QThread();
    m_workers[blockIdx]->moveToThread(m_threads[blockIdx]);

    // Связываем рабочие сигналы прогресса
    connect(m_threads[blockIdx], &QThread::started, m_workers[blockIdx], &CopyWorker::process);
    connect(m_workers[blockIdx], &CopyWorker::progressChanged, this, &MainWindow::onCopyProgress);
    connect(m_workers[blockIdx], &CopyWorker::statusChanged, this, &MainWindow::onCopyStatusChanged);

    // Связываем сигнал отмены
    connect(this, &MainWindow::requestCancel, m_workers[blockIdx], [this, blockIdx](int targetId){
        if (targetId == blockIdx && m_workers[blockIdx]) m_workers[blockIdx]->cancel();
    }, Qt::DirectConnection);

    // СВЯЗЫВАЕМ СИГНАЛ ЗАВЕРШЕНИЯ С ГЛАВНЫМ ОКНОМ (ИСПРАВЛЕНО!)
    // Теперь окно вовремя узнает, что поток умер, и сбросит указатель в nullptr
    connect(m_workers[blockIdx], &CopyWorker::finished, this, &MainWindow::onCopyFinished);

    // Правильное каскадное удаление объектов из памяти после работы
    connect(m_workers[blockIdx], &CopyWorker::finished, m_threads[blockIdx], &QThread::quit);
    connect(m_workers[blockIdx], &CopyWorker::finished, m_workers[blockIdx], &QObject::deleteLater);
    connect(m_threads[blockIdx], &QThread::finished, m_threads[blockIdx], &QObject::deleteLater);

    m_threads[blockIdx]->start();
}



void MainWindow::onStartCopyClicked() {
    startBlockCopy(getCurrentProfileIdx());
}

void MainWindow::onCancelCopyClicked() {
    int idx = getCurrentProfileIdx();
    if (m_threads[idx] && m_threads[idx]->isRunning()) {
        ui->btnCancelCopy->setEnabled(false);
        emit requestCancel(idx);
    }
}

void MainWindow::onCopyProgress(int workerId, int percent) {
    QProgressBar* progressBars[] = { ui->progressBar_1, ui->progressBar_2, ui->progressBar_3 };
    if (workerId >= 0 && workerId < 3) progressBars[workerId]->setValue(percent);
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
            QString timeString = (speedBytesSec > 0) ? formatTime(static_cast<int>(remainingBytes / speedBytesSec)) : "вычисляется...";

            QString info = QString("Файл: %1\nПрогресс: %2 из %3\nСкорость: %4\nОсталось времени: %5")
                               .arg(statusText, formattedCopied, formattedTotal, formattedSpeed, timeString);
            ui->lblStatus->setText(info);
        }
    }
}

void MainWindow::onCopyFinished(int workerId, bool success) {
    if (workerId < 0 || workerId >= 3) return;

    if (success) {
        // Запоминаем текущую дату и время
        QString currentDateTimeStr = QDateTime::currentDateTime().toString("dd.MM.yyyy hh:mm:ss");
        m_lastSyncTimes[workerId] = currentDateTimeStr;

        // Сохраняем в QSettings
        QSettings settings("MyCompany", "FolderSyncApp");
        settings.setValue(QString("Block_%1/LastSync").arg(workerId), currentDateTimeStr);
    }

    if (workerId == getCurrentProfileIdx()) {
        ui->btnStartCopy->setEnabled(true);
        ui->btnCancelCopy->setEnabled(false);

        if (success) {
            ui->lblStatus->setText(QString("Синхронизация успешно завершена.\nПоследний бэкап: %1").arg(m_lastSyncTimes[workerId]));
        } else {
            ui->lblStatus->setText(QString("Копирование остановлено.\nПоследний бэкап: %1").arg(m_lastSyncTimes[workerId]));
        }
    }

    m_threads[workerId] = nullptr;
    m_workers[workerId] = nullptr;
}



void MainWindow::onScheduleTypeChanged() {
    int idx = getCurrentProfileIdx();
    int currentMode = ui->comboScheduleMode->currentIndex();

    ui->spinBoxInterval->setVisible(currentMode == 1);
    ui->timeEditExact->setVisible(currentMode == 2);

    if (currentMode == 1) { ui->spinBoxInterval->raise(); ui->spinBoxInterval->repaint(); }
    if (currentMode == 2) { ui->timeEditExact->raise(); ui->timeEditExact->repaint(); }

    // Обновляем данные в оперативной памяти (происходит мгновенно)
    m_schedConfigs[idx].mode = currentMode;
    m_schedConfigs[idx].interval = ui->spinBoxInterval->value();
    m_schedConfigs[idx].time = ui->timeEditExact->time();

    m_lastExecutionTime[idx] = QTime();

    // Асинхронно сохраняем на диск без блокировки потока
    QSettings settings("MyCompany", "FolderSyncApp");
    settings.setValue(QString("Block_%1/SchedMode").arg(idx), currentMode);
    settings.setValue(QString("Block_%1/SchedInterval").arg(idx), m_schedConfigs[idx].interval);
    settings.setValue(QString("Block_%1/SchedTime").arg(idx), m_schedConfigs[idx].time);
}


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

    // ==================== ИСПРАВЛЕННЫЙ БЛОК ВЫВОДА СТАТУСА ====================
    if (isRunning) {
        ui->lblStatus->setText("Выполняется расчет объема и копирование...");
    } else {
        // Вместо слепого "Ожидание запуска..." выводим точную дату последнего успеха
        ui->lblStatus->setText(QString("Статус: Ожидание запуска...\nПоследняя синхронизация: %1")
                                   .arg(m_lastSyncTimes[idx]));
    }
    // ==========================================================================

    // Блокируем сигналы, чтобы интерфейс не вызывал автосохранение при обновлении полей
    ui->comboScheduleMode->blockSignals(true);
    ui->spinBoxInterval->blockSignals(true);
    ui->timeEditExact->blockSignals(true);

    // БЕРЕМ ДАННЫЕ ИЗ БЫСТРОЙ ОПЕРАТИВНОЙ ПАМЯТИ, А НЕ С ДИСКА!
    int savedMode = m_schedConfigs[idx].mode;
    int savedInterval = m_schedConfigs[idx].interval;
    QTime savedTime = m_schedConfigs[idx].time;

    ui->comboScheduleMode->setCurrentIndex(savedMode);
    ui->spinBoxInterval->setValue(savedInterval);
    ui->timeEditExact->setTime(savedTime);

    ui->spinBoxInterval->setVisible(savedMode == 1);
    ui->timeEditExact->setVisible(savedMode == 2);

    if (savedMode == 1) ui->spinBoxInterval->raise();
    if (savedMode == 2) ui->timeEditExact->raise();

    ui->comboScheduleMode->blockSignals(false);
    ui->spinBoxInterval->blockSignals(false);
    ui->timeEditExact->blockSignals(false);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Если закрытие вызвано кнопкой "Выход", то полностью закрываем программу
    if (m_forceClose) {
        m_trayIcon->hide(); // Скрываем иконку перед выходом
        event->accept();
    } else {
        // Если нажат крестик — отменяем закрытие и просто скрываем окно в трей
        event->ignore();
        this->hide();

        // Показываем всплывающее уведомление Windows (опционально)
        m_trayIcon->showMessage("Бэкап Баз",
                                "Программа свернута в трей и продолжает работать в фоне.",
                                QSystemTrayIcon::Information, 2000);
    }
}

// Слот для кнопки "Выход" (и пункта меню в трее)
void MainWindow::onExitButtonClicked() {
    m_forceClose = true; // Выставляем флаг полного закрытия
    this->close();       // Вызываем стандартное закрытие окна
}

// Слот для обработки кликов по иконке возле часов
void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    // При двойном клике (или обычном клике) левой кнопкой мыши разворачиваем окно обратно на экран
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
        if (this->isHidden()) {
            this->showNormal();
            this->activateWindow(); // Выводим на передний план над всеми окнами
        } else {
            this->hide(); // Если окно уже было открыто, повторный клик скроет его
        }
    }
}
