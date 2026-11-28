/// @file
/// @brief AssetsDock (AssetsDock.h).

#include "Panels/AssetsDock.h"

#include "Panels/TreeBranches.h"

#include <QAbstractFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QTreeView>
#if MEOW2_EDITOR_SVG_ICONS
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#endif

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

QString assetIconName(const QFileInfo& file)
{
    if (file.isDir())
    {
        return u"Folder"_s;
    }
    const QString suffix = file.suffix().toLower();
    if (suffix == u"m2scene"_s)
    {
        return u"Scene"_s;
    }
    if (suffix == u"gltf"_s || suffix == u"glb"_s)
    {
        return u"Mesh"_s;
    }
    if (suffix == u"png"_s || suffix == u"jpg"_s || suffix == u"jpeg"_s || suffix == u"m2texture"_s)
    {
        return u"Texture"_s;
    }
    if (suffix == u"hlsl"_s || suffix == u"m2shader"_s)
    {
        return u"Shader"_s;
    }
    return {};
}

namespace
{
/// @brief The engine's asset icons (assets/icons/assets/*.svg, built in as :/icons/<Name>.svg) for the Assets tree.
///        Drawn once per icon into a pixmap; without Qt's Svg module, Qt's own file icons.
class AssetIconProvider final : public QAbstractFileIconProvider
{
public:
    [[nodiscard]] QIcon icon(const QFileInfo& info) const override
    {
#if MEOW2_EDITOR_SVG_ICONS
        if (const QString name = assetIconName(info); !name.isEmpty())
        {
            auto cached = m_icons.find(name);
            if (cached == m_icons.end())
            {
                QSvgRenderer renderer(u":/icons/%1.svg"_s.arg(name));
                QIcon icon;
                for (const int size : {16, 24, 32, 48, 64, 96}) // sharp at the tree's size on high-DPI displays too
                {
                    QPixmap pixmap(size, size);
                    pixmap.fill(Qt::transparent);
                    QPainter painter(&pixmap);
                    renderer.render(&painter);
                    icon.addPixmap(pixmap);
                }
                cached = m_icons.insert(name, icon);
            }
            return *cached;
        }
#endif
        return QAbstractFileIconProvider::icon(info);
    }

private:
    mutable QHash<QString, QIcon> m_icons; ///< By icon name, made on first use.
};
} // namespace

AssetsDock::AssetsDock(QWidget* parent)
    : QDockWidget(u"Assets"_s, parent)
    , m_model(new QFileSystemModel(this)) // Qt parent–child ownership
    , m_tree(new ArrowTreeView(this))
{
    setObjectName(u"AssetsDock"_s); // QMainWindow::saveState() keys docks by object name
    m_icons = std::make_unique<AssetIconProvider>();
    m_model->setIconProvider(m_icons.get());
    // The project's scenes, models and textures (Scenes/, Assets/); not the .meta sidecars or the project file.
    m_model->setNameFilters(
        {u"*.m2scene"_s, u"*.gltf"_s, u"*.glb"_s, u"*.png"_s, u"*.jpg"_s, u"*.jpeg"_s, u"*.hlsl"_s});
    m_model->setNameFilterDisables(false); // hide what doesn't match, rather than greying it out
    m_tree->setModel(m_model);
    m_tree->setObjectName(u"AssetsTree"_s);
    m_tree->setHeaderHidden(true);
    m_tree->setIconSize(QSize(28, 28)); // the asset icons are pictures, too small to read at the default 16 px
    for (int column = 1; column < m_model->columnCount(); ++column)
    {
        m_tree->hideColumn(column); // size, type, date: the name is enough here
    }
    connect(m_tree, &QTreeView::activated, this,
            [this](const QModelIndex& index) { activate(m_model->filePath(index)); });
    setWidget(m_tree);
}

AssetsDock::~AssetsDock()
{
    m_model->setIconProvider(nullptr); // the model (a child, deleted after this) must not ask a deleted provider
}

void AssetsDock::setProject(const QString& folder)
{
    if (folder == m_folder)
    {
        return;
    }
    m_folder = folder;
    m_tree->setRootIndex(m_model->setRootPath(folder));
}

void AssetsDock::activate(const QString& path)
{
    if (m_onActivate && QFileInfo{path}.isFile())
    {
        m_onActivate(path);
    }
}
} // namespace Meow2::Editor
