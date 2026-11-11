#pragma once
#include <QString>
#include <QStringList>

class QTreeWidgetItem;
namespace language {
void setEnglish(bool enabled);
QString text(const char* source);
// These roles keep source strings separate from file names and paths.
void setItemText(QTreeWidgetItem* item, int column, const char* source, const QStringList& arguments = {});
void retranslateItem(QTreeWidgetItem* item);
QString diagnosticText(const QString& source);
}
