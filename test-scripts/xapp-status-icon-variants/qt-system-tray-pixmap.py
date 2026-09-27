#!/usr/bin/env python3

# install python3-pyside6.qtgui, .qtwidgets
#
# Exercises the pixmap-only StatusNotifierItem path. Qt only sends IconName when
# the QIcon has a theme name; a file- or pixmap-backed QIcon has none, so Qt
# sends IconPixmap only (sizes above 64px are dropped, 22px and 64px are added
# if missing) and xapp-sn-watcher must convert it to a temporary png. This is
# what flameshot does:
#
#     QIcon::fromTheme("flameshot-tray", QIcon(":img/app/flameshot.png"))
#
# An optional image path argument is used as the icon source instead of a
# pixmap rendered from the icon theme.

import sys

from PySide6.QtWidgets import QApplication, QSystemTrayIcon, QMenu
from PySide6.QtGui import QIcon, QPixmap, QAction

LARGE_SOURCE_SIZE = 128
SMALL_SOURCE_FILES = ("./dialog-warning.png", "./dialog-error.png")

class App(QApplication):
    def __init__(self, source_path):
        super(App, self).__init__([])

        self.source_path = source_path
        self.source_type = "large"
        self.tic_toc = False

        self.setQuitOnLastWindowClosed(False)

        self.tray = QSystemTrayIcon()
        self.tray.setIcon(self.make_icon())
        self.tray.setVisible(True)

        self.menu = QMenu()

        entry = "Large source (22 + 64 px pixmaps sent)"
        action = QAction(entry)
        action.triggered.connect(self.use_large_source)
        self.menu.addAction(action)

        entry = "Small source (24 px file, only 22 + 24 px pixmaps sent)"
        action2 = QAction(entry)
        action2.triggered.connect(self.use_small_source)
        self.menu.addAction(action2)

        entry = "Quit"
        action3 = QAction(entry)
        action3.triggered.connect(self.quit)
        self.menu.addAction(action3)

        self.tray.setContextMenu(self.menu)
        self.tray.activated.connect(self.icon_activated)
        self.exec()

    def make_icon(self):
        theme_name = "dialog-warning" if self.tic_toc else "dialog-error"

        if self.source_type == "small":
            fallback = QIcon(SMALL_SOURCE_FILES[self.tic_toc])
        elif self.source_path:
            fallback = QIcon(self.source_path)
        else:
            fallback = QIcon(QIcon.fromTheme(theme_name).pixmap(LARGE_SOURCE_SIZE))

        # The theme name is deliberately nonexistent so the nameless fallback is used.
        return QIcon.fromTheme("nonexistent-tray-icon", fallback)

    def use_large_source(self, item):
        self.source_type = "large"
        self.tray.setIcon(self.make_icon())

    def use_small_source(self, item):
        self.source_type = "small"
        self.tray.setIcon(self.make_icon())

    def icon_activated(self, reason):
        self.tic_toc = not self.tic_toc
        self.tray.setIcon(self.make_icon())

if __name__ == '__main__':
    app = App(sys.argv[1] if len(sys.argv) > 1 else None)
