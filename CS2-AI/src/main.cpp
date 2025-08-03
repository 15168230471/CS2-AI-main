#include <QApplication>
#include <QTimer>
#include <windows.h>
#include "UI/MainWindow.h"
#include "CS2/CS2AI.h"


volatile bool g_isPaused = false;

int main(int argc, char* argv[])
{
    // Windows 10+ 推荐：让进程自己对 DPI 负责，避免系统虚拟化坐标
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    QApplication app(argc, argv);
    MainWindow w;
    w.show();

    QTimer keyTimer;
    QObject::connect(&keyTimer, &QTimer::timeout, [&]() {
        
        if (GetAsyncKeyState(VK_F8) & 0x8000) {
            qInfo("F8 pressed, exiting application.");
            app.quit();
        }

        
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
