// Copyright 2026 air/OS contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QApplication>
#include <QSystemTrayIcon>
#include <QPixmap>
#include <QTimer>
#include <Application.h>
#include <Deskbar.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <unistd.h>
#include <cstdio>

int ownedItems(bool dropHeartbeats = false, int32* lastID = nullptr)
{
    BMessenger deskbar("application/x-vnd.Be-TSKB");
    BMessage count(B_COUNT_PROPERTIES), reply;
    count.AddSpecifier("Replicant");
    count.AddSpecifier("Shelf");
    count.AddSpecifier("View", "Status");
    count.AddSpecifier("Window", "Deskbar");
    if (deskbar.SendMessage(&count, &reply, 1000000, 1000000) != B_OK)
        return -1;
    int32 n = 0;
    if (reply.FindInt32("result", &n) != B_OK)
        return -1;
    int owned = 0;
    for (int32 i = 0; i < n; ++i) {
        BMessage query(B_GET_PROPERTY), result, archive;
        query.AddSpecifier("Replicant", i);
        query.AddSpecifier("Shelf");
        query.AddSpecifier("View", "Status");
        query.AddSpecifier("Window", "Deskbar");
        const void* data = nullptr;
        ssize_t size = 0;
        if (deskbar.SendMessage(&query, &result, 1000000, 1000000) == B_OK
            && result.FindMessage("result", &archive) == B_OK
            && archive.FindData("team", B_ANY_TYPE, &data, &size) == B_OK
            && size == sizeof(team_id) && *(const team_id*)data == getpid()) {
            ++owned;
            if (lastID) {
                BMessage idQuery(B_GET_PROPERTY), idReply;
                idQuery.AddSpecifier("ID");
                idQuery.AddSpecifier("Replicant", i);
                idQuery.AddSpecifier("Shelf");
                idQuery.AddSpecifier("View", "Status");
                idQuery.AddSpecifier("Window", "Deskbar");
                if (deskbar.SendMessage(&idQuery, &idReply, 1000000, 1000000) == B_OK)
                    idReply.FindInt32("result", lastID);
            }
            if (dropHeartbeats) {
                BMessage drop(B_SET_PROPERTY);
                drop.AddInt32("what2", 'MSGR');
                drop.AddMessenger("messenger", BMessenger(be_app));
                void* object = nullptr;
                drop.AddData("qtrayobject", B_ANY_TYPE, &object, sizeof(object));
                drop.AddString("application_name", "Bridge tray regression probe");
                drop.AddSpecifier("View");
                drop.AddSpecifier("Replicant", i);
                drop.AddSpecifier("Shelf");
                drop.AddSpecifier("View", "Status");
                drop.AddSpecifier("Window", "Deskbar");
                deskbar.SendMessage(&drop, (BHandler*)nullptr, 1000000);
            }
        }
    }
    return owned;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::magenta);
    QSystemTrayIcon tray(QIcon(pixmap), &app);
    tray.setToolTip("Bridge tray regression probe");
    tray.show();
    QTimer timer;
    int ticks = 0;
    bool passed = true;
    bool dropHeartbeats = app.arguments().contains("--drop-heartbeats");
    bool removeIcon = app.arguments().contains("--remove-icon");
    QObject::connect(&timer, &QTimer::timeout, [&] {
        int32 id = -1;
        int count = ownedItems(dropHeartbeats, &id);
        std::printf("tick=%d team=%ld owned_tray_items=%d\n", ++ticks, (long)getpid(), count);
        std::fflush(stdout);
        passed &= count == 1 || (removeIcon && ticks > 4 && ticks <= 12 && count == 0);
        if (removeIcon && ticks == 4) {
            BDeskbar deskbar;
            passed &= id >= 0 && deskbar.RemoveItem(id) == B_OK;
        }
        if (ticks == 20) {
            for (int cycle = 0; cycle < 10; ++cycle) {
                tray.hide();
                passed &= ownedItems() == 0;
                tray.show();
                passed &= ownedItems() == 1;
            }
            tray.hide();
            std::printf("after_hide=%d lifecycle_cycles=10 result=%s\n",
                ownedItems(), passed ? "PASS" : "FAIL");
            app.exit(passed && ownedItems() == 0 ? 0 : 1);
        }
    });
    timer.start(1000);
    return app.exec();
}
