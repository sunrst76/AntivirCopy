#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QDateTime>
#include <QTimer>
#include <QTime>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// ==================== ЛОГИКА ФОНОВОГО КОПИРОВАНИЯ (CopyWorker) ====================
class CopyWorker : public QObject {
    Q_OBJECT
public:
    CopyWorker(int workerId, const QString &src, const QString &dst);

public slots:
    void process();
    void cancel();

signals:
    void progressChanged(int workerId, int percent);
    void statusChanged(int workerId, const QString &statusText, qint64 copiedBytes, qint64 totalBytes, qint64 speedBytesSec, qint64 remainingBytes);
    void finished(int workerId, bool success);

private:
    qint64 countTotalBytes(const QString &dirPath); // ИЗМЕНЕНО: Считаем байты, а не файлы
    int m_id;
    QString m_src;
    QString m_dst;
    std::atomic<bool> m_cancelRequested{false};
};

// ==================== ГЛАВНОЕ ОКНО (MainWindow) ====================
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void requestCancel(int blockIdx); // Сигнал для отмены конкретного блока


private slots:

    // Кнопки открытия диалоговых окон
    void onSelectSourceClicked();
    void onSelectDestClicked();

    // Кнопка запуска копирования
    void onStartCopyClicked();
    // Кнопка отмены копирования
    void onCancelCopyClicked();

    // Отслеживание изменений текста в полях ввода вручную
    void onSourceTextChanged(const QString &text);
    void onDestTextChanged(const QString &text);

    // Ответные слоты на сигналы из фонового потока
    void onCopyProgress(int workerId, int percent);
    void onCopyStatusChanged(int workerId, const QString &statusText, qint64 copiedBytes, qint64 totalBytes, qint64 speedBytesSec, qint64 remainingBytes);
    void onCopyFinished(int workerId, bool success);
    void onProfileChanged();
    void onScheduleTypeChanged();    // Слот изменения настроек планировщика
    void onTimerTick();              // Ежеминутный тик таймера
    void onExitButtonClicked();     // Слот для кнопки "Выход"
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason); // Клик по трею

private:
    Ui::MainWindow *ui;

    QString m_lastSyncTimes[3]; // Хранилище даты/времени бэкапа для 3 профилей

    // Вспомогательный метод красивого форматирования размера
    QString formatSize(qint64 bytes, bool isSpeed = false) const;
    // Вспомогательный метод для красивого форматирования времени
    QString formatTime(int seconds) const;

    // Массивы для путей (индексы 0, 1, 2 для блоков 1, 2, 3)
    QString m_sourcePaths[3];
    QString m_destPaths[3];

    // Массивы для управления параллельными потоками
    QThread* m_threads[3];
    CopyWorker* m_workers[3];
    int getCurrentProfileIdx() const;

    // Внутренние методы архитектуры приложения
    void startBlockCopy(int blockIdx);
    void loadSettings();

    // СТРУКТУРА ДЛЯ ХРАНЕНИЯ НАСТРОЕК В ОЗУ (Чтобы не дергать диск при кликах)
    struct ScheduleConfig {
        int mode = 0;
        int interval = 60;
        QTime time = QTime(18, 0);
    };
    ScheduleConfig m_schedConfigs[3]; // Массив настроек для 3-х профилей в ОЗУ

    QTimer* m_scheduleTimer;
    QTime m_lastExecutionTime[3];
    QSystemTrayIcon* m_trayIcon;    // Объект иконки в трее
    bool m_forceClose = false;      // Флаг для полного закрытия утилиты

protected:
    // системный перехватчик тиков таймера
    void timerEvent(QTimerEvent *event) override;
    // Перехват нажатия на системный крестик окна
    void closeEvent(QCloseEvent *event) override;
};


#endif // MAINWINDOW_H

