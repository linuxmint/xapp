#!/usr/bin/env python3

# install python3-pyside2.qtgui, .qtwidgets

from PySide2.QtWidgets import QApplication, QSystemTrayIcon, QMenu, QAction
from PySide2.QtGui import QIcon, QPixmap

icon_type = "theme"
tic_toc = False

app = QApplication([])
app.setQuitOnLastWindowClosed(False)

icon = QIcon.fromTheme("dialog-warning")
tray = QSystemTrayIcon()
tray.setIcon(icon)

tray.setVisible(True)


def use_icon_theme(item):
    global icon_type
    icon_type = "theme"

def use_icon_file(item):
    global icon_type
    icon_type = "file"

def use_icon_pixels(item):
    global icon_type
    icon_type = "pixels"

def icon_activated(reason):
    global tic_toc
    global icon_type
    if icon_type == "theme":
        icon = QIcon.fromTheme("dialog-warning" if tic_toc else "dialog-error")
        tic_toc = not tic_toc
        tray.setIcon(icon)
    elif icon_type == "file":
        icon = QIcon("./dialog-warning.png" if tic_toc else "./dialog-error.png")
        tic_toc = not tic_toc
        tray.setIcon(icon)
    else:
        pixmap = QPixmap("./dialog-warning.png" if tic_toc else "./dialog-error.png")
        tic_toc = not tic_toc
        icon = QIcon(pixmap)
        tray.setIcon(icon)

menu = QMenu()

entry = "Use icon name"
action = QAction(entry)
action.triggered.connect(use_icon_theme)
menu.addAction(action)

entry = "Use icon file"
action2 = QAction(entry)
action2.triggered.connect(use_icon_file)
menu.addAction(action2)

entry = "Use icon pixels"
action3 = QAction(entry)
action3.triggered.connect(use_icon_pixels)
menu.addAction(action3)

entry = "Quit"
action4 = QAction(entry)
action4.triggered.connect(app.quit)
menu.addAction(action4)

tray.setContextMenu(menu)
tray.activated.connect(icon_activated)

app.exec_()