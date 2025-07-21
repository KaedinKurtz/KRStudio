#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QTreeWidget>
#include <QDebug>


class NodeCatalogWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NodeCatalogWidget(QWidget* parent = nullptr);
    void mouseMoveEvent(QMouseEvent* event);
    void mousePressEvent(QMouseEvent* event);

private slots:
    void onFilterTextChanged(const QString& text);

private:
    void populateTree();

    QLineEdit* m_searchBar;
    QTreeWidget* m_tree;
};
