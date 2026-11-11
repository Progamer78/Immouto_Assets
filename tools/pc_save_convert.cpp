// Read-only Windows save conversion. Original .sav files are never modified.
#include <windows.h>
#include <algorithm>
#include <map>
#include <fstream>
#include <iostream>
#include "wolfsave/WolfSaveParser.h"
#include "pc_progress.hpp"

int wmain(int argc, wchar_t** argv) {
    int result=0;
    try {
        if (argc<2) throw std::runtime_error("Drag SaveDataNN.sav onto this program, or pass its filename on the command line.");
        for (int i=1;i<argc;++i) {
            const auto source=std::filesystem::absolute(argv[i]);
            const auto length=std::filesystem::file_size(source);
            if (length<32 || length>32*1024*1024) throw std::runtime_error("Unsupported save size");
            WolfSaveParser parser;
            if (!parser.Parse(source.wstring())) throw std::runtime_error("Unable to read this PC save");
            auto converted=wolf::convert_pc_dump(parser.ProgressData());
            auto output=source;output.replace_extension(L".pc-save.json");
            if (std::filesystem::exists(output)) throw std::runtime_error("The converted output already exists; move or rename it before converting again");
            std::ofstream file(output,std::ios::binary);file<<converted.dump();file.close();
            if (!file) throw std::runtime_error("Unable to write converted save");
            std::wcout<<L"Created "<<output<<L"\n";
        }
        std::cout<<"Copy the .pc-save.json file to Monochrome/imports in iPhone Files.\nIn the game: Menu > Import PC save. Existing slots are kept.\n";
    } catch (const std::exception& error) { std::cerr<<"Conversion failed: "<<error.what()<<"\n";result=1; }
    DWORD processes[2]; if (GetConsoleProcessList(processes,2)==1) {std::cout<<"Press Enter to close.\n";std::cin.get();}
    return result;
}
