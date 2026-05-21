#include "TextureTool.hpp"

#include <Windows.h>

#include <iostream>

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    try {
        return texture_tool::Run(argc, argv);
    }
    catch (const std::exception& ex) {
        std::cerr << "程序异常: " << ex.what() << std::endl;
        return 1;
    }
}
