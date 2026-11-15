/// @file
/// @brief AssetsDock (AssetsDock.h).

#include "Panels/AssetsDock.h"

#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QTreeView>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

AssetsDock::AssetsDock(QWidget* parent)
    : QDockWidget(u"Assets"_s, parent)
    , m_model(new QFileSystemModel(this)) // Qt parent–child ownership
    , m_tree(new QTreeView(this))
{
    setObjectName(u"AssetsDock"_s); // QMainWindow::saveState() keys docks by object name
    m_model->setNameFilters({u"*.m2scene"_s, u"*.gltf"_s, u"*.glb"_s});
    m_model->setNameFilterDisables(false); // hide what doesn't match, rather than greying it out
    m_tree->setModel(m_model);
    m_tree->setObjectName(u"AssetsTree"_s);
    m_tree->setHeaderHidden(true);
    for (int column = 1; column < m_model->columnCount(); ++column)
    {
        m_tree->hideColumn(column); // size, type, date: the name is enough here
    }
    connect(m_tree, &QTreeView::activated, this,
            [this](const QModelIndex& index) { activate(m_model->filePath(index)); });
    setWidget(m_tree);
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
