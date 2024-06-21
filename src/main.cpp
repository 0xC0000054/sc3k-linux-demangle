/*
* A utility that removes the name mangling from the debug symbol
* function names in the SimCity 3000 Unlimited Linux release.
*
* Copyright (C) 2024 Nicholas Hayes
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif // _WIN32

extern "C"
{
#include "demangle.h"
}

static const std::vector<std::pair<std::string, std::string>> ParameterSubstitutions
{
    // The demangler puts a space in front of a pointer or reference modifier.
    std::pair<std::string, std::string>(" &", "&"),
    std::pair<std::string, std::string>(" *", "*"),
    std::pair<std::string, std::string>(" **", "**"),
    // All unsigned values come first so that they are correctly handled.
    std::pair<std::string, std::string>("unsigned char", "uint8_t"),
    std::pair<std::string, std::string>("unsigned short", "uint16_t"),
    std::pair<std::string, std::string>("unsigned int", "uint32_t"),
    std::pair<std::string, std::string>("unsigned long", "uint32_t"),
    std::pair<std::string, std::string>("unsigned long long", "uint64_t"),
    std::pair<std::string, std::string>("char", "int8_t"),
    std::pair<std::string, std::string>("short", "int16_t"),
    std::pair<std::string, std::string>("int", "int32_t"),
    std::pair<std::string, std::string>("long", "int32_t"),
    std::pair<std::string, std::string>("long long", "int64_t"),
};

class DemanglerString
{
public:
    DemanglerString(char* ptr) : ptr(ptr)
    {
    }

    ~DemanglerString()
    {
        char* localPtr = ptr;
        ptr = nullptr;

        if (localPtr)
        {
            free(localPtr);
        }
    }

    const char* const Get() const noexcept
    {
        return ptr;
    }

private:
    char* ptr;
};

// Adapted from https://stackoverflow.com/a/24315631
static inline void DoFunctionParameterSubstitution(std::string& str, const std::string& from, const std::string& to)
{
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {

        if (start_pos > 0)
        {
            // Check that the term we are replacing is a whole word, this prevents a double replacement.
            // For example, uint32_t being converted to uint32_t32_t

            char previous = str[start_pos - 1];
            char next = str[start_pos + from.length() + 1];

            // The first character in 'from' is checked for the replacement strings that
            // strip a leading space from the &, * and ** operators.
            if ((previous == ' ' || previous == '(' || from[0] == ' ')
                && (next == ',' || next == ')' || next == ' ' || next == '\0'))
            {
                str.replace(start_pos, from.length(), to);
            }
        }

        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
}

static std::string GetDemangledLine(const char* const mangledLine)
{
    DemanglerString demangled(cplus_demangle(mangledLine, DMGL_PARAMS | DMGL_ANSI));

    std::string result(demangled.Get());

    for (const auto& item : ParameterSubstitutions)
    {
        DoFunctionParameterSubstitution(result, item.first, item.second);
    }

    return result;
}

static void DemangledInputFile(const std::filesystem::path& input, const std::filesystem::path& output)
{
    constexpr std::string_view ThunkPrefix = "__thunk_";

    std::ifstream in(input, std::ifstream::in);
    std::ofstream out(output, std::ofstream::out);

    size_t functionNameStart = 0;
    bool isGZUnknownClass = false;

    for (size_t lineIndex = 0; in.good(); lineIndex++)
    {
        std::string line;
        std::getline(in, line);

        if (line.starts_with(ThunkPrefix))
        {
            // The thunk prefix uses the format: __thunk_<unique number>_
            // The function name follows this prefix.

            size_t thunkPrefixEnd = line.find('_', ThunkPrefix.size() + 1);

            if (thunkPrefixEnd == std::string::npos)
            {
                throw std::runtime_error("Failed to find the end of the thunk prefix.");
            }

            line.erase(0, thunkPrefixEnd + 1);
        }

        const std::string result = GetDemangledLine(line.c_str());
        std::string_view resultAsStringView(result);

        if (lineIndex == 0)
        {
            // We strip the class name from the start of the function string
            // when writing it to the output.
            size_t index = result.find_first_of("::");

            if (index != std::string::npos)
            {
                functionNameStart = index + 2;
                isGZUnknownClass = resultAsStringView.substr(functionNameStart).compare("QueryInterface(uint32_t, void**)") == 0;

                // Write the class name at the top of the file.

                if (isGZUnknownClass)
                {
                    std::string className = result.substr(0, index);

                    if (className[0] == 'c')
                    {
                        // Change the class name to its interface form, the 'c' at the start of the name will be replaced with 'cI'.
                        // For example cSC3App will be converted to cISC3App.
                        className.replace(0, 1, "cI");
                    }

                    out << "#include \"cIGZUnknown.h\"" << std::endl << std::endl;
                    out << "class " << className << " : public cIGZUnknown" << std::endl;
                    out << '{' << std::endl;
                    out << "public:" << std::endl;

                    // We don't write the QueryInterface method to the file.
                    continue;
                }
                else
                {
                    out << "class " << resultAsStringView.substr(0, index) << std::endl;
                    out << '{' << std::endl;
                    out << "public:" << std::endl;
                }
            }
        }
        else if (lineIndex < 3 && isGZUnknownClass)
        {
            // We don't write the AddRef or Release methods to the file.
            continue;
        }

        out << "    virtual void* " << resultAsStringView.substr(functionNameStart) << " = 0;" << std::endl;

    }

    out << "};" << std::endl;
}

static std::filesystem::path GetTemporaryFilePath()
{
    std::filesystem::path path;

#ifdef _WIN32
    constexpr int GuidStringLength = 40;

    wchar_t guidString[GuidStringLength]{};
    GUID guid{};

    if (FAILED(CoCreateGuid(&guid)))
    {
        throw std::runtime_error("CoCreateGUID failed");
    }


    if (StringFromGUID2(guid, guidString, GuidStringLength) == 0)
    {
        throw std::runtime_error("StringFromGUID2 failed");
    }

    path = std::filesystem::temp_directory_path();
    path /= guidString;
    path.append(L".txt");
#else
#error "You must add temporary filename generation code for this platform."
#endif // WIN32

    return path;
}

int main(int nargs, char* argv[])
{
    if (nargs <  2 || nargs > 3)
    {
        std::cout << "Usage SC3KLinuxDemangle input.txt [output.txt]\nThe output file is optional, when it is omitted the input file will be overwritten." << std::endl;
        return 1;
    }

    try
    {
        bool overwriteInputFile = false;

        const std::filesystem::path inputFile = argv[1];
        std::filesystem::path outputFile;

        if (nargs == 3)
        {
            outputFile = argv[2];

            if (inputFile.compare(outputFile) == 0)
            {
                overwriteInputFile = true;
                outputFile = GetTemporaryFilePath();
            }
        }
        else
        {
            overwriteInputFile = true;
            outputFile = GetTemporaryFilePath();
        }

        DemangledInputFile(inputFile, outputFile);

        if (overwriteInputFile)
        {
            std::filesystem::copy_file(outputFile, inputFile, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::remove(outputFile);
        }
    }
    catch (const std::exception& e)
    {
        std::cout << e.what() << std::endl;
    }

    return 0;
}
