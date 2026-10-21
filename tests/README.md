# Native tray regression test

On Haiku with Qt 6 development packages:

```sh
qmake6 tray-lifecycle.pro
make
./tray-lifecycle
./tray-lifecycle --drop-heartbeats
```

Each run owns a temporary tray icon for 20 seconds, checks its actual Deskbar
replicants, then exercises ten hide/show cycles and checks cleanup. The second
run redirects only its own icon's heartbeat messages away from Qt to reproduce
lost replies while the icon still exists. It must retain exactly one icon. The
old implementation creates another icon every six seconds and leaves earlier
icons behind on hide. The helper removes them when the test process exits.

No other application's tray items are modified.
