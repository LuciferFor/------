#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <conio.h> // 用于 _getch() 函数
#include <windows.h>
#include "BLP.hpp"

namespace fs = std::filesystem;

// 获取用户选择的多个文件
std::vector<std::string> openMultipleFiles() {
	// 缓冲区，用于存储用户选择的文件路径
	char buffer[8192] = { 0 }; // 注意：要足够大以支持多选路径存储
	OPENFILENAME ofn;          // 文件打开对话框结构体
	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;       // 父窗口句柄，NULL 表示没有父窗口
	ofn.lpstrFile = buffer;     // 缓冲区，用于存储路径
	ofn.nMaxFile = sizeof(buffer);
	ofn.lpstrFilter = "MDX and MDL Files\0*.mdx;*.mdl\0All Files\0*.*\0\0";
	ofn.nFilterIndex = 1;       // 默认选择第一个过滤器
	ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER; // 多选支持，现代界面

	// 调用文件选择对话框
	if (GetOpenFileName(&ofn)) {
		std::vector<std::string> files;

		// 文件路径解析
		std::string directory = buffer; // 获取目录部分
		char* file = buffer + directory.size() + 1; // 文件名部分

		if (*file == '\0') {
			// 如果没有多选，直接返回单文件路径
			files.push_back(directory);
		}
		else {
			// 处理多选
			while (*file) {
				files.push_back(directory + "\\" + file); // 组合目录和文件名
				file += strlen(file) + 1;                 // 移动到下一个文件名
			}
		}
		return files;
	}
	else {
		// 如果用户取消或发生错误，返回空的 vector
		return {};
	}
}

// 读取文件内容
int ReadFile(std::string url, std::string& text) {
	std::ifstream file(url);

	if (!file.is_open()) {
		return -1;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	text = buffer.str();

	file.close();

	return 0;
}

// 将字符串转换小写
std::string ToLowerCase(const std::string& input) {
	std::string result = input;
	std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
		return std::tolower(c);
		});
	return result;
}

struct TextureEntry {
	std::string filePath;
	int replaceableId;
	int flags;
};

// 字符串查询
int FindString(const std::string& str, const std::string& target) {
	std::size_t pos = str.find(target);
	return (pos != std::string::npos) ? static_cast<int>(pos) : -1;
}

// 获取文件大小
long GetFileLength(const char* url) {
	FILE* pFile = NULL;
	long size = 0;

	pFile = fopen(url, "rb");
	if (pFile != NULL) {
		fseek(pFile, 0, SEEK_END);   ///将文件指针移动文件结尾
		size = ftell(pFile); ///求出当前文件指针距离文件开始的字节数
		fclose(pFile);
	}

	return size;
}

//获取文件的名称
void GetFileUrlName(std::string path, std::string& name) {
	for (int i = path.size() - 1; i > 0; i--)
	{
		if (path[i] == '\\' || path[i] == '/')
		{
			name = path.substr(i + 1);
			return;
		}
	}
	name = path;
}

// 复制文件
bool CopyFileSync(const std::string& srcPath, const std::string& destPath) {
	if (CopyFile(srcPath.c_str(), destPath.c_str(), FALSE)) {
		return true;  // 成功复制
	}
	else {
		std::cerr << "Error copying file: " << GetLastError() << std::endl;
		return false;  // 复制失败
	}
}

// 字符串拆分函数
std::vector<std::string> splitString(const std::string& input, const std::string& delimiter) {
	std::vector<std::string> result;
	std::string temp;
	size_t delimiterLength = delimiter.length();

	for (size_t i = 0; i < input.length(); ++i) {
		bool isDelimiter = false;

		// 检查是否匹配分隔符
		if (delimiterLength == 1) {
			isDelimiter = (input[i] == delimiter[0]);
		}
		else if (input.substr(i, delimiterLength) == delimiter) {
			isDelimiter = true;
			i += delimiterLength - 1; // 跳过整个分隔符
		}

		if (isDelimiter) {
			if (!temp.empty()) {
				result.push_back(temp);  // 将当前部分添加到结果中
				temp.clear();            // 清空临时字符串
			}
		}
		else {
			temp += input[i];  // 添加字符到临时字符串
		}
	}

	if (!temp.empty()) {
		result.push_back(temp);  // 添加最后一部分
	}

	return result;
}

// 获取当前程序的目录
std::string getCurrentProgramDirectory() {
	char buffer[MAX_PATH]; // 存储路径
	// 获取当前程序的完整路径
	DWORD length = GetModuleFileName(NULL, buffer, MAX_PATH);
	if (length == 0) {
		throw std::runtime_error("Failed to get the program directory.");
	}

	// 将路径转换为 std::string
	std::string fullPath(buffer);

	// 找到最后一个 '\\'，去掉文件名部分，保留目录
	size_t lastSlashPos = fullPath.find_last_of("\\/");
	if (lastSlashPos != std::string::npos) {
		return fullPath.substr(0, lastSlashPos);
	}

	return fullPath; // 如果没有斜杠（极少情况），返回原始路径
}

std::string executeCommandWithPipe(const std::string& command) {
	// 创建管道
	HANDLE hRead, hWrite;
	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
	if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
		throw std::runtime_error("Failed to create pipe");
	}

	// 配置启动信息
	STARTUPINFOA si = { sizeof(STARTUPINFOA) };
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = hWrite;
	si.hStdError = hWrite;

	PROCESS_INFORMATION pi = {};
	if (!CreateProcessA(nullptr, const_cast<char*>(command.c_str()), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
		CloseHandle(hRead);
		CloseHandle(hWrite);
		throw std::runtime_error("Failed to create process");
	}

	// 关闭写入端
	CloseHandle(hWrite);

	// 读取输出
	char buffer[4096];
	std::string result;
	DWORD bytesRead;
	while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
		buffer[bytesRead] = '\0';
		result += buffer;
	}

	// 等待进程结束
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	CloseHandle(hRead);

	return result;
}

// mdx检索blp类
class MDXTextureParser {
private:
	std::string filename;
	std::vector<TextureEntry> textures;

public:
	bool parseMDX(const std::string& mdxFilename) {
		filename = mdxFilename;
		std::ifstream file(filename, std::ios::binary);
		if (!file.is_open()) {
			//std::cerr << "无法打开文件: " << filename << std::endl;
			return false;
		}

		char header[4];
		file.read(header, 4);
		if (strncmp(header, "MDLX", 4) != 0) {
			//std::cerr << "不是有效的MDX文件" << std::endl;
			return false;
		}

		textures.clear();
		while (!file.eof()) {
			char chunkHeader[4];
			int chunkSize;

			file.read(chunkHeader, 4);
			file.read(reinterpret_cast<char*>(&chunkSize), 4);

			if (strncmp(chunkHeader, "TEX", 3) == 0) {
				int textureCount = chunkSize / 268; // 每个纹理条目268字节

				for (int i = 0; i < textureCount; ++i) {
					TextureEntry texture;

					// 读取可替换ID
					file.read(reinterpret_cast<char*>(&texture.replaceableId), 4);

					// 读取文件路径（260字节）
					char filepath[261] = { 0 };
					file.read(filepath, 260);
					texture.filePath = filepath;

					// 读取标志
					file.read(reinterpret_cast<char*>(&texture.flags), 4);

					textures.push_back(texture);
				}
				break; // 只处理纹理块
			}
			else {
				// 跳过不需要的块
				file.seekg(chunkSize, std::ios::cur);
			}
		}

		return true;
	}

	void printTextures() {
		std::cout << "模型引用的纹理贴图：" << std::endl;
		for (const auto& texture : textures) {
			if (!texture.filePath.empty()) {
				std::cout << "纹理路径: " << texture.filePath << std::endl;
				std::cout << "------------------------" << std::endl;
			}
		}
	}

	// 将所有不带前缀的纹理统统加上war3mapimported
	bool addPrefix(std::string prefix) {
		// 查找并替换匹配的纹理路径
		bool pathModified = false;
		for (auto& texture : textures) {
			if (texture.filePath.length() > 0) {
				if (FindString(texture.filePath, "\\") == -1) {
					texture.filePath = prefix + "\\" + texture.filePath;
					pathModified = true;
				}
			}
		}
		return pathModified;
	}

	// 将所有纹理导出到当前目录
	int exportAllTexture(std::string path) {
		std::string cmd;
		for (auto& texture : textures) {
			std::string fileName;
			if (FindString(ToLowerCase(texture.filePath), ToLowerCase(path)) != -1) {
				GetFileUrlName(texture.filePath, fileName);
				if (fileName.length() > 0) {
					cmd = "es.exe -w \"" + fileName + "\"";
					std::string retStr = executeCommandWithPipe(cmd.c_str());
					std::vector<std::string> retList = splitString(retStr, "\r\n");
					std::string minFilePath;
					for (auto& ret : retList) {
						if (BLPValidator::isValidBLPFile(ret)) {
							if (GetFileLength(ret.c_str()) < GetFileLength(minFilePath.c_str()) || minFilePath.empty()) {
								minFilePath = ret;
							}
						}
					}
					CopyFileSync(minFilePath, fileName);
				}
			}
		}
		return 0;
	}

	// 修改贴图纹理路径
	bool modifyTexturePath(const std::string& oldPath, const std::string& newPath) {
		// 检查新路径长度是否超过260字节限制
		if (newPath.length() > 260) {
			std::cerr << "新的纹理路径长度超过260字节限制" << std::endl;
			return false;
		}

		// 查找并替换匹配的纹理路径
		bool pathModified = false;
		for (auto& texture : textures) {
			if (texture.filePath == oldPath) {
				texture.filePath = newPath;
				pathModified = true;
			}
		}

		if (!pathModified) {
			std::cerr << "未找到匹配的纹理路径: " << oldPath << std::endl;
			return false;
		}

		// 重写整个MDX文件
		return rewriteMDXFile();
	}

	// 替换指定所有纹理路径
	bool replaceAllTexturePaths(const std::string& searchPath, const std::string& replacementPath) {
		bool anyPathModified = false;
		for (auto& texture : textures) {
			if (texture.filePath.find(searchPath) != std::string::npos) {
				// 替换路径中包含searchPath的部分
				size_t pos = texture.filePath.find(searchPath);
				texture.filePath.replace(pos, searchPath.length(), replacementPath);
				anyPathModified = true;
			}
		}

		if (!anyPathModified) {
			std::cerr << "未找到匹配的纹理路径: " << searchPath << std::endl;
			return false;
		}

		// 重写整个MDX文件
		return rewriteMDXFile();
	}

	// 判断模型是否引用了某个纹理
	bool checkTexture(const std::string& path) {
		for (const auto& texture : textures) {
			if (!texture.filePath.empty()) {
				if (ToLowerCase(texture.filePath) == ToLowerCase(path)) {
					return true;
				}
			}
		}
		return false;
	}

	bool rewriteMDXFile() {
		// 读取整个原始文件内容
		std::ifstream inputFile(filename, std::ios::binary);
		if (!inputFile.is_open()) {
			std::cerr << "无法打开原始文件" << std::endl;
			return false;
		}

		// 创建临时文件
		std::string tempFilename = filename + ".tmp";
		std::ofstream outputFile(tempFilename, std::ios::binary);
		if (!outputFile.is_open()) {
			std::cerr << "无法创建临时文件" << std::endl;
			return false;
		}

		// 首先复制文件头
		char header[4];
		inputFile.read(header, 4);
		outputFile.write(header, 4);

		bool texChunkFound = false;
		while (!inputFile.eof()) {
			char chunkHeader[4] = { 0 };
			int chunkSize = 0;

			// 读取块头
			inputFile.read(chunkHeader, 4);
			if (inputFile.gcount() < 4) break;  // 文件读取结束

			inputFile.read(reinterpret_cast<char*>(&chunkSize), 4);

			// 写入块头和块大小
			outputFile.write(chunkHeader, 4);
			outputFile.write(reinterpret_cast<char*>(&chunkSize), 4);

			// 检查是否是纹理块
			if (strncmp(chunkHeader, "TEX", 3) == 0) {
				// 写入修改后的纹理块
				for (const auto& texture : textures) {
					// 写入可替换ID
					outputFile.write(reinterpret_cast<const char*>(&texture.replaceableId), 4);

					// 写入文件路径（固定260字节）
					char filepath[261] = { 0 };
					strncpy(filepath, texture.filePath.c_str(), 260);
					outputFile.write(filepath, 260);

					// 写入标志
					outputFile.write(reinterpret_cast<const char*>(&texture.flags), 4);
				}

				// 跳过原始文件中的纹理块
				inputFile.seekg(chunkSize, std::ios::cur);
				texChunkFound = true;
			}
			else {
				// 复制其他块的内容
				std::vector<char> chunkBuffer(chunkSize);
				inputFile.read(chunkBuffer.data(), chunkSize);
				outputFile.write(chunkBuffer.data(), chunkSize);
			}
		}

		inputFile.close();
		outputFile.close();

		// 替换原始文件
		std::filesystem::rename(tempFilename, filename);

		return texChunkFound;
	}
};

// 截取路径中的特定部分
std::string ExtractSubPath(const std::string& fullPath, const std::string& keyword) {
	// 查找关键字的位置
	size_t pos = fullPath.find(keyword);
	if (pos != std::string::npos) {
		// 返回从关键字开始的子路径
		return fullPath.substr(pos);
	}
	else {
		// 如果关键字未找到，返回原始路径
		return fullPath;
	}
}

//获取文件后缀
std::string GetFileExtension(const std::string& filePath) {
	size_t lastDotPos = filePath.find_last_of(".");
	if (lastDotPos != std::string::npos) {
		return filePath.substr(lastDotPos);
	}
	return ""; // 如果找不到点（.）则返回空字符串
}

// 判断某个blp是否被mdx引用
bool CheckRefPathMdx(std::string directory, std::string blpPath) {
	std::vector<std::string> filenames;

	fs::path dirPath(directory);

	if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
		std::cerr << "Invalid directory: " << directory << std::endl;
		return false;
	}

	// 递归遍历目录
	for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
		if (entry.is_regular_file()) { // 仅处理文件
			filenames.push_back(entry.path().string()); // 完整路径
		}
	}

	// 打印结果
	for (const auto& name : filenames) {
		if (ToLowerCase(GetFileExtension(name)) == ".mdx") {
			MDXTextureParser parser;
			if (parser.parseMDX(name)) {
				if (parser.checkTexture(blpPath)) {
					return true;
				}
			}
			else {
				//std::cout << "模型" << name << "无法打开" << std::endl;
				continue;
			}
		}
	}

	return false;
}

// 遍历所有blp
void ListFilesInDirectory(const std::string& directory) {
	std::vector<std::string> filenames;
	int i = 0;
	int count = 0;
	uintmax_t size = 0;
	fs::path dirPath(directory);

	if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
		std::cerr << "Invalid directory: " << directory << std::endl;
		return;
	}

	// 递归遍历目录
	//for (const auto& entry : fs::recursive_directory_iterator(dirPath)) {
	//	if (entry.is_regular_file()) { // 仅处理文件
	//		filenames.push_back(entry.path().string()); // 完整路径
	//	}
	//}

	// 遍历指定目录（不递归）
	for (const auto& entry : fs::directory_iterator(dirPath)) {
		if (entry.is_regular_file()) { // 仅处理文件
			filenames.push_back(entry.path().string()); // 完整路径
		}
	}

	// 打印结果
	for (const auto& name : filenames) {
		std::string pname = ToLowerCase(GetFileExtension(name));
		if (pname == ".blp" || pname == ".tga") {
			std::string fileName = ExtractSubPath(name, "war3mapimported");
			if (!CheckRefPathMdx(directory, fileName)) {
				std::cout << "未被引用(" << i << "/" << filenames.size() << "):" << fileName << std::endl;
				++count;
				size += fs::file_size(name);
			}
		}
		++i;
	}

	std::cout << "文件总数:" << filenames.size() << std::endl;
	std::cout << "共有" << count << "个文件未被引用,共计大小(" << (size / 1024 / 1024) << "M)" << std::endl;
}

int main(int argc, char* argv[]) {
	//ListFilesInDirectory("war3mapimported\\");
	//while (true) {
	//	// 使用 _getch() 获取用户按键输入，不需要回车确认
	//	char key = _getch();

	//	if (key == 'q' || key == 'Q') { // 用户按下 'q' 或 'Q' 键退出
	//		std::cout << "Exiting program...\n";
	//		break;
	//	}
	//	else {
	//		std::cout << "You pressed: " << key << ". Press 'q' to quit.\n";
	//	}
	//}
	std::vector<std::string> files = openMultipleFiles();
	std::string replaceStr;
	std::string currentPath = getCurrentProgramDirectory();
	ReadFile(currentPath +"\\replace.txt", replaceStr);
	std::vector<std::string> replaceList;
	// 即将替换的列表
	if (FindString(replaceStr, "\r\n") != -1) {
		replaceList = splitString(replaceStr, "\r\n");
	}
	else {
		replaceList = splitString(replaceStr, "\n");
	}
	for (auto& par : files) {
		bool isReplace = false;
		MDXTextureParser parser;
		if (parser.parseMDX(par)) {
			// 增加前缀
			if (parser.addPrefix("war3mapimported")) {
				isReplace = true;
			}
			// 替换前缀
			for (auto ret : replaceList) {
				std::vector<std::string> replaceList = splitString(ret, "->");
				parser.replaceAllTexturePaths(replaceList[0], replaceList[1]);
				isReplace = true;
			}
			if (isReplace) {
				parser.rewriteMDXFile();
			}
			parser.exportAllTexture("war3mapimported");
		}
	}
	return 0;
}