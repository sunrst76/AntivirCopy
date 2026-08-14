#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QDateTime>

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
    void onCancelCopyClicked(); // <-- ДОБАВИТЬ: Слот для кнопки отмены
    void onProfileChanged(); // <-- Добавить этот слот

    // Отслеживание изменений текста в полях ввода вручную
    void onSourceTextChanged(const QString &text);
    void onDestTextChanged(const QString &text);

    // Ответные слоты на сигналы из фонового потока
    void onCopyProgress(int workerId, int percent);   
    void onCopyStatusChanged(int workerId, const QString &statusText, qint64 copiedBytes, qint64 totalBytes, qint64 speedBytesSec, qint64 remainingBytes);
    void onCopyFinished(int workerId, bool success);

private:
    Ui::MainWindow *ui;

    // ДОБАВЛЕНО: Вспомогательный метод красивого форматирования размера
    QString formatSize(qint64 bytes, bool isSpeed = false) const;
    // ДОБАВЛЕНО: Вспомогательный метод для красивого форматирования времени
    QString formatTime(int seconds) const;

    // Массивы для путей (индексы 0, 1, 2 для блоков 1, 2, 3)
    QString m_sourcePaths[3];
    QString m_destPaths[3];

    // Массивы для управления параллельными потоками
    QThread* m_threads[3];
    CopyWorker* m_workers[3];
    int getCurrentProfileIdx() const; // <-- ДОБАВИТЬ ЭТУ СТРОКУ

    // Внутренние методы архитектуры приложения
    void startBlockCopy(int blockIdx);
    void loadSettings();
};


#endif // MAINWINDOW_H

