#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "file.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <fnmatch.h>

class Find_File_Data_Posix : public Find_File_Data
{
public:
    Find_File_Data_Posix();
    virtual ~Find_File_Data_Posix();

    virtual const char* GetName() const;
    virtual const char* GetFullName() const
    {
        return DirEntry != nullptr ? FullName : nullptr;
    }
    virtual unsigned int GetTime() const;

    virtual bool FindFirst(const char* fname);
    virtual bool FindNext();
    virtual void Close();

private:
    DIR* Directory;
    struct dirent* DirEntry;
    const char* FileFilter;
    char FullName[PATH_MAX];
    char DirName[PATH_MAX];

    bool FindNextWithFilter();
};

Find_File_Data_Posix::Find_File_Data_Posix()
    : Directory(nullptr)
    , DirEntry(nullptr)
{
}

Find_File_Data_Posix::~Find_File_Data_Posix()
{
    Close();
}

const char* Find_File_Data_Posix::GetName() const
{
    if (DirEntry == nullptr) {
        return nullptr;
    }
    return DirEntry->d_name;
}

unsigned int Find_File_Data_Posix::GetTime() const
{
    if (DirEntry == nullptr) {
        return 0;
    }
    struct stat buf = {0};
    if (stat(FullName, &buf) != 0) {
        return 0;
    }
#ifdef LIBCMINI
    /*
     * libcmini writes a GEMDOS _DOSTIME into st_mtime (time word, then date word).
     * On m68k that long is (time << 16) | date, so swap the halves and the date sorts first.
     * https://github.com/freemint/libcmini/blob/master/sources/stat.c
     */
    unsigned int raw = (unsigned int)buf.st_mtime;
    return (raw << 16) | (raw >> 16);
#else
    return (unsigned int)buf.st_mtime;
#endif
}

bool Find_File_Data_Posix::FindNextWithFilter()
{
    while (true) {
        DirEntry = readdir(Directory);
        if (DirEntry == nullptr) {
            return false;
        }
        if (fnmatch(FileFilter, DirEntry->d_name, FNM_PATHNAME | FNM_CASEFOLD) == 0) {
            strcpy(FullName, DirName);
            strcat(FullName, DirEntry->d_name);
            break;
        }
    }
    return true;
}

bool Find_File_Data_Posix::FindFirst(const char* fname)
{
    Close();
    FullName[0] = '\0';
    DirName[0] = '\0';

    // split directory and file from the path
    char* fdir = strrchr((char*)fname, '/');
    if (fdir != nullptr) {
        strncat(DirName, fname, (fdir - fname + 1));
        FileFilter = fdir + 1;
        Directory = opendir(DirName);
    } else {
        FileFilter = fname;
        Directory = opendir(".");
    }

    if (Directory == nullptr) {
        return false;
    }

    return FindNextWithFilter();
}

bool Find_File_Data_Posix::FindNext()
{
    if (Directory == nullptr) {
        return false;
    }
    return FindNextWithFilter();
}

void Find_File_Data_Posix::Close()
{
    if (Directory != nullptr) {
        closedir(Directory);
        Directory = nullptr;
    }
}

Find_File_Data* Find_File_Data::CreateFindData()
{
    return new Find_File_Data_Posix();
}
