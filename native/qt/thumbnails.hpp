#pragma once
#include <QDialog>
#include <QListWidget>
// Windows compositor previews are only for the picker; media uses the selected
// native capture method. Registration never calls PrintWindow on the UI thread.
void installSourceThumbnails(QDialog* dialog, QListWidget* list);
