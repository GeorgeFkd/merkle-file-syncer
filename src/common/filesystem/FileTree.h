#include <vector>
#include <string>
#include <memory>
#include <array>
enum class FileType{
    File,Directory
};

class FileNode {
public:
    FileType type;
    std::string path;
    //will not use it right now
    std::array<uint8_t,32> hash;
    std::vector<std::unique_ptr<FileNode>> children;
};

class FilesystemTree {
public:
    static FilesystemTree buildFrom(const std::string& rootDir);
private:
    std::unique_ptr<FileNode> root;
};
