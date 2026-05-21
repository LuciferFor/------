#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>

class BLPValidator {
public:
    // BLP文件头结构
    struct BLPHeader {
        char magic[4];     // 魔术数字，应该是 'BLP2'
        uint32_t type;     // 压缩类型
        uint32_t encoding; // 编码方式
        uint32_t alphaDepth; // Alpha通道深度
        uint32_t width;    // 宽度
        uint32_t height;   // 高度
        uint32_t mipMapCount; // 多级渐远纹理的层数
        uint32_t dataSize;    // 数据大小
    };

    static bool isValidBLPFile(const std::string& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件: " << filepath << std::endl;
            return false;
        }

        BLPHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(BLPHeader));

        // 检查基本参数
        if (header.type > 1) {  // 通常type只能是0或1
            std::cerr << "无效的BLP压缩类型" << std::endl;
            return false;
        }

        // 检查图像尺寸
        if (header.width == 0 || header.height == 0 ||
            header.width > 4096 || header.height > 4096) {
            std::cerr << "无效的图像尺寸" << std::endl;
            return false;
        }

        // 检查多级渐远纹理层数
        if (header.mipMapCount > 8) {
            std::cerr << "异常的多级渐远纹理层数" << std::endl;
            return false;
        }

        // 检查数据大小
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();

        if (header.dataSize > fileSize) {
            std::cerr << "数据大小超过文件实际大小" << std::endl;
            return false;
        }

        return true;
    }

    // 获取BLP文件的基本信息
    static bool getBLPInfo(const std::string& filepath,
        uint32_t& width,
        uint32_t& height,
        uint32_t& mipMapCount) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "无法打开文件: " << filepath << std::endl;
            return false;
        }

        BLPHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(BLPHeader));

        // 再次验证文件头
        if (strncmp(header.magic, "BLP2", 4) != 0) {
            return false;
        }

        width = header.width;
        height = header.height;
        mipMapCount = header.mipMapCount;

        return true;
    }

    // 检查文件扩展名（可选）
    static bool hasValidBLPExtension(const std::string& filepath) {
        size_t dotPos = filepath.find_last_of('.');
        if (dotPos == std::string::npos) return false;

        std::string ext = filepath.substr(dotPos + 1);
        // 转换为小写
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        return ext == "blp";
    }
};
