#!/usr/bin/env python3

# install python3-pyside2.qtgui, .qtwidgets

from PySide2.QtWidgets import QApplication, QSystemTrayIcon, QMenu, QAction
from PySide2.QtGui import QIcon, QPixmap

class App(QApplication):
    def __init__(self):
        super(App, self).__init__([])

        self.icon_type = "theme"
        self.tic_toc = False

        self.setQuitOnLastWindowClosed(False)

        self.tray = QSystemTrayIcon()
        self.tray.setIcon(QIcon.fromTheme("dialog-warning"))
        self.tray.setVisible(True)

        self.menu = QMenu()

        entry = "Use icon name"
        action = QAction(entry)
        action.triggered.connect(self.use_icon_theme)
        self.menu.addAction(action)

        entry = "Use icon file"
        action2 = QAction(entry)
        action2.triggered.connect(self.use_icon_file)
        self.menu.addAction(action2)

        entry = "Use icon pixels"
        action3 = QAction(entry)
        action3.triggered.connect(self.use_icon_pixels)
        self.menu.addAction(action3)

        entry = "Quit"
        action4 = QAction(entry)
        action4.triggered.connect(self.quit)
        self.menu.addAction(action4)

        self.tray.setContextMenu(self.menu)
        self.tray.activated.connect(self.icon_activated)
        self.exec_()

    def use_icon_theme(self, item):
        self.icon_type = "theme"

    def use_icon_file(self, item):
        self.icon_type = "file"

    def use_icon_pixels(self, item):
        self.icon_type = "pixels"

    def icon_activated(self, reason):
        if self.icon_type == "theme":
            icon = QIcon.fromTheme("dialog-warning" if self.tic_toc else "dialog-error")
            self.tray.setIcon(icon)
            self.tic_toc = not self.tic_toc
        elif self.icon_type == "file":
            icon = QIcon("./dialog-warning.png" if self.tic_toc else "./dialog-error.png")
            self.tray.setIcon(icon)
            self.tic_toc = not self.tic_toc
        else:
            pixmap = QPixmap("./dialog-warning.png" if self.tic_toc else "./dialog-error.png")
            icon = QIcon(pixmap)
            self.tray.setIcon(icon)
            self.tic_toc = not self.tic_toc

if __name__ == '__main__':
    app = App()
