#pragma once
#include "FileTree.h"
#include "MerkleTree.h"
#include "SimpleFileTree.h"
#include <memory>

template <TreeType Type>
class FileTreeFactory {
public:
    static std::unique_ptr<FileTree> create(const std::string &rootDir) {
        if constexpr (Type == TreeType::Vanilla) {
            return std::make_unique<SimpleFileTree>(rootDir);
        } else if constexpr (Type == TreeType::Merkle) {
            return std::make_unique<MerkleTree>(rootDir);
        }
        return nullptr;
    }
};
