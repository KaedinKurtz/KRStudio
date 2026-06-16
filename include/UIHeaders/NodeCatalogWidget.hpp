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

protected:
    // The QTreeWidget child consumes mouse events, so the press/move that should START a drag never
    // reach this parent widget. Filter the tree viewport's events instead and launch the QDrag there.
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onFilterTextChanged(const QString& text);

private:
    void startNodeDrag(const QPoint& viewportPos);
    void populateTree();

    QPoint m_dragStart;

    QLineEdit* m_searchBar;
    QTreeWidget* m_tree;
};
