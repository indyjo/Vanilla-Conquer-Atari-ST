// TiberianDawn.DLL and RedAlert.dll and corresponding source code is free
// software: you can redistribute it and/or modify it under the terms of
// the GNU General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.

// TiberianDawn.DLL and RedAlert.dll and corresponding source code is distributed
// in the hope that it will be useful, but with permitted additional restrictions
// under Section 7 of the GPL. See the GNU General Public License in LICENSE.TXT
// distributed with this program. You should have received a copy of the
// GNU General Public License along with permitted additional restrictions
// with this program. If not, see https://github.com/electronicarts/CnC_Remastered_Collection

/*
 * Minimal path helpers for libcmini / bare TOS builds (LIBCMINI).
 *
 * Game data, config, and saves all live in the GEMDOS current directory (e.g. C:\CNC
 * under Hatari).  Use plain relative names — never re-prefix getcwd()'s "C:\CNC" onto
 * filenames before fopen(), or Hatari hostfs mis-parses the drive letter.
 */

#include "paths.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace
{
    bool Is_Dot_Path(const char* path)
    {
        return path != nullptr && path[0] == '.' && path[1] == '\0';
    }

    bool Is_Empty_Path(const char* path)
    {
        return path == nullptr || path[0] == '\0';
    }
} // namespace

const char* PathsClass::Program_Path()
{
    if (ProgramPath.empty()) {
        ProgramPath = ".";
    }

    return ProgramPath.c_str();
}

const char* PathsClass::Data_Path()
{
    if (DataPath.empty()) {
        DataPath = ".";
    }

    return DataPath.c_str();
}

const char* PathsClass::User_Path()
{
    if (UserPath.empty()) {
        UserPath = ".";
    }

    return UserPath.c_str();
}

bool PathsClass::Create_Directory(const char* dirname)
{
    if (Is_Empty_Path(dirname) || Is_Dot_Path(dirname)) {
        return true;
    }

    bool ret = true;
    std::string temp(dirname);
    size_t pos = 0;

    do {
        pos = temp.find_first_of("/\\", pos + 1);
        std::string part = temp.substr(0, pos);

        if (part.empty() || Is_Dot_Path(part.c_str())) {
            continue;
        }

        if (mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) {
            ret = false;
            break;
        }
    } while (pos != std::string::npos);

    return ret;
}

bool PathsClass::Is_Absolute(const char* path)
{
    if (Is_Empty_Path(path)) {
        return false;
    }

    if (path[0] == '\\') {
        return true;
    }

    return path[1] == ':';
}

std::string PathsClass::Concatenate_Paths(const char* path1, const char* path2)
{
    if (Is_Empty_Path(path2)) {
        return Is_Empty_Path(path1) ? std::string() : std::string(path1);
    }

    if (Is_Absolute(path2)) {
        return std::string(path2);
    }

    if (Is_Empty_Path(path1) || Is_Dot_Path(path1)) {
        return std::string(path2);
    }

    size_t len = std::strlen(path1);
    if (len > 0 && path1[len - 1] == SEP) {
        return std::string(path1) + path2;
    }

    return std::string(path1) + SEP + path2;
}

std::string PathsClass::Get_Filename(const char* path)
{
    if (Is_Empty_Path(path)) {
        return std::string();
    }

    const char* base = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }

    return std::string(base);
}

std::string PathsClass::Argv_Path(const char* cmd_arg)
{
    if (Is_Empty_Path(cmd_arg)) {
        return ".";
    }

    const char* slash = std::strrchr(cmd_arg, '\\');
    const char* alt = std::strrchr(cmd_arg, '/');
    if (alt != nullptr && (slash == nullptr || alt > slash)) {
        slash = alt;
    }

    if (slash == nullptr) {
        return ".";
    }

    if (slash == cmd_arg) {
        return std::string(1, SEP);
    }

    return std::string(cmd_arg, slash);
}
