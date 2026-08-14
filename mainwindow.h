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

signals:
    void progressChanged(int workerId, int percent);
    void finished(int workerId, bool success);

private:
    int countFiles(const QString &dirPath);
    int m_id;
    QString m_src;
    QString m_dst;
};

// ==================== ГЛАВНОЕ ОКНО (MainWindow) ====================
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Кнопки открытия диалоговых окон
    void onSelectSourceClicked();
    void onSelectDestClicked();

    // Кнопка запуска копирования
    void onStartCopyClicked();

    // Отслеживание изменений текста в полях ввода вручную
    void onSourceTextChanged(const QString &text);
    void onDestTextChanged(const QString &text);

    // Ответные слоты на сигналы из фонового потока
    void onCopyProgress(int workerId, int percent);
    void onCopyFinished(int workerId, bool success);

private:
    Ui::MainWindow *ui;

    // Массивы для путей (индексы 0, 1, 2 для блоков 1, 2, 3)
    QString m_sourcePaths[3];
    QString m_destPaths[3];

    // Массивы для управления параллельными потоками
    QThread* m_threads[3];
    CopyWorker* m_workers[3];

    // Внутренние методы архитектуры приложения
    void startBlockCopy(int blockIdx);
    void loadSettings();
};


#endif // MAINWINDOW_H

