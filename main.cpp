#include "mainwindow.h"
#include <QApplication>
#include <QSharedMemory>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    // Инициализируем QApplication в самом начале, чтобы QMessageBox гарантированно имел доступ к системным шрифтам и стилям Windows
    QApplication a(argc, argv);

    // Задаем новый уникальный ключ (изменили имя, чтобы очистить старые зависшие кэши ОС)
    QSharedMemory sharedMemory("AntivirCopy_Instance_Lock_v2");

    // Пытаемся создать сегмент памяти. Если он НЕ создался — значит, первая копия ТОЧНО уже работает
    if (!sharedMemory.create(1)) {

        // Показываем окно. Теперь оно появится в 100% случаев, так как QApplication уже инициализирован выше
        QMessageBox::warning(
            nullptr,
            "Внимание",
            "Программа AntivirCopy уже запущена!"
            );

        return 0; // Закрываем вторую копию после клика на "ОК"
    }

    // Сюда попадет только первая копия
    MainWindow w;
    w.show();
    return a.exec();
}
