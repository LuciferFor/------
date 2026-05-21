#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace texture_tool {

namespace fs = std::filesystem;

struct ReplaceRule {
    std::string from;
    std::string to;
};

struct ProcessOptions {
    bool dryRun = false;
    bool benchmark = false;
    fs::path programDirectory;
    fs::path currentDirectory;
    std::vector<ReplaceRule> replaceRules;
};

struct ScanOptions {
    fs::path root;
    bool benchmark = false;
};

struct ScanResult {
    std::vector<fs::path> modelFiles;
    std::vector<fs::path> textureFiles;
    std::vector<fs::path> unusedTextures;
    uintmax_t unusedBytes = 0;
    double enumerateMs = 0.0;
    double parseMs = 0.0;
    double checkMs = 0.0;
    double totalMs = 0.0;
};

struct ProcessStats {
    size_t inputs = 0;
    size_t mdxProcessed = 0;
    size_t mdlSkipped = 0;
    size_t rewritten = 0;
    size_t wouldRewrite = 0;
    size_t exported = 0;
    size_t wouldExport = 0;
    size_t exportMisses = 0;
    size_t externalSearches = 0;
    double totalMs = 0.0;
};

struct TextureEntry {
    uint32_t replaceableId = 0;
    std::string filePath;
    uint32_t flags = 0;
};

class MDXTextureParser {
public:
    bool parseMDX(const fs::path& mdxFilename);
    bool addPrefix(std::string_view prefix);
    bool replaceAllTexturePaths(std::string_view searchPath, std::string_view replacementPath);
    bool rewriteMDXFile(bool dryRun) const;

    const fs::path& filename() const { return filename_; }
    const std::vector<TextureEntry>& textures() const { return textures_; }

private:
    fs::path filename_;
    std::vector<TextureEntry> textures_;
};

class TextureReferenceIndex {
public:
    void addReference(std::string_view texturePath);
    bool containsTextureFile(const fs::path& root, const fs::path& textureFile) const;
    size_t size() const { return keys_.size(); }

private:
    std::unordered_set<std::string> keys_;
};

class TextureSearchCache {
public:
    explicit TextureSearchCache(fs::path programDirectory);

    void setLocalSearchRoots(std::vector<fs::path> roots);
    std::optional<fs::path> findTextureByBasename(std::string_view basename);
    size_t externalSearches() const { return externalSearches_; }

private:
    void buildLocalIndex();
    std::optional<fs::path> findBestLocalCandidate(const std::string& basenameKey) const;
    std::optional<fs::path> findWithEverything(std::string_view basename);

    fs::path programDirectory_;
    std::vector<fs::path> localSearchRoots_;
    bool localIndexBuilt_ = false;
    size_t externalSearches_ = 0;
    std::unordered_map<std::string, std::vector<fs::path>> localTexturesByBasename_;
    std::unordered_map<std::string, std::optional<fs::path>> cache_;
};

class TextureScanner {
public:
    ScanResult findUnusedTextures(const ScanOptions& options) const;
};

std::string toUtf8(const std::wstring& value);
std::wstring bytesToWideBestEffort(std::string_view value);
std::string pathToUtf8(const fs::path& path);

std::vector<fs::path> ShowModelFileDialog(std::chrono::milliseconds* elapsed = nullptr);
std::vector<fs::path> CollectInputModels(const std::vector<fs::path>& inputs);
TextureReferenceIndex BuildTextureReferenceIndex(const fs::path& root, const std::vector<fs::path>& modelFiles);
ScanResult FindUnusedTextures(const ScanOptions& options);
ProcessStats ProcessModelBatch(const std::vector<fs::path>& modelFiles, const ProcessOptions& options);
std::vector<ReplaceRule> LoadReplaceRules(const fs::path& programDirectory);

int Run(int argc, wchar_t* argv[]);

} // namespace texture_tool
