#define NOMINMAX

#include "TextureTool.hpp"

#include <Windows.h>
#include <ShObjIdl.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>

#pragma comment(lib, "Ole32.lib")

namespace texture_tool {

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint32_t kTextureRecordSize = 268;

struct Timer {
    Clock::time_point start = Clock::now();

    double elapsedMs() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
};

struct DirectoryInventory {
    std::vector<fs::path> modelFiles;
    std::vector<fs::path> textureFiles;
};

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::wstring lowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(::towlower(ch));
    });
    return value;
}

std::string trimAscii(std::string value) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
        return !isSpace(static_cast<unsigned char>(ch));
    }).base(), value.end());
    return value;
}

std::string stripUtf8Bom(std::string value) {
    if (value.size() >= 3 &&
        static_cast<unsigned char>(value[0]) == 0xEF &&
        static_cast<unsigned char>(value[1]) == 0xBB &&
        static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    return value;
}

bool hasPathSeparator(std::string_view value) {
    return value.find('/') != std::string_view::npos || value.find('\\') != std::string_view::npos;
}

std::string normalizeSeparators(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    while (value.rfind("./", 0) == 0) {
        value.erase(0, 2);
    }
    while (!value.empty() && value.front() == '/') {
        value.erase(value.begin());
    }
    std::string compact;
    compact.reserve(value.size());
    bool previousSlash = false;
    for (char ch : value) {
        if (ch == '/') {
            if (!previousSlash) {
                compact.push_back(ch);
            }
            previousSlash = true;
        }
        else {
            compact.push_back(ch);
            previousSlash = false;
        }
    }
    return compact;
}

std::string pathGenericUtf8(const fs::path& path) {
    return toUtf8(path.generic_wstring());
}

std::string normalizedPathKeyFromPath(const fs::path& path) {
    return lowerAscii(normalizeSeparators(pathGenericUtf8(path)));
}

std::string fileNameFromTexturePath(std::string_view texturePath) {
    std::string value(texturePath);
    const size_t nullPos = value.find('\0');
    if (nullPos != std::string::npos) {
        value.resize(nullPos);
    }
    value = trimAscii(value);
    value = normalizeSeparators(value);
    const size_t slash = value.find_last_of('/');
    if (slash != std::string::npos) {
        value.erase(0, slash + 1);
    }
    return value;
}

bool isModelExtension(const fs::path& path) {
    const std::wstring ext = lowerWide(path.extension().wstring());
    return ext == L".mdx" || ext == L".mdl";
}

bool isMdxExtension(const fs::path& path) {
    return lowerWide(path.extension().wstring()) == L".mdx";
}

bool isMdlExtension(const fs::path& path) {
    return lowerWide(path.extension().wstring()) == L".mdl";
}

bool isTextureExtension(const fs::path& path) {
    const std::wstring ext = lowerWide(path.extension().wstring());
    return ext == L".blp" || ext == L".tga";
}

bool isBlpExtension(const fs::path& path) {
    return lowerWide(path.extension().wstring()) == L".blp";
}

bool isTextureReference(std::string_view value) {
    const std::string lowered = lowerAscii(fileNameFromTexturePath(value));
    return lowered.size() > 4 &&
        (lowered.ends_with(".blp") || lowered.ends_with(".tga"));
}

std::string normalizeTextureReferenceKey(std::string_view rawPath) {
    std::string value(rawPath);
    const size_t nullPos = value.find('\0');
    if (nullPos != std::string::npos) {
        value.resize(nullPos);
    }
    value = trimAscii(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    value = lowerAscii(normalizeSeparators(value));

    const size_t importPos = value.find("war3mapimported/");
    if (importPos != std::string::npos) {
        value = value.substr(importPos);
    }
    else if (!value.empty() && !hasPathSeparator(value)) {
        value = "war3mapimported/" + value;
    }
    return value;
}

std::vector<std::string> textureFileCandidateKeys(const fs::path& root, const fs::path& textureFile) {
    std::vector<std::string> keys;
    std::error_code ec;
    fs::path relative = fs::relative(textureFile, root, ec);
    if (ec) {
        relative = textureFile.filename();
    }

    const std::string relativeKey = normalizedPathKeyFromPath(relative);
    if (!relativeKey.empty()) {
        keys.push_back(relativeKey);
    }

    const size_t importPos = relativeKey.find("war3mapimported/");
    if (importPos != std::string::npos) {
        keys.push_back(relativeKey.substr(importPos));
    }

    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

std::vector<std::string> splitLines(std::string value) {
    std::vector<std::string> lines;
    std::stringstream stream(value);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::string readWholeFileBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

fs::path programDirectory() {
    std::wstring buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        return fs::current_path();
    }
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}

fs::path normalizedAbsolutePath(const fs::path& path) {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        absolute = path;
    }
    fs::path canonical = fs::weakly_canonical(absolute, ec);
    return ec ? absolute.lexically_normal() : canonical;
}

void sortAndDedupePaths(std::vector<fs::path>& paths) {
    std::sort(paths.begin(), paths.end(), [](const fs::path& a, const fs::path& b) {
        return lowerWide(a.wstring()) < lowerWide(b.wstring());
    });
    paths.erase(std::unique(paths.begin(), paths.end(), [](const fs::path& a, const fs::path& b) {
        return lowerWide(a.wstring()) == lowerWide(b.wstring());
    }), paths.end());
}

DirectoryInventory collectDirectoryInventory(const fs::path& root) {
    DirectoryInventory inventory;
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        return inventory;
    }

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && it != end) {
        const fs::directory_entry entry = *it;
        if (entry.is_regular_file(ec)) {
            const fs::path path = entry.path();
            if (isModelExtension(path)) {
                inventory.modelFiles.push_back(path);
            }
            else if (isTextureExtension(path)) {
                inventory.textureFiles.push_back(path);
            }
        }
        it.increment(ec);
    }

    sortAndDedupePaths(inventory.modelFiles);
    sortAndDedupePaths(inventory.textureFiles);
    return inventory;
}

class BLPValidator {
public:
    static bool isValidTextureFile(const fs::path& path) {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) {
            return false;
        }
        if (!isBlpExtension(path)) {
            return isTextureExtension(path);
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        char magic[4] = {};
        file.read(magic, sizeof(magic));
        if (file.gcount() != sizeof(magic)) {
            return false;
        }

        return std::memcmp(magic, "BLP1", 4) == 0 || std::memcmp(magic, "BLP2", 4) == 0;
    }
};

uintmax_t fileSizeOrZero(const fs::path& path) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    return ec ? 0 : size;
}

std::vector<std::string> parseMDLTextureReferences(const fs::path& mdlPath) {
    std::vector<std::string> refs;
    const std::string content = readWholeFileBytes(mdlPath);
    if (content.empty()) {
        return refs;
    }

    size_t pos = 0;
    while (pos < content.size()) {
        const size_t firstQuote = content.find('"', pos);
        if (firstQuote == std::string::npos) {
            break;
        }
        const size_t secondQuote = content.find('"', firstQuote + 1);
        if (secondQuote == std::string::npos) {
            break;
        }

        std::string candidate = content.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        if (isTextureReference(candidate)) {
            refs.push_back(std::move(candidate));
        }
        pos = secondQuote + 1;
    }

    return refs;
}

std::vector<std::string> parseModelTextureReferences(const fs::path& modelPath) {
    std::vector<std::string> refs;
    if (isMdxExtension(modelPath)) {
        MDXTextureParser parser;
        if (!parser.parseMDX(modelPath)) {
            return refs;
        }
        refs.reserve(parser.textures().size());
        for (const TextureEntry& texture : parser.textures()) {
            if (!texture.filePath.empty() && isTextureReference(texture.filePath)) {
                refs.push_back(texture.filePath);
            }
        }
        return refs;
    }

    if (isMdlExtension(modelPath)) {
        return parseMDLTextureReferences(modelPath);
    }

    return refs;
}

bool readExact(std::ifstream& file, char* buffer, std::streamsize bytes) {
    file.read(buffer, bytes);
    return file.gcount() == bytes;
}

bool isTextureChunk(const char header[4]) {
    return std::memcmp(header, "TEXS", 4) == 0 || std::memcmp(header, "TEX", 3) == 0;
}

bool copyBytes(std::ifstream& input, std::ofstream& output, uint32_t bytes) {
    std::vector<char> buffer(64 * 1024);
    uint32_t remaining = bytes;
    while (remaining > 0) {
        const std::streamsize toRead = static_cast<std::streamsize>(std::min<uint32_t>(remaining, static_cast<uint32_t>(buffer.size())));
        input.read(buffer.data(), toRead);
        const std::streamsize readCount = input.gcount();
        if (readCount <= 0) {
            return false;
        }
        output.write(buffer.data(), readCount);
        remaining -= static_cast<uint32_t>(readCount);
    }
    return true;
}

std::wstring quoteArgument(const std::wstring& value) {
    std::wstring quoted = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') {
            quoted += L"\\\"";
        }
        else {
            quoted += ch;
        }
    }
    quoted += L"\"";
    return quoted;
}

std::optional<std::string> executeCommandWithPipe(const std::wstring& command, const fs::path& workingDirectory) {
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &securityAttributes, 0)) {
        return std::nullopt;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;

    PROCESS_INFORMATION processInfo{};
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');

    const std::wstring cwd = workingDirectory.empty() ? std::wstring() : workingDirectory.wstring();
    const BOOL created = CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        cwd.empty() ? nullptr : cwd.c_str(),
        &startupInfo,
        &processInfo);

    CloseHandle(writePipe);

    if (!created) {
        CloseHandle(readPipe);
        return std::nullopt;
    }

    std::string output;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer, buffer + bytesRead);
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);
    CloseHandle(readPipe);
    return output;
}

bool copyFileReplacing(const fs::path& source, const fs::path& destination) {
    std::error_code ec;
    if (fs::exists(destination, ec) && fs::equivalent(source, destination, ec)) {
        return true;
    }
    return CopyFileW(source.wstring().c_str(), destination.wstring().c_str(), FALSE) != 0;
}

struct ExportStats {
    size_t exported = 0;
    size_t wouldExport = 0;
    size_t misses = 0;
};

ExportStats exportReferencedTextures(
    const MDXTextureParser& parser,
    TextureSearchCache& searchCache,
    const ProcessOptions& options) {
    ExportStats stats;

    for (const TextureEntry& texture : parser.textures()) {
        const std::string key = lowerAscii(normalizeSeparators(texture.filePath));
        if (key.find("war3mapimported/") == std::string::npos) {
            continue;
        }

        const std::string basename = fileNameFromTexturePath(texture.filePath);
        if (basename.empty()) {
            continue;
        }

        const std::optional<fs::path> source = searchCache.findTextureByBasename(basename);
        if (!source) {
            std::cout << "未找到贴图: " << basename << std::endl;
            ++stats.misses;
            continue;
        }

        fs::path destinationDirectory = parser.filename().parent_path();
        if (destinationDirectory.empty()) {
            destinationDirectory = options.currentDirectory;
        }
        const fs::path destination = destinationDirectory / bytesToWideBestEffort(basename);
        if (options.dryRun) {
            std::cout << "[dry-run] 将导出: " << pathToUtf8(*source) << " -> " << pathToUtf8(destination) << std::endl;
            ++stats.wouldExport;
            continue;
        }

        if (copyFileReplacing(*source, destination)) {
            ++stats.exported;
        }
        else {
            std::cout << "复制贴图失败: " << pathToUtf8(*source) << " -> " << pathToUtf8(destination)
                      << " (Win32 " << GetLastError() << ")" << std::endl;
            ++stats.misses;
        }
    }

    return stats;
}

std::string relativeDisplayPath(const fs::path& root, const fs::path& path) {
    std::error_code ec;
    fs::path relative = fs::relative(path, root, ec);
    return pathToUtf8(ec ? path : relative);
}

void printScanResult(const fs::path& root, const ScanResult& result, bool benchmark) {
    size_t index = 0;
    for (const fs::path& path : result.unusedTextures) {
        ++index;
        std::cout << "未被引用(" << index << "/" << result.textureFiles.size() << "): "
                  << relativeDisplayPath(root, path) << std::endl;
    }

    std::cout << "模型文件数: " << result.modelFiles.size() << std::endl;
    std::cout << "贴图文件数: " << result.textureFiles.size() << std::endl;
    std::cout << "共有 " << result.unusedTextures.size() << " 个文件未被引用, 共计大小("
              << (result.unusedBytes / 1024 / 1024) << "M)" << std::endl;

    if (benchmark) {
        std::cout << "Benchmark: enumerate=" << result.enumerateMs << "ms, parse="
                  << result.parseMs << "ms, check=" << result.checkMs
                  << "ms, total=" << result.totalMs << "ms" << std::endl;
    }
}

void printProcessStats(const ProcessStats& stats, bool benchmark) {
    std::cout << "输入模型数: " << stats.inputs << std::endl;
    std::cout << "已处理 MDX: " << stats.mdxProcessed << ", 跳过 MDL: " << stats.mdlSkipped << std::endl;
    std::cout << "已重写: " << stats.rewritten << ", dry-run 将重写: " << stats.wouldRewrite << std::endl;
    std::cout << "已导出贴图: " << stats.exported << ", dry-run 将导出: " << stats.wouldExport
              << ", 未找到: " << stats.exportMisses << std::endl;

    if (benchmark) {
        std::cout << "Benchmark: externalSearches=" << stats.externalSearches
                  << ", total=" << stats.totalMs << "ms" << std::endl;
    }
}

void printUsage(const fs::path& exePath) {
    const std::string exe = pathToUtf8(exePath.filename());
    std::cout
        << "用法:\n"
        << "  " << exe << " <file-or-dir>... [--dry-run] [--benchmark]\n"
        << "  " << exe << " --scan-unused <dir> [--benchmark]\n"
        << "  " << exe << " --help\n"
        << "\n"
        << "无参数启动时会打开 MDX/MDL 多选文件框。" << std::endl;
}

} // namespace

std::string toUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), bytes, nullptr, nullptr);
    return result;
}

std::wstring bytesToWideBestEffort(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    auto convert = [&](UINT codePage, DWORD flags) -> std::wstring {
        const int chars = MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (chars <= 0) {
            return {};
        }
        std::wstring result(static_cast<size_t>(chars), L'\0');
        MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), result.data(), chars);
        return result;
    };

    std::wstring utf8 = convert(CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!utf8.empty()) {
        return utf8;
    }
    return convert(CP_ACP, 0);
}

std::string pathToUtf8(const fs::path& path) {
    return toUtf8(path.wstring());
}

bool MDXTextureParser::parseMDX(const fs::path& mdxFilename) {
    filename_ = mdxFilename;
    textures_.clear();

    std::ifstream file(filename_, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    char header[4] = {};
    if (!readExact(file, header, sizeof(header)) || std::memcmp(header, "MDLX", 4) != 0) {
        return false;
    }

    while (file.good()) {
        char chunkHeader[4] = {};
        uint32_t chunkSize = 0;
        if (!readExact(file, chunkHeader, sizeof(chunkHeader))) {
            break;
        }
        if (!readExact(file, reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize))) {
            return false;
        }

        if (!isTextureChunk(chunkHeader)) {
            file.seekg(chunkSize, std::ios::cur);
            if (!file.good()) {
                return false;
            }
            continue;
        }

        if (chunkSize % kTextureRecordSize != 0) {
            return false;
        }

        const uint32_t textureCount = chunkSize / kTextureRecordSize;
        textures_.reserve(textureCount);
        for (uint32_t i = 0; i < textureCount; ++i) {
            TextureEntry texture;
            char filepath[261] = {};
            if (!readExact(file, reinterpret_cast<char*>(&texture.replaceableId), sizeof(texture.replaceableId)) ||
                !readExact(file, filepath, 260) ||
                !readExact(file, reinterpret_cast<char*>(&texture.flags), sizeof(texture.flags))) {
                textures_.clear();
                return false;
            }
            texture.filePath = filepath;
            textures_.push_back(std::move(texture));
        }
        return true;
    }

    return true;
}

bool MDXTextureParser::addPrefix(std::string_view prefix) {
    bool modified = false;
    for (TextureEntry& texture : textures_) {
        if (texture.filePath.empty() || hasPathSeparator(texture.filePath)) {
            continue;
        }
        texture.filePath = std::string(prefix) + "\\" + texture.filePath;
        modified = true;
    }
    return modified;
}

bool MDXTextureParser::replaceAllTexturePaths(std::string_view searchPath, std::string_view replacementPath) {
    if (searchPath.empty()) {
        return false;
    }

    bool modified = false;
    for (TextureEntry& texture : textures_) {
        size_t pos = texture.filePath.find(searchPath);
        if (pos == std::string::npos) {
            continue;
        }

        std::string next = texture.filePath;
        while (pos != std::string::npos) {
            next.replace(pos, searchPath.size(), replacementPath);
            pos = next.find(searchPath, pos + replacementPath.size());
        }

        if (next.size() > 260) {
            std::cout << "跳过过长贴图路径: " << next << std::endl;
            continue;
        }

        texture.filePath = std::move(next);
        modified = true;
    }
    return modified;
}

bool MDXTextureParser::rewriteMDXFile(bool dryRun) const {
    if (dryRun) {
        return true;
    }

    std::ifstream inputFile(filename_, std::ios::binary);
    if (!inputFile.is_open()) {
        return false;
    }

    fs::path tempFilename = filename_;
    tempFilename += L".tmp";
    std::ofstream outputFile(tempFilename, std::ios::binary | std::ios::trunc);
    if (!outputFile.is_open()) {
        return false;
    }

    char header[4] = {};
    if (!readExact(inputFile, header, sizeof(header))) {
        return false;
    }
    outputFile.write(header, sizeof(header));

    bool texChunkFound = false;
    while (inputFile.good()) {
        char chunkHeader[4] = {};
        uint32_t chunkSize = 0;
        if (!readExact(inputFile, chunkHeader, sizeof(chunkHeader))) {
            break;
        }
        if (!readExact(inputFile, reinterpret_cast<char*>(&chunkSize), sizeof(chunkSize))) {
            return false;
        }

        outputFile.write(chunkHeader, sizeof(chunkHeader));
        outputFile.write(reinterpret_cast<const char*>(&chunkSize), sizeof(chunkSize));

        if (!isTextureChunk(chunkHeader)) {
            if (!copyBytes(inputFile, outputFile, chunkSize)) {
                return false;
            }
            continue;
        }

        if (chunkSize != textures_.size() * kTextureRecordSize) {
            return false;
        }

        for (const TextureEntry& texture : textures_) {
            outputFile.write(reinterpret_cast<const char*>(&texture.replaceableId), sizeof(texture.replaceableId));

            char filepath[260] = {};
            const size_t bytesToCopy = std::min(sizeof(filepath), texture.filePath.size());
            std::memcpy(filepath, texture.filePath.data(), bytesToCopy);
            outputFile.write(filepath, sizeof(filepath));

            outputFile.write(reinterpret_cast<const char*>(&texture.flags), sizeof(texture.flags));
        }

        inputFile.seekg(chunkSize, std::ios::cur);
        if (!inputFile.good()) {
            return false;
        }
        texChunkFound = true;
    }

    inputFile.close();
    outputFile.close();

    if (!texChunkFound) {
        std::error_code ec;
        fs::remove(tempFilename, ec);
        return false;
    }

    const std::wstring temp = tempFilename.wstring();
    const std::wstring target = filename_.wstring();
    return MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

void TextureReferenceIndex::addReference(std::string_view texturePath) {
    const std::string key = normalizeTextureReferenceKey(texturePath);
    if (!key.empty()) {
        keys_.insert(key);
    }
}

bool TextureReferenceIndex::containsTextureFile(const fs::path& root, const fs::path& textureFile) const {
    for (const std::string& key : textureFileCandidateKeys(root, textureFile)) {
        if (keys_.find(key) != keys_.end()) {
            return true;
        }
    }
    return false;
}

TextureSearchCache::TextureSearchCache(fs::path programDirectory)
    : programDirectory_(std::move(programDirectory)) {
}

void TextureSearchCache::setLocalSearchRoots(std::vector<fs::path> roots) {
    localSearchRoots_.clear();
    localSearchRoots_.reserve(roots.size());
    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
            continue;
        }
        localSearchRoots_.push_back(normalizedAbsolutePath(root));
    }
    sortAndDedupePaths(localSearchRoots_);
    localIndexBuilt_ = false;
    localTexturesByBasename_.clear();
}

std::optional<fs::path> TextureSearchCache::findTextureByBasename(std::string_view basename) {
    const std::string basenameString = fileNameFromTexturePath(basename);
    if (basenameString.empty()) {
        return std::nullopt;
    }

    const std::string basenameKey = lowerAscii(basenameString);
    const auto cached = cache_.find(basenameKey);
    if (cached != cache_.end()) {
        return cached->second;
    }

    buildLocalIndex();

    std::optional<fs::path> result = findBestLocalCandidate(basenameKey);
    if (!result) {
        result = findWithEverything(basenameString);
    }

    cache_[basenameKey] = result;
    return result;
}

void TextureSearchCache::buildLocalIndex() {
    if (localIndexBuilt_) {
        return;
    }
    localIndexBuilt_ = true;

    for (const fs::path& root : localSearchRoots_) {
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        while (!ec && it != end) {
            const fs::directory_entry entry = *it;
            if (entry.is_regular_file(ec) && isTextureExtension(entry.path())) {
                const std::string basenameKey = lowerAscii(pathToUtf8(entry.path().filename()));
                localTexturesByBasename_[basenameKey].push_back(entry.path());
            }
            it.increment(ec);
        }
    }

    for (auto& [_, paths] : localTexturesByBasename_) {
        sortAndDedupePaths(paths);
    }
}

std::optional<fs::path> TextureSearchCache::findBestLocalCandidate(const std::string& basenameKey) const {
    const auto found = localTexturesByBasename_.find(basenameKey);
    if (found == localTexturesByBasename_.end()) {
        return std::nullopt;
    }

    std::optional<fs::path> best;
    uintmax_t bestSize = 0;
    for (const fs::path& candidate : found->second) {
        if (!BLPValidator::isValidTextureFile(candidate)) {
            continue;
        }
        const uintmax_t size = fileSizeOrZero(candidate);
        if (!best || size > bestSize) {
            best = candidate;
            bestSize = size;
        }
    }
    return best;
}

std::optional<fs::path> TextureSearchCache::findWithEverything(std::string_view basename) {
    ++externalSearches_;
    fs::path esPath = programDirectory_ / L"es.exe";
    const std::wstring executable = fs::exists(esPath) ? esPath.wstring() : L"es.exe";
    const std::wstring command = quoteArgument(executable) + L" -w " + quoteArgument(bytesToWideBestEffort(basename));
    const std::optional<std::string> output = executeCommandWithPipe(command, programDirectory_);
    if (!output) {
        return std::nullopt;
    }

    const std::string basenameKey = lowerAscii(std::string(basename));
    std::optional<fs::path> best;
    uintmax_t bestSize = 0;
    for (std::string line : splitLines(*output)) {
        line = trimAscii(line);
        if (line.empty()) {
            continue;
        }

        const fs::path candidate = bytesToWideBestEffort(line);
        if (lowerAscii(pathToUtf8(candidate.filename())) != basenameKey) {
            continue;
        }
        if (!BLPValidator::isValidTextureFile(candidate)) {
            continue;
        }
        const uintmax_t size = fileSizeOrZero(candidate);
        if (!best || size > bestSize) {
            best = candidate;
            bestSize = size;
        }
    }

    return best;
}

ScanResult TextureScanner::findUnusedTextures(const ScanOptions& options) const {
    ScanResult result;
    Timer totalTimer;

    const fs::path root = normalizedAbsolutePath(options.root);

    Timer enumerateTimer;
    DirectoryInventory inventory = collectDirectoryInventory(root);
    result.enumerateMs = enumerateTimer.elapsedMs();
    result.modelFiles = std::move(inventory.modelFiles);
    result.textureFiles = std::move(inventory.textureFiles);

    Timer parseTimer;
    TextureReferenceIndex index = BuildTextureReferenceIndex(root, result.modelFiles);
    result.parseMs = parseTimer.elapsedMs();

    Timer checkTimer;
    for (const fs::path& texture : result.textureFiles) {
        if (!index.containsTextureFile(root, texture)) {
            result.unusedTextures.push_back(texture);
            result.unusedBytes += fileSizeOrZero(texture);
        }
    }
    result.checkMs = checkTimer.elapsedMs();
    result.totalMs = totalTimer.elapsedMs();
    return result;
}

std::vector<fs::path> ShowModelFileDialog(std::chrono::milliseconds* elapsed) {
    const auto start = Clock::now();
    std::vector<fs::path> files;

    HRESULT coResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool needsUninitialize = SUCCEEDED(coResult);
    if (FAILED(coResult) && coResult != RPC_E_CHANGED_MODE) {
        return files;
    }

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            options |= FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
            dialog->SetOptions(options);
        }

        const COMDLG_FILTERSPEC filters[] = {
            { L"MDX and MDL Files", L"*.mdx;*.mdl" },
            { L"All Files", L"*.*" },
        };
        dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
        dialog->SetFileTypeIndex(1);
        dialog->SetTitle(L"选择 MDX/MDL 模型文件");

        hr = dialog->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItemArray* results = nullptr;
            hr = dialog->GetResults(&results);
            if (SUCCEEDED(hr)) {
                DWORD count = 0;
                results->GetCount(&count);
                files.reserve(count);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(results->GetItemAt(i, &item))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            files.emplace_back(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                results->Release();
            }
        }
        dialog->Release();
    }

    if (needsUninitialize) {
        CoUninitialize();
    }

    if (elapsed != nullptr) {
        *elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
    }
    return files;
}

std::vector<fs::path> CollectInputModels(const std::vector<fs::path>& inputs) {
    std::vector<fs::path> models;
    for (const fs::path& input : inputs) {
        std::error_code ec;
        const fs::path path = normalizedAbsolutePath(input);
        if (fs::is_regular_file(path, ec)) {
            if (isModelExtension(path)) {
                models.push_back(path);
            }
            continue;
        }

        if (!fs::is_directory(path, ec)) {
            continue;
        }

        DirectoryInventory inventory = collectDirectoryInventory(path);
        models.insert(models.end(), inventory.modelFiles.begin(), inventory.modelFiles.end());
    }

    sortAndDedupePaths(models);
    return models;
}

TextureReferenceIndex BuildTextureReferenceIndex(const fs::path&, const std::vector<fs::path>& modelFiles) {
    TextureReferenceIndex index;
    if (modelFiles.empty()) {
        return index;
    }

    const size_t hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const size_t threadCount = std::min<size_t>(hardwareThreads, modelFiles.size());
    std::atomic<size_t> nextIndex = 0;
    std::mutex mergeMutex;
    std::vector<std::thread> workers;
    workers.reserve(threadCount);

    for (size_t workerIndex = 0; workerIndex < threadCount; ++workerIndex) {
        workers.emplace_back([&]() {
            std::vector<std::string> localRefs;
            while (true) {
                const size_t current = nextIndex.fetch_add(1);
                if (current >= modelFiles.size()) {
                    break;
                }

                std::vector<std::string> refs = parseModelTextureReferences(modelFiles[current]);
                localRefs.insert(localRefs.end(), refs.begin(), refs.end());
            }

            if (!localRefs.empty()) {
                std::lock_guard<std::mutex> lock(mergeMutex);
                for (const std::string& ref : localRefs) {
                    index.addReference(ref);
                }
            }
        });
    }

    for (std::thread& worker : workers) {
        worker.join();
    }

    return index;
}

ScanResult FindUnusedTextures(const ScanOptions& options) {
    TextureScanner scanner;
    return scanner.findUnusedTextures(options);
}

ProcessStats ProcessModelBatch(const std::vector<fs::path>& modelFiles, const ProcessOptions& options) {
    Timer totalTimer;
    ProcessStats stats;
    stats.inputs = modelFiles.size();

    std::vector<fs::path> searchRoots;
    searchRoots.push_back(options.programDirectory);
    for (const fs::path& model : modelFiles) {
        searchRoots.push_back(model.parent_path());
    }

    TextureSearchCache searchCache(options.programDirectory);
    searchCache.setLocalSearchRoots(std::move(searchRoots));

    for (const fs::path& model : modelFiles) {
        if (isMdlExtension(model)) {
            std::cout << "跳过 MDL 改写(仅扫描支持): " << pathToUtf8(model) << std::endl;
            ++stats.mdlSkipped;
            continue;
        }

        if (!isMdxExtension(model)) {
            continue;
        }

        MDXTextureParser parser;
        if (!parser.parseMDX(model)) {
            std::cout << "无法解析 MDX: " << pathToUtf8(model) << std::endl;
            continue;
        }

        ++stats.mdxProcessed;
        bool modified = parser.addPrefix("war3mapimported");
        for (const ReplaceRule& rule : options.replaceRules) {
            modified = parser.replaceAllTexturePaths(rule.from, rule.to) || modified;
        }

        if (modified) {
            if (options.dryRun) {
                std::cout << "[dry-run] 将重写: " << pathToUtf8(model) << std::endl;
                ++stats.wouldRewrite;
            }
            else if (parser.rewriteMDXFile(false)) {
                std::cout << "已重写: " << pathToUtf8(model) << std::endl;
                ++stats.rewritten;
            }
            else {
                std::cout << "重写失败: " << pathToUtf8(model) << std::endl;
            }
        }

        ExportStats exportStats = exportReferencedTextures(parser, searchCache, options);
        stats.exported += exportStats.exported;
        stats.wouldExport += exportStats.wouldExport;
        stats.exportMisses += exportStats.misses;
    }

    stats.externalSearches = searchCache.externalSearches();
    stats.totalMs = totalTimer.elapsedMs();
    return stats;
}

std::vector<ReplaceRule> LoadReplaceRules(const fs::path& programDirectory) {
    std::vector<ReplaceRule> rules;
    std::unordered_set<std::string> seen;
    std::string content = stripUtf8Bom(readWholeFileBytes(programDirectory / L"replace.txt"));
    if (content.empty()) {
        return rules;
    }

    for (std::string line : splitLines(std::move(content))) {
        line = trimAscii(line);
        if (line.empty()) {
            continue;
        }

        const size_t arrow = line.find("->");
        if (arrow == std::string::npos) {
            std::cout << "忽略无效 replace.txt 行: " << line << std::endl;
            continue;
        }

        ReplaceRule rule;
        rule.from = trimAscii(line.substr(0, arrow));
        rule.to = trimAscii(line.substr(arrow + 2));
        if (rule.from.empty()) {
            std::cout << "忽略空搜索项 replace.txt 行: " << line << std::endl;
            continue;
        }

        const std::string key = rule.from + '\0' + rule.to;
        if (seen.insert(key).second) {
            rules.push_back(std::move(rule));
        }
    }

    return rules;
}

int Run(int argc, wchar_t* argv[]) {
    const fs::path exePath = argc > 0 ? fs::path(argv[0]) : fs::path(L"模型贴图检索.exe");
    const fs::path appDirectory = programDirectory();
    const fs::path currentDirectory = fs::current_path();

    bool dryRun = false;
    bool benchmark = false;
    bool scanUnused = false;
    fs::path scanRoot;
    std::vector<fs::path> inputs;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--dry-run") {
            dryRun = true;
        }
        else if (arg == L"--benchmark") {
            benchmark = true;
        }
        else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            printUsage(exePath);
            return 0;
        }
        else if (arg == L"--scan-unused") {
            scanUnused = true;
            if (i + 1 >= argc) {
                std::cout << "--scan-unused 需要目录参数" << std::endl;
                printUsage(exePath);
                return 1;
            }
            scanRoot = argv[++i];
        }
        else {
            inputs.emplace_back(arg);
        }
    }

    if (scanUnused) {
        const fs::path root = normalizedAbsolutePath(scanRoot);
        std::error_code ec;
        if (!fs::is_directory(root, ec)) {
            std::cout << "扫描目录不存在: " << pathToUtf8(root) << std::endl;
            return 1;
        }

        ScanOptions options;
        options.root = root;
        options.benchmark = benchmark;
        ScanResult result = FindUnusedTextures(options);
        printScanResult(root, result, benchmark);
        return 0;
    }

    if (inputs.empty()) {
        std::chrono::milliseconds dialogElapsed(0);
        inputs = ShowModelFileDialog(&dialogElapsed);
        if (benchmark) {
            std::cout << "Benchmark: dialog=" << dialogElapsed.count() << "ms" << std::endl;
        }
        if (inputs.empty()) {
            return 0;
        }
    }

    std::vector<fs::path> modelFiles = CollectInputModels(inputs);
    if (modelFiles.empty()) {
        std::cout << "没有找到 MDX/MDL 文件。" << std::endl;
        return 0;
    }

    ProcessOptions options;
    options.dryRun = dryRun;
    options.benchmark = benchmark;
    options.programDirectory = appDirectory;
    options.currentDirectory = currentDirectory;
    options.replaceRules = LoadReplaceRules(appDirectory);

    ProcessStats stats = ProcessModelBatch(modelFiles, options);
    printProcessStats(stats, benchmark);
    return 0;
}

} // namespace texture_tool
