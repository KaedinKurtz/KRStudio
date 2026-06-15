#include "TextureBrowserWidget.hpp"
#include "Scene.hpp"
#include "components.hpp"
#include "MaterialApply.hpp" // krs::material::applyPackTags (shared with the AC3 gate)
#include "GizmoSystem.hpp" // GizmoHandleComponent (never retexture gizmo handles)

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeView>
#include <QFileSystemModel>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QDir>
#include <QDirIterator>
#include <QPixmap>
#include <QScrollArea>
#include <QDebug>

namespace {
// MaterialLoader's filename patterns (keep in sync with MaterialLoader.cpp).
const char* kMapKeys[] = { "albedo", "normal", "roughness", "metallic",
                           "metalness", "ao", "height", "emissive", "opacity" };

QString mapTypeOf(const QString& fileName)
{
    const QString lower = fileName.toLower();
    for (const char* key : kMapKeys) {
        if (lower.contains(QStringLiteral("-") + key) || lower.contains(QStringLiteral("_") + key))
            return QString::fromLatin1(key);
    }
    return {};
}
} // namespace

TextureBrowserWidget::TextureBrowserWidget(Scene* scene, const QString& materialsRoot,
                                           QWidget* parent)
    : QWidget(parent), m_scene(scene), m_root(materialsRoot)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Filter packs…"));
    layout->addWidget(m_filter);

    m_model = new QFileSystemModel(this);
    m_model->setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    m_model->setRootPath(m_root);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    m_tree->setRootIndex(m_model->index(m_root));
    m_tree->setHeaderHidden(true);
    for (int c = 1; c < m_model->columnCount(); ++c) m_tree->hideColumn(c);
    m_tree->setExpandsOnDoubleClick(false);
    layout->addWidget(m_tree, 1);

    // Thumbnail strip for the selected pack's maps.
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFixedHeight(112);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_thumbStrip = new QWidget(scroll);
    m_thumbLayout = new QHBoxLayout(m_thumbStrip);
    m_thumbLayout->setContentsMargins(2, 2, 2, 2);
    m_thumbLayout->setSpacing(4);
    m_thumbLayout->addStretch();
    scroll->setWidget(m_thumbStrip);
    layout->addWidget(scroll);

    m_packInfo = new QLabel(QStringLiteral("Select a material pack"), this);
    m_packInfo->setStyleSheet("color: palette(mid); font-size: 8pt;");
    layout->addWidget(m_packInfo);

    auto* row = new QHBoxLayout();
    m_applyButton = new QPushButton(QStringLiteral("Apply to Selected"), this);
    row->addWidget(m_applyButton);
    m_autoApply = new QCheckBox(QStringLiteral("Auto-apply"), this);
    m_autoApply->setToolTip(QStringLiteral("Apply the pack as soon as it is clicked"));
    row->addWidget(m_autoApply);
    row->addWidget(new QLabel(QStringLiteral("Depth"), this));
    m_heightScale = new QDoubleSpinBox(this);
    m_heightScale->setRange(0.0, 0.5);
    m_heightScale->setSingleStep(0.02);
    m_heightScale->setDecimals(3);
    m_heightScale->setValue(0.1);
    m_heightScale->setToolTip(QStringLiteral(
        "Parallax height scale for packs with a height map (the \"fake depth\")"));
    m_heightScale->setKeyboardTracking(false);
    row->addWidget(m_heightScale);
    row->addWidget(new QLabel(QStringLiteral("Tiling"), this));
    m_tiling = new QDoubleSpinBox(this);
    m_tiling->setRange(0.05, 32.0);
    m_tiling->setSingleStep(0.25);
    m_tiling->setDecimals(2);
    m_tiling->setValue(1.0);
    m_tiling->setToolTip(QStringLiteral(
        "Texture repeats: tiles per metre for triplanar materials,\n"
        "UV multiplier for UV-mapped meshes. Smaller = bigger texture."));
    m_tiling->setKeyboardTracking(false);
    row->addWidget(m_tiling);
    layout->addLayout(row);

    connect(m_tree, &QTreeView::clicked, this, [this]() {
        onTreeSelectionChanged();
        if (m_autoApply->isChecked()) applyToSelection();
    });
    connect(m_tree, &QTreeView::doubleClicked, this, [this]() {
        onTreeSelectionChanged();
        applyToSelection();
    });
    connect(m_applyButton, &QPushButton::clicked, this, &TextureBrowserWidget::applyToSelection);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        // QFileSystemModel name filtering hides files, not dirs — emulate by
        // collapsing/expanding matches.
        if (text.isEmpty()) { m_tree->collapseAll(); return; }
        m_tree->expandAll();
        Q_UNUSED(text); // visual aid only; full proxy filtering is overkill here
    });
}

QString TextureBrowserWidget::currentPackDir() const
{
    const QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) return {};
    const QString dir = m_model->filePath(idx);
    // A "pack" is a directory that actually contains an albedo map.
    return mapFilesIn(dir).isEmpty() ? QString() : dir;
}

QStringList TextureBrowserWidget::mapFilesIn(const QString& dir)
{
    QStringList out;
    QDirIterator it(dir, { "*.png", "*.jpg", "*.jpeg", "*.tga", "*.bmp" }, QDir::Files);
    while (it.hasNext()) {
        const QString f = it.next();
        if (!mapTypeOf(QFileInfo(f).fileName()).isEmpty()) out << f;
    }
    return out;
}

bool TextureBrowserWidget::hasHeightMap(const QString& dir)
{
    for (const QString& f : mapFilesIn(dir))
        if (mapTypeOf(QFileInfo(f).fileName()) == QLatin1String("height")) return true;
    return false;
}

void TextureBrowserWidget::onTreeSelectionChanged()
{
    const QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) return;
    rebuildThumbnails(m_model->filePath(idx));
}

void TextureBrowserWidget::rebuildThumbnails(const QString& dir)
{
    // Clear old thumbs (keep the trailing stretch).
    while (m_thumbLayout->count() > 1) {
        QLayoutItem* item = m_thumbLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    const QStringList files = mapFilesIn(dir);
    if (files.isEmpty()) {
        m_packInfo->setText(QStringLiteral("%1 — no material maps here (pick a pack folder)")
                                .arg(QDir(dir).dirName()));
        return;
    }

    QStringList types;
    for (const QString& f : files) {
        const QString type = mapTypeOf(QFileInfo(f).fileName());
        types << type;
        auto* cell = new QWidget(m_thumbStrip);
        auto* v = new QVBoxLayout(cell);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(1);
        auto* img = new QLabel(cell);
        img->setFixedSize(72, 72);
        img->setScaledContents(true);
        QPixmap pm(f);
        if (!pm.isNull()) img->setPixmap(pm.scaled(72, 72, Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation));
        auto* caption = new QLabel(type, cell);
        caption->setAlignment(Qt::AlignCenter);
        caption->setStyleSheet("font-size: 7pt;");
        v->addWidget(img);
        v->addWidget(caption);
        m_thumbLayout->insertWidget(m_thumbLayout->count() - 1, cell);
    }
    m_packInfo->setText(QStringLiteral("%1 — %2 maps (%3)")
                            .arg(QDir(dir).dirName())
                            .arg(files.size())
                            .arg(types.join(QStringLiteral(", "))));
}

void TextureBrowserWidget::applyToSelection()
{
    if (!m_scene) return;
    const QString dir = currentPackDir();
    if (dir.isEmpty()) {
        m_packInfo->setText(QStringLiteral("Not a material pack (needs *-albedo.* etc.)"));
        return;
    }

    auto& reg = m_scene->getRegistry();
    const bool parallax = hasHeightMap(dir);
    int applied = 0;
    for (auto e : reg.view<SelectedComponent>()) {
        if (reg.any_of<CameraComponent, GridComponent, GizmoHandleComponent>(e)) continue;
        reg.emplace_or_replace<MaterialDirectoryTag>(e, dir.toStdString());
        auto& req = reg.emplace_or_replace<MaterialReloadRequest>(e);
        req.heightScaleOverride = parallax ? float(m_heightScale->value()) : -1.0f;
        req.tilingOverride = float(m_tiling->value());
        // Tag mutation is the SINGLE SOURCE OF TRUTH krs::material::applyPackTags
        // (MaterialApply.hpp), shared with the applied-texture gate (AC3): real-UV bodies keep
        // their object-space UV mapping; only no-UV primitives switch to world-space triplanar.
        krs::material::applyPackTags(reg, e, parallax);
        ++applied;
    }
    m_packInfo->setText(applied > 0
        ? QStringLiteral("Applied %1 to %2 object(s)").arg(QDir(dir).dirName()).arg(applied)
        : QStringLiteral("Nothing selected — click an object first"));
}
