/*

 MIT License

 Copyright (c) 2017 Chevy Ray Johnston

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.

 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

#include "binary.hpp"
#include "bitmap.hpp"
#include "cli.hpp"
#include "hash.hpp"
#include "options.hpp"
#include "packer.hpp"

#define EXIT_SKIPPED 2

using namespace std;
using namespace filesystem;

const int binVersion = 0;

static vector<Bitmap *> bitmaps;
static vector<Packer *> packers;

static void SplitFileName(const string &path, string *dir, string *name, string *ext)
{
    size_t si = path.rfind('/') + 1;
    if (si == string::npos)
        si = 0;
    size_t di = path.rfind('.');
    if (dir != nullptr)
    {
        if (si > 0)
            *dir = path.substr(0, si);
        else
            *dir = "";
    }
    if (name != nullptr)
    {
        if (di != string::npos)
            *name = path.substr(si, di - si);
        else
            *name = path.substr(si);
    }
    if (ext != nullptr)
    {
        if (di != string::npos)
            *ext = path.substr(di);
        else
            *ext = "";
    }
}

static string GetFileName(const string &path)
{
    string name;
    SplitFileName(path, nullptr, &name, nullptr);
    return name;
}

static string NormalizePath(const string &path)
{
    string str = path;
    replace(str.begin(), str.end(), '\\', '/');
    return str;
}

static void LoadBitmap(const string &prefix, const string &path)
{
    if (options.verbose)
        cout << '\t' << NormalizePath(path) << endl;

    bitmaps.push_back(new Bitmap(path, prefix + GetFileName(NormalizePath(path)), options.premultiply, options.trim));
}

static void LoadBitmaps(const string &root, const string &prefix)
{
    for (const auto &entry : directory_iterator(root))
    {
        auto &path = entry.path();
        if (entry.is_directory())
            LoadBitmaps(path.string(), prefix + path.filename().string() + '/');
        else if (path.extension().string() == ".png")
            LoadBitmap(prefix, path.string());
    }
}

static void RemoveFile(const string &file)
{
    remove(file.data());
}

static void GetSubdirs(const string &root, vector<string> &subdirs)
{
    for (const auto &entry : directory_iterator(root))
        if (entry.is_directory())
            subdirs.push_back(entry.path().string());
}

static void FindPackers(const string &root, const string &name, const string &ext, vector<string> &packers)
{
    for (const auto &entry : directory_iterator(root))
    {
        auto &path = entry.path();
        if (entry.is_regular_file() && path.string().starts_with(name) && path.extension().string() == ext)
            packers.push_back(path.string());
    }
}

static int Pack(size_t newHash, string &outputDir, string &name, vector<string> &inputs, string prefix = "")
{
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        if (inputs[i].rfind('.') == string::npos)
            HashFiles(newHash, inputs[i], options.useTimeForHash);
        else
            HashFile(newHash, inputs[i], options.useTimeForHash);
    }

    // Load the old hash
    size_t oldHash;
    if (LoadHash(oldHash, outputDir + name + ".hash"))
    {
        if (!options.force && newHash == oldHash)
        {
            if (!options.splitSubdirectories)
            {
                cout << "atlas is unchanged: " << name << endl;

                return EXIT_SUCCESS;
            }
            return EXIT_SKIPPED;
        }
    }

    // Remove old files
    RemoveFile(outputDir + name + ".hash");
    RemoveFile(outputDir + name + ".bin");
    RemoveFile(outputDir + name + ".xml");
    RemoveFile(outputDir + name + ".json");
    RemoveFile(outputDir + name + ".png");
    for (size_t i = 0; i < 16; ++i)
        RemoveFile(outputDir + name + to_string(i) + ".png");

    // Load the bitmaps from all the input files and directories
    if (options.verbose)
        cout << "loading images..." << endl;
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        if (!options.splitSubdirectories && inputs[i].rfind('.') != string::npos)
            LoadBitmap("", inputs[i]);
        else
            LoadBitmaps(inputs[i], prefix);
    }

    // Sort the bitmaps by area
    stable_sort(bitmaps.begin(), bitmaps.end(), [](const Bitmap *a, const Bitmap *b)
                { return (a->width * a->height) < (b->width * b->height); });

    // Pack the bitmaps
    while (!bitmaps.empty())
    {
        if (options.verbose)
            cout << "packing " << bitmaps.size() << " images..." << endl;
        auto packer = new Packer(options.width, options.height, options.padding);
        packer->Pack(bitmaps, options.unique, options.rotate);
        packers.push_back(packer);
        if (options.verbose)
            cout << "finished packing: " << name << (options.noZero && bitmaps.empty() ? "" : to_string(packers.size() - 1)) << " (" << packer->width << " x " << packer->height << ')' << endl;

        if (packer->bitmaps.empty())
        {
            cerr << "packing failed, could not fit bitmap: " << (bitmaps.back())->name << endl;
            return EXIT_FAILURE;
        }
    }

    bool noZero = options.noZero && packers.size() == 1;

    // Save the atlas image
    for (size_t i = 0; i < packers.size(); ++i)
    {
        string pngName = outputDir + name + (noZero ? "" : to_string(i)) + ".png";
        if (options.verbose)
            cout << "writing png: " << pngName << endl;
        packers[i]->SavePng(pngName);
    }

    // Save the atlas binary
    if (options.binary)
    {
        if (options.verbose)
            cout << "writing bin: " << outputDir << name << ".bin" << endl;

        ofstream bin(outputDir + name + ".bin", ios::binary);

        if (!options.splitSubdirectories)
        {
            WriteByte(bin, 'c');
            WriteByte(bin, 'r');
            WriteByte(bin, 'c');
            WriteByte(bin, 'h');
            WriteShort(bin, binVersion);
            WriteByte(bin, options.trim);
            WriteByte(bin, options.rotate);
            WriteByte(bin, (char)options.binaryStringFormat);
        }
        WriteShort(bin, (int16_t)packers.size());
        for (size_t i = 0; i < packers.size(); ++i)
            packers[i]->SaveBin(name + (noZero ? "" : to_string(i)), bin, options.trim, options.rotate);
        bin.close();
    }

    // Save the atlas xml
    if (options.xml)
    {
        if (options.verbose)
            cout << "writing xml: " << outputDir << name << ".xml" << endl;

        ofstream xml(outputDir + name + ".xml");
        if (!options.splitSubdirectories)
        {
            xml << "<atlas>" << endl;
            xml << "\t<trim>" << (options.trim ? "true" : "false") << "</trim>" << endl;
            xml << "\t<rotate>" << (options.rotate ? "true" : "false") << "</trim>" << endl;
        }
        for (size_t i = 0; i < packers.size(); ++i)
            packers[i]->SaveXml(name + (noZero ? "" : to_string(i)), xml, options.trim, options.rotate);
        if (!options.splitSubdirectories)
            xml << "</atlas>" << endl;
        xml.close();
    }

    // Save the atlas json
    if (options.json)
    {
        if (options.verbose)
            cout << "writing json: " << outputDir << name << ".json" << endl;

        ofstream json(outputDir + name + ".json");
        if (!options.splitSubdirectories)
        {
            json << '{' << endl;
            json << "\t\"trim\": " << (options.trim ? "true" : "false") << ',' << endl;
            json << "\t\"rotate\": " << (options.rotate ? "true" : "false") << ',' << endl;
            json << "\t\"textures\": [" << endl;
        }
        for (size_t i = 0; i < packers.size(); ++i)
        {
            packers[i]->SaveJson(name + (noZero ? "" : to_string(i)), json, options.trim, options.rotate);
            if (!options.splitSubdirectories)
            {
                if (i != packers.size() - 1)
                    json << ',';
                json << endl;
            }
        }
        if (!options.splitSubdirectories)
        {
            json << "\t]" << endl;
            json << '}' << endl;
        }
        json.close();
    }

    // Save the new hash
    SaveHash(newHash, outputDir + name + ".hash");

    return EXIT_SUCCESS;
}

int main(int argc, const char *argv[])
{
    PrintHelp(argc, argv);

    // Get the output directory and name
    string outputDir, name;
    SplitFileName(NormalizePath(argv[1]), &outputDir, &name, nullptr);

    // Get all the input files and directories
    vector<string> inputs;
    stringstream ss(argv[2]);
    while (ss.good())
    {
        string inputStr;
        getline(ss, inputStr, ',');
        inputs.push_back(NormalizePath(inputStr));
    }

    ParseArguments(argc, argv, 3);

    // Hash the arguments and input directories
    size_t newHash = 0;
    for (int i = 1; i < argc; ++i)
        HashString(newHash, argv[i]);

    if (!options.splitSubdirectories)
    {
        int result = Pack(newHash, outputDir, name, inputs);

        if (result != EXIT_SUCCESS)
            return result;

        return EXIT_SUCCESS;
    }

    string newInput, namePrefix;
    for (string &input : inputs)
    {
        if (!input.ends_with(".png"))
        {
            newInput = input;
            break;
        }
    }

    if (newInput.empty())
    {
        cerr << "could not find directories in input" << endl;
        return EXIT_FAILURE;
    }

    namePrefix = name + "_";

    vector<string> subdirs;
    GetSubdirs(newInput, subdirs);

    bool skipped = true;
    for (string &subdir : subdirs)
    {
        string newName = GetFileName(subdir), prefixedName = namePrefix + newName;
        vector<string> input{subdir};
        int result = Pack(newHash, outputDir, prefixedName, input, newName + "/");
        if (result == EXIT_SUCCESS)
            skipped = false;
        else if (result != EXIT_SKIPPED)
            return result;

        packers.clear();
        bitmaps.clear();
    }

    if (skipped)
    {
        cout << "atlas is unchanged: " << name << endl;

        return EXIT_SUCCESS;
    }

    RemoveFile(outputDir + name + ".bin");
    RemoveFile(outputDir + name + ".xml");
    RemoveFile(outputDir + name + ".json");

    vector<string> cachedPackers;
    if (options.binary)
    {
        if (options.verbose)
            cout << "writing bin: " << outputDir << name << ".bin" << endl;

        vector<ifstream *> cacheFiles;

        FindPackers(outputDir, namePrefix, "bin", cachedPackers);

        ofstream bin(outputDir + name + ".bin", ios::binary);
        WriteByte(bin, 'c');
        WriteByte(bin, 'r');
        WriteByte(bin, 'c');
        WriteByte(bin, 'h');
        WriteShort(bin, binVersion);
        WriteByte(bin, options.trim);
        WriteByte(bin, options.rotate);
        WriteByte(bin, (char)options.binaryStringFormat);
        int16_t imageCount = 0;
        for (size_t i = 0; i < cachedPackers.size(); ++i)
        {
            ifstream binCache(cachedPackers[i], ios::binary);
            imageCount += ReadShort(binCache);
            binCache.close();
        }
        WriteShort(bin, imageCount);
        for (size_t i = 0; i < cachedPackers.size(); ++i)
        {
            ifstream binCache(cachedPackers[i], ios::binary);
            ReadShort(binCache);
            bin << binCache.rdbuf();
            binCache.close();
        }
        bin.close();
    }

    if (options.xml)
    {
        if (options.verbose)
            cout << "writing xml: " << outputDir << name << ".xml" << endl;

        cachedPackers.clear();

        FindPackers(outputDir, namePrefix, "xml", cachedPackers);

        ofstream xml(outputDir + name + ".xml");
        xml << "<atlas>" << endl;
        xml << "\t<trim>" << (options.trim ? "true" : "false") << "</trim>" << endl;
        xml << "\t<rotate>" << (options.rotate ? "true" : "false") << "</trim>" << endl;
        for (size_t i = 0; i < cachedPackers.size(); ++i)
        {
            ifstream xmlCache(cachedPackers[i]);
            xml << xmlCache.rdbuf();
            xmlCache.close();
        }
        xml << "</atlas>" << endl;
        xml.close();
    }

    if (options.json)
    {
        if (options.verbose)
            cout << "writing json: " << outputDir << name << ".json" << endl;

        cachedPackers.clear();

        FindPackers(outputDir, namePrefix, "json", cachedPackers);

        ofstream json(outputDir + name + ".json");
        json << '{' << endl;
        json << "\t\"trim\": " << (options.trim ? "true" : "false") << ',' << endl;
        json << "\t\"rotate\": " << (options.rotate ? "true" : "false") << ',' << endl;
        json << "\t\"textures\": [" << endl;
        for (size_t i = 0; i < cachedPackers.size(); ++i)
        {
            ifstream jsonCache(cachedPackers[i]);
            json << jsonCache.rdbuf();
            jsonCache.close();
            if (i != cachedPackers.size() - 1)
                json << ',';
            json << endl;
        }
        json << "\t]" << endl;
        json << '}' << endl;
        json.close();
    }

    return EXIT_SUCCESS;
}
