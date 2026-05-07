#include "FileTree.h"
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QString>
std::unique_ptr<FileNode> buildNode(const QString &path) {
    auto node = std::make_unique<FileNode>();
    QFileInfo info(path);

    node->path = info.fileName().toStdString();

    if (info.isDir()) {
        node->type = FileType::Directory;
        QDir dir(path);
        const auto entries = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries,QDir::Name);
        for (const auto &entry : entries) {
            node->children.emplace_back(buildNode(entry.absoluteFilePath()));
        }
    } else {
        node->type = FileType::File;
    }

    return node;
}

FilesystemTree FilesystemTree::buildFrom(const std::string &rootDir) {
    FilesystemTree tree;
    auto strDir = QString::fromStdString(rootDir);
    QFileInfo rootPath(strDir);
    qDebug() << "Building filesystem tree from: " << strDir << " \n";
    assert(rootPath.isDir() && "Root directory path given is not a directory");
    tree.root = buildNode(QString::fromStdString(rootDir));
    return tree;
}
