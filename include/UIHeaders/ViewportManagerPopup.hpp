#pragma once
#include <QWidget>
namespace ads { class CDockWidget; }
class Scene;

namespace Ui { class ViewportManagerWidget; }

class ViewportManagerPopup : public QWidget {
    Q_OBJECT
public:
    explicit ViewportManagerPopup(QWidget* parent = nullptr);
    ~ViewportManagerPopup() override;

    void updateUi(const QList<ads::CDockWidget*>& viewportDocks,
        Scene* scene);

signals:
    void addViewportRequested();
    void removeViewportRequested();
    void resetViewportsRequested();
    void showViewportRequested(ads::CDockWidget*);

private:
    Ui::ViewportManagerWidget* ui;
    void clearLayout(QLayout* layout);
};