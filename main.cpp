#include "compiler.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;
using namespace std;

static string readFile(const fs::path& path)
{
    ifstream file(path);
    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main()
{

    auto source = readFile(fs::path("main.to"));

    Compiler compiler;

    auto res = compiler.compile("main.to", source);

    compiler.generateObject(*res.mod);

    return 0;
}