/*
 appswitch - a command-line application switcher
 Nicholas Riley <appswitch@sabi.net>

 Copyright (c) 2003-04, Nicholas Riley
 All rights reserved.

 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of this software nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#define DEBUG 0

#include <unistd.h>
#include <sys/ioctl.h>
#include "CPS.h"

const char *APP_NAME;

#define VERSION "1.0"

struct {
    OSType creator;
    CFStringRef bundleID;
    char *name;
    pid_t pid;
    char *path;
    enum {
        MATCH_UNKNOWN, MATCH_FRONT, MATCH_CREATOR, MATCH_BUNDLE_ID, MATCH_NAME, MATCH_PID, MATCH_PATH, MATCH_ALL
    } matchType;
    enum {
        APP_NONE, APP_SWITCH, APP_SHOW, APP_HIDE, APP_QUIT, APP_KILL, APP_KILL_HARD, APP_LIST, APP_PRINT_PID
    } appAction;
    Boolean longList;
    enum {
        ACTION_NONE, ACTION_SHOW_ALL, ACTION_HIDE_OTHERS
    } action;
    enum {
        FINAL_NONE, FINAL_SWITCH
    } finalAction;
} OPTS =
{
    kLSUnknownCreator, NULL, NULL, -1, NULL, MATCH_UNKNOWN, APP_NONE, ACTION_NONE, FINAL_NONE, false
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
    fprintf(stderr, "usage: %s [-sShHqkFlLP] [-c creator] [-i bundleID] [-a name] [-p pid] [path]\n"
            "  -s            show application, bring windows to front (do not switch)\n"
            "  -S            show all applications\n"
            "  -h            hide application\n"
            "  -H            hide other applications\n"
            "  -q            quit application\n"
            "  -k            kill application (SIGINT)\n"
            "  -K            kill application hard (SIGKILL)\n"
            "  -l            list applications\n"
            "  -L            list applications including full paths and bundle identifiers\n"
            "  -P            print application process ID\n"
            "  -F            bring current application's windows to front\n"
            "  -c creator    match application by four-character creator code ('ToyS')\n"
            "  -i bundle ID  match application by bundle identifier (com.apple.scripteditor)\n"
            "  -p pid        match application by process identifier [slower]\n"
            "  -a name       match application by name\n"
            , APP_NAME);
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

    const char *opts = "c:i:p:a:sShHqkKlLPF";

    while ( (ch = getopt(argc, argv, opts)) != -1) {
        switch (ch) {
            case 'p':
                if (OPTS.matchType != MATCH_UNKNOWN) errexit("choose only one of -c, -i, -p, -a options");
                if (sscanf(optarg, "%d", &OPTS.pid) != 1 || OPTS.pid < 0)
                    errexit("invalid process identifier (argument of -p)");
                OPTS.matchType = MATCH_PID;
                break;
            case 'c':
                if (OPTS.matchType != MATCH_UNKNOWN) errexit("choose only one of -c, -i, -p, -a options");
                if (strlen(optarg) != 4) errexit("creator (argument of -c) must be four characters long");
                OPTS.creator = *(OSTypePtr)optarg;
                OPTS.matchType = MATCH_CREATOR;
                break;
            case 'i':
                if (OPTS.matchType != MATCH_UNKNOWN) errexit("choose only one of -c, -i, -p, -a options");
                OPTS.bundleID = CFStringCreateWithCString(NULL, optarg, CFStringGetSystemEncoding());
                OPTS.matchType = MATCH_BUNDLE_ID;
                break;
            case 'a':
                if (OPTS.matchType != MATCH_UNKNOWN) errexit("choose only one of -c, -i, -p, -a options");
                OPTS.name = strdup(optarg);
                OPTS.matchType = MATCH_NAME;
                break;
            case 's':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P options");
                OPTS.appAction = APP_SHOW;
                break;
            case 'h':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P options");
                OPTS.appAction = APP_HIDE;
                break;
            case 'q':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P options");
                OPTS.appAction = APP_QUIT;
                break;
            case 'k':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P options");
                OPTS.appAction = APP_KILL;
                break;
            case 'K':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P options");
                OPTS.appAction = APP_KILL_HARD;
                break;
            case 'l':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P options");
                OPTS.appAction = APP_LIST;
                break;
            case 'L':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P options");
                OPTS.appAction = APP_LIST;
                OPTS.longList = true;
                break;
            case 'P':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -P options");
                OPTS.appAction = APP_PRINT_PID;
                break;
            case 'S':
                if (OPTS.action != ACTION_NONE) errexit("choose -S, -H or neither option");
                OPTS.action = ACTION_SHOW_ALL;
                break;
            case 'H':
                if (OPTS.action != ACTION_NONE) errexit("choose -S, -H or neither option");
                OPTS.action = ACTION_HIDE_OTHERS;
                break;
            case 'F':
                if (OPTS.finalAction != FINAL_NONE) errexit("choose only one -F option");
                OPTS.finalAction = FINAL_SWITCH;
                break;
            default: usage();
        }
    }

    argc -= optind;
    argv += optind;

    if (OPTS.matchType != MATCH_UNKNOWN && argc != 0) usage();

    if (OPTS.matchType == MATCH_UNKNOWN) {
        if (argc == 0) {
            if (OPTS.appAction == APP_LIST) {
                OPTS.matchType = MATCH_ALL;
            } else if (OPTS.action != ACTION_NONE || OPTS.finalAction != FINAL_NONE) {
                OPTS.matchType = MATCH_FRONT;
            } else usage();
        } else if (argc == 1) {
            OPTS.path = argv[0];
            OPTS.matchType = MATCH_PATH;
        } else usage();
    }

    if (OPTS.matchType != MATCH_FRONT && OPTS.appAction == APP_NONE)
        OPTS.appAction = APP_SWITCH;

}

CPSProcessSerNum frontApplication() {
    CPSProcessSerNum psn;
    OSStatus err = CPSGetFrontProcess(&psn);
    if (err != noErr) osstatusexit(err, "can't get frontmost process");
#if DEBUG
    fprintf(stderr, "front application PSN %ld.%ld\n", psn.hi, psn.lo);
#endif
    return psn;
}

Boolean bundleIdentifierForApplication(CFStringRef *bundleID, char *path) {
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, path, strlen(path), false);
    if (url == NULL) return false;
    CFBundleRef bundle = CFBundleCreate(NULL, url);
    if (bundle != NULL) {
        *bundleID = CFBundleGetIdentifier(bundle);
        if (*bundleID != NULL) {
            CFRetain(*bundleID);
#if DEBUG
            CFShow(*bundleID);
#endif
        }
        CFRelease(bundle);
    } else {
        *bundleID = NULL;
    }
    CFRelease(url);
    return true;
}

OSStatus quitApplication(CPSProcessSerNum *psn) {
    AppleEvent event;
    AEAddressDesc appDesc;
    OSStatus err;

    AEInitializeDesc(&appDesc);
    err = AECreateDesc(typeProcessSerialNumber, psn, sizeof(*psn), &appDesc);
    if (err != noErr) return err;

    // XXX AECreateAppleEvent is very slow in Mac OS X 10.2.4 and earlier.
    // XXX This is Apple's bug: <http://lists.apple.com/archives/applescript-implementors/2003/Feb/19/aecreateappleeventfromco.txt>
    err = AECreateAppleEvent(kCoreEventClass, kAEQuitApplication, &appDesc, kAutoGenerateReturnID, kAnyTransactionID, &event);
    if (err != noErr) return err;

    AppleEvent nullReply = {typeNull, nil};
    err = AESendMessage(&event, &nullReply, kAENoReply, kNoTimeOut);
    (void)AEDisposeDesc(&event);
    if (err != noErr) return err;

    (void)AEDisposeDesc(&nullReply); // according to docs, don't call unless AESend returned successfully

    return noErr;
}

CPSProcessSerNum matchApplication(CPSProcessInfoRec *info) {
    long pathMaxLength = pathconf("/", _PC_PATH_MAX);
    long nameMaxLength = pathconf("/", _PC_NAME_MAX);

    char *path = (char *)malloc(pathMaxLength);
    char *name = (char *)malloc(nameMaxLength);;

    if (path == NULL || name == NULL) errexit("can't allocate memory for path or filename buffer");

    if (OPTS.matchType == MATCH_FRONT) return frontApplication();

    OSStatus err;
    CPSProcessSerNum psn = {
        kNoProcess, kNoProcess
    };
    int len;
    char *format = NULL;
    if (OPTS.appAction == APP_LIST) {
        int termwidth = 80;
        struct winsize ws;
        char *banner = "       PSN   PID TYPE CREA NAME                ";
                     // 12345678.0 12345 1234 1234 12345678901234567890
        if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *)&ws) != -1 ||
             ioctl(STDERR_FILENO, TIOCGWINSZ, (char *)&ws) != -1 ||
             ioctl(STDIN_FILENO,  TIOCGWINSZ, (char *)&ws) != -1) ||
            ws.ws_col != 0) termwidth = ws.ws_col;
        char *formatButPath = "%8ld.%ld %5ld %c%c%c%c %c%c%c%c %-20.20s";
        int pathlen = termwidth - strlen(banner) - 1;
        // XXX don't ever free 'format', should fix if we get called repeatedly
        if (OPTS.longList) {
            printf("%s PATH (bundle identifier)\n", banner);
            asprintf(&format, "%s %%s", formatButPath);
        } else if (pathlen >= 4) {
            printf("%s PATH\n", banner);
            asprintf(&format, "%s %%-%d.%ds", formatButPath, pathlen, pathlen);
        } else {
            format = formatButPath;
        }
    }
    
    while ( (err = CPSGetNextProcess(&psn)) == noErr) {
        err = CPSGetProcessInfo(&psn, info, path, pathMaxLength, &len, name, nameMaxLength);
        if (err != noErr) osstatusexit(err, "can't get information for process PSN %ld.%ld", psn.hi, psn.lo);

#if DEBUG
        fprintf(stderr, "%ld.%ld: %s : %s\n", psn.hi, psn.lo, name, path);
#endif

        switch (OPTS.matchType) {
            case MATCH_ALL:
                break;
            case MATCH_CREATOR: if (OPTS.creator != info->ExecFileCreator) continue;
                break;
            case MATCH_NAME: if (strcmp(name, OPTS.name) != 0) continue;
                break;
            case MATCH_PID: if (OPTS.pid != info->UnixPID) continue;
                break;
            case MATCH_PATH: if (strcmp(path, OPTS.path) != 0) continue;
                break;
            case MATCH_BUNDLE_ID:
               {
                   CFStringRef bundleID;
                   if (!bundleIdentifierForApplication(&bundleID, path))
                       errexit("can't get bundle location for process '%s' (PSN %ld.%ld, pid %ld)", name, psn.hi, psn.lo, info->UnixPID);
                   if (bundleID != NULL) {
                       CFComparisonResult result = CFStringCompare(OPTS.bundleID, bundleID, kCFCompareCaseInsensitive);
                       if (result == kCFCompareEqualTo)
                           break;
		       CFRelease(bundleID);
                   }
                   continue;
               }
            default:
                errexit("internal error: invalid match type");
        }
        if (OPTS.appAction == APP_LIST) {
            char *type = (char *)&(info->ExecFileType), *crea = (char *)&(info->ExecFileCreator);
#define CXX(c) ( (c) < ' ' ? ' ' : (c) )
#define OSTYPE_CHAR_ARGS(t) CXX(t[0]), CXX(t[1]), CXX(t[2]), CXX(t[3])
            printf(format, psn.hi, psn.lo, info->UnixPID,
		   OSTYPE_CHAR_ARGS(type), OSTYPE_CHAR_ARGS(crea),
                   name, path);
            if (OPTS.longList) {
                CFStringRef bundleID = NULL;
                if (!bundleIdentifierForApplication(&bundleID, path))
                    errexit("can't get bundle location for process '%s' (PSN %ld.%ld, pid %ld)", name, psn.hi, psn.lo, info->UnixPID);
                if (bundleID != NULL) {
                    char *bundleIDStr = (char *)CFStringGetCStringPtr(bundleID, CFStringGetSystemEncoding());
                    if (bundleIDStr == NULL) {
                        CFIndex bundleIDLength = CFStringGetLength(bundleID) + 1;
                        bundleIDStr = (char *)malloc(bundleIDLength * sizeof(char));
                        if (!CFStringGetCString(bundleID, bundleIDStr, bundleIDLength, CFStringGetSystemEncoding())) {
                            CFShow(bundleIDStr);
                            errexit("internal error: string encoding conversion failed for bundle identifier");
                        }
                        printf(" (%s)", bundleIDStr);
                        free(bundleIDStr);
                    } else {
                        printf(" (%s)", bundleIDStr);
                    }
                    CFRelease(bundleID);
                }
            }
            putchar('\n');
            continue;
        }
        return psn;
    }
    if (err != procNotFound) osstatusexit(err, "can't get next process");

    if (OPTS.appAction == APP_LIST) return frontApplication();

    errexit("can't find matching process");
    return psn;
}

int main (int argc, char * const argv[]) {
    OSStatus err = noErr;

    APP_NAME = argv[0];
    getargs(argc, argv);

    // need to establish connection with window server
    InitCursor();

    CPSProcessInfoRec info;
    CPSProcessSerNum psn = matchApplication(&info);

    const char *verb;
    switch (OPTS.appAction) {
        case APP_NONE: break;
        case APP_LIST: break; // already handled in matchApplication
        case APP_SWITCH: err = CPSSetFrontProcess(&psn); verb = "set front"; break;
        case APP_SHOW: err = CPSPostShowReq(&psn); verb = "show"; break;
        case APP_HIDE: err = CPSPostHideReq(&psn); verb = "hide"; break;
        case APP_QUIT: err = quitApplication(&psn); verb = "quit"; break;
        case APP_KILL: err = CPSPostKillRequest(&psn, kNilOptions); verb = "kill"; break;
        case APP_KILL_HARD: err = CPSPostKillRequest(&psn, bfCPSKillHard); verb = "kill"; break;
        case APP_PRINT_PID:
            if (info.UnixPID <= 0) errexit("can't get process ID");
            printf("%lu\n", info.UnixPID); // pid_t is signed, but this field isn't
            break;
        default:
            errexit("internal error: invalid application action");
    }
    if (err != noErr) osstatusexit(err, "can't %s process", verb);

    switch (OPTS.action) {
        case ACTION_NONE: break;
        case ACTION_SHOW_ALL: err = CPSPostShowAllReq(&psn); verb = "show all"; break;
        case ACTION_HIDE_OTHERS: err = CPSPostHideMostReq(&psn); verb = "hide other"; break;
        default:
            errexit("internal error: invalid action");
    }
    if (err != noErr) osstatusexit(err, "can't %s processes", verb);

    switch (OPTS.finalAction) {
        case FINAL_NONE: break;
        case FINAL_SWITCH:
            psn = frontApplication();
#if DEBUG
            fprintf(stderr, "posting show request for %ld.%ld\n", psn.hi, psn.lo);
#endif
            if (OPTS.action != ACTION_NONE) usleep(750000); // XXX
            err = CPSPostShowReq(&psn) || CPSSetFrontProcess(&psn);
            verb = "bring current application's windows to the front";
            break;
        default:
            errexit("internal error: invalid final action");    
    }
    if (err != noErr) osstatusexit(err, "can't %s", verb);

    exit(0);
}
