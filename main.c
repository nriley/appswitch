/*
 appswitch - a command-line application switcher
 Nicholas Riley <appswitch@sabi.net>

 Copyright (c) 2003, Nicholas Riley
 All rights reserved.

 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of this software nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#define DEBUG 0

#include <unistd.h>
#include "CPS.h"

const char *APP_NAME;

#define VERSION "1.0d1"

struct {
    OSType creator;
    CFStringRef bundleID;
    char *name;
    pid_t pid;
    char *path;
    enum {
        MATCH_UNKNOWN, MATCH_CREATOR, MATCH_BUNDLE_ID, MATCH_NAME, MATCH_PID, MATCH_PATH
    } matchType;
} OPTS =
{
    kLSUnknownCreator, NULL, NULL, -1, NULL, MATCH_UNKNOWN
};

typedef struct {
    OSStatus status;
    const char *desc;
} errRec, errList[];

static errList ERRS = {
    // Process Manager errors
    { appIsDaemon, "application is background-only\n", },
    { procNotFound, "unable to connect to system service.\nAre you logged in?" },
    // CoreGraphics errors
    { kCGErrorIllegalArgument, "window server error.\nAre you logged in?" },
    { fnfErr, "file not found" },
    { 0, NULL }
};

void usage() {
    fprintf(stderr, "usage: %s [-c creator] [-i bundleID] [-a name] [-p pid] [path]\n"
            "  -c creator    match application by four-character creator code ('ToyS')\n"
            "  -i bundle ID  match application by bundle identifier (com.apple.scripteditor)\n"
            "  -p pid        match application by process identifier [slower]\n"
            "  -a name       match application by name\n", APP_NAME);
    fprintf(stderr, "appswitch "VERSION" (c) 2003 Nicholas Riley <http://web.sabi.net/nriley/software/>.\n"
            "Please send bugs, suggestions, etc. to <appswitch@sabi.net>.\n");

    exit(1);
}

char *osstatusstr(OSStatus err) {
    errRec *rec;
    const char *errDesc = "unknown error";
    char * const failedStr = "(unable to retrieve error message)";
    static char *str = NULL;
    size_t len;
    if (str != NULL && str != failedStr) free(str);
    for (rec = &(ERRS[0]) ; rec->status != 0 ; rec++)
        if (rec->status == err) {
            errDesc = rec->desc;
            break;
        }
            len = strlen(errDesc) + 10 * sizeof(char);
    str = (char *)malloc(len);
    if (str != NULL)
        snprintf(str, len, "%s (%ld)", errDesc, err);
    else
        str = failedStr;
    return str;
}

void osstatusexit(OSStatus err, const char *fmt, ...) {
    va_list ap;
    const char *errDesc = osstatusstr(err);
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", APP_NAME);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, ": %s\n", errDesc);
    exit(1);
}

void errexit(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", APP_NAME);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

void getargs(int argc, char * const argv[]) {
    extern char *optarg;
    extern int optind;
    int ch;

    if (argc == 1) usage();

    const char *opts = "c:i:p:a:";
    
    ch = getopt(argc, argv, opts);

    switch (ch) {
        case 'p':
            if (sscanf(optarg, "%d", &OPTS.pid) != 1 || OPTS.pid < 0)
                errexit("invalid process identifier (argument of -p)");
            OPTS.matchType = MATCH_PID;
            break;
        case 'c':
            if (strlen(optarg) != 4) errexit("creator (argument of -c) must be four characters long");
            OPTS.creator = *(OSTypePtr)optarg;
            OPTS.matchType = MATCH_CREATOR;
            break;
        case 'i':
            OPTS.bundleID = CFStringCreateWithCString(NULL, optarg, CFStringGetSystemEncoding());
            OPTS.matchType = MATCH_BUNDLE_ID;
            break;
        case 'a':
            OPTS.name = strdup(optarg);
            OPTS.matchType = MATCH_NAME;
            break;
        case -1:
            break;
        default: usage();
    }

    if (getopt(argc, argv, opts) != -1)
        errexit("choose only one of -c, -i, -u, -p, -a options");

    argc -= optind;
    argv += optind;

    if (OPTS.matchType != MATCH_UNKNOWN && argc != 0) usage();

    if (OPTS.matchType == MATCH_UNKNOWN) {
        if (argc != 1) usage();
        OPTS.path = argv[0];
        OPTS.matchType = MATCH_PATH;
    }
}

int main (int argc, char * const argv[]) {
    OSStatus err;

    APP_NAME = argv[0];
    getargs(argc, argv);
    
    long pathMaxLength = pathconf("/", _PC_PATH_MAX);
    long nameMaxLength = pathconf("/", _PC_NAME_MAX);

    char *path = (char *)malloc(pathMaxLength);
    char *name = (char *)malloc(nameMaxLength);;

    if (path == NULL || name == NULL) errexit("can't allocate memory for path or filename buffer");

    // need to establish connection with window server
    InitCursor();
    
    CPSProcessSerNum psn = {
        kNoProcess, kNoProcess
    };
    CPSProcessInfoRec info;
    int len;
    while ( (err = CPSGetNextProcess(&psn)) == noErr) {
        err = CPSGetProcessInfo(&psn, &info, path, pathMaxLength, &len, name, nameMaxLength);
        if (err != noErr) osstatusexit(err, "can't get information for process PSN %ld.%ld", psn.hi, psn.lo);

#if DEBUG
        fprintf(stderr, "%ld.%ld: %s : %s\n", psn.hi, psn.lo, name, path);
#endif
        
        switch (OPTS.matchType) {
            case MATCH_UNKNOWN:
                break;
            case MATCH_CREATOR: if (OPTS.creator != info.ExecFileCreator) continue;
                break;
            case MATCH_NAME: if (strcmp(name, OPTS.name) != 0) continue;
                break;
            case MATCH_PID: if (OPTS.pid != info.UnixPID) continue;
                break;
            case MATCH_PATH: if (strcmp(path, OPTS.path) != 0) continue;
                break;
            case MATCH_BUNDLE_ID:
            {
                CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, path, strlen(path), false);
                if (url == NULL) errexit("can't get bundle location for process '%s' (PSN %ld.%ld, pid %d)", name, psn.hi, psn.lo, info.UnixPID);
                CFBundleRef bundle = CFBundleCreate(NULL, url);
                if (bundle != NULL) {
                    CFStringRef bundleID = CFBundleGetIdentifier(bundle);
#if DEBUG
                    CFShow(bundleID);
#endif
                    if (bundleID != NULL && CFStringCompare(OPTS.bundleID, bundleID, kCFCompareCaseInsensitive) == kCFCompareEqualTo)
                        break;
                }
                CFRelease(url);
                continue;
            }
            default:
                errexit("unknown match type");
        }
        err = CPSSetFrontProcess(&psn);
        if (err != noErr) osstatusexit(err, "can't set front process");
        exit(0);
    }
    if (err != procNotFound) osstatusexit(err, "can't get next process");

    errexit("can't find matching process");
    return 0;
}
