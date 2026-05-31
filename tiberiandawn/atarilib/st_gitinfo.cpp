#include "common/gitinfo.h"
#include <ctime>

const char GitSHA1[] = "unknown";
const char GitShortSHA1[] = "unknown";
const char GitCommitDate[] = "unknown";
const char GitCommitAuthorName[] = "unknown";
const char GitTag[] = "";
time_t GitCommitTimeStamp = 0;
bool GitUncommittedChanges = false;
bool GitHaveInfo = false;
int GitRevision = 0;
