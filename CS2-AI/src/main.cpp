#include <QApplication>
#include <QTimer>
#include <windows.h>
#include "UI/MainWindow.h"
#include "CS2/CS2AI.h"

// È«¾ÖÔÝÍ£±êÖ¾
volatile bool g_isPaused = false;

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    MainWindow w;
    w.show();

    QTimer keyTimer;
    QObject::connect(&keyTimer, &QTimer::timeout, [&]() {
        // F8 ÍË³ö
        if (GetAsyncKeyState(VK_F8) & 0x8000) {
            qInfo("F8 pressed, exiting application.");
            app.quit();
        }

        // F7 ÔÝÍ£/»Ö¸´ÇÐ»»
        static bool lastF7 = false;
        bool curF7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
        if (curF7 && !lastF7) {
            g_isPaused = !g_isPaused;
            if (g_isPaused)
                qInfo("F7 pressed, paused.");
            else
                qInfo("F7 pressed, resumed.");
        }
        lastF7 = curF7;
        });
    keyTimer.start(50);

    return app.exec();
}
