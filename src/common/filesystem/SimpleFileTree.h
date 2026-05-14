#pragma once
#include "FileTree.h"

class SimpleFileTree : public FileTree {
public:
    explicit SimpleFileTree(const std::string &rootDir);
    void build() override;
    void debug() const override;
    bool addFile(const std::string &relativePath) override;
    bool deleteFile(const std::string& relativePath) override;
    TreeDiff diff(const FileTree &other) const override;
    QString getRootPath() const override;
    FileNode *getRoot() const override;

private:
    void diffNodes(const FileNode *left, const QString &leftRootPath,
                   const FileNode *right, const QString &rightRootPath,
                   const QString &path, TreeDiff &result) const;
    void debugNode(const FileNode *node, int depth) const;
    std::unique_ptr<FileNode> root;
    QString rootPath;
};
