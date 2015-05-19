/*
 appswitch - a command-line application switcher
 Nicholas Riley <appswitch@sabi.net>

 Copyright (c) 2003-12, Nicholas Riley
 All rights reserved.

 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of this software nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#define DEBUG 0

#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <ApplicationServices/ApplicationServices.h>
#import <AppKit/AppKit.h>

const char *APP_NAME;

#define VERSION "1.1.1"

struct {
    CFStringRef creator;
    CFStringRef bundleID;
    CFStringRef name;
    pid_t pid;
    CFStringRef path;
    enum {
        MATCH_UNKNOWN, MATCH_FRONT, MATCH_CREATOR, MATCH_BUNDLE_ID, MATCH_NAME, MATCH_PID, MATCH_PATH, MATCH_ALL
    } matchType;
    enum {
        APP_NONE, APP_SWITCH, APP_SHOW, APP_HIDE, APP_QUIT, APP_KILL, APP_KILL_HARD, APP_LIST, APP_PRINT_PID, APP_FRONTMOST
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
    { appIsDaemon, "application is background-only", },
    { procNotFound, "application not found" },
    { connectionInvalid, "application is not background-only", },
    // CoreGraphics errors
    { kCGErrorIllegalArgument, "window server error.\nAre you logged in?" },
    { kCGErrorInvalidContext, "application context unavailable" },
    { fnfErr, "file not found" },
    // (abused) errors
    { permErr, "no permission" },
    { 0, NULL }
};

void usage() {
    fprintf(stderr, "usage: %s [-sShHqkKlLPfF] [-c creator] [-i bundleID] [-a name] [-p pid] [path]\n"
            "  -s            show application, bring windows to front (do not switch)\n"
            "  -S            show all applications\n"
            "  -h            hide application\n"
            "  -H            hide other applications\n"
            "  -q            quit application\n"
            "  -k            kill application (SIGTERM)\n"
            "  -K            kill application hard (SIGKILL)\n"
            "  -l            list applications\n"
            "  -L            list applications including full paths and bundle identifiers\n"
            "  -P            print application process ID\n"
            "  -f            bring application's frontmost window to front\n"
            "  -F            bring current application's windows to front\n"
            "  -c creator    match application by four-character creator code ('ToyS')\n"
            "  -i bundle ID  match application by bundle identifier (com.apple.ScriptEditor2)\n"
            "  -p pid        match application by process identifier\n"
            "  -a name       match application by name\n"
            , APP_NAME);
    fprintf(stderr, "appswitch "VERSION" (c) 2003-15 Nicholas Riley <http://sabi.net/nriley/software/>.\n"
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
        snprintf(str, len, "%s (%ld)", errDesc, (long)err);
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

    const char *opts = "c:i:p:a:sShHqkKlLPfF";

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
                OPTS.creator = CFStringCreateWithFileSystemRepresentation(NULL, optarg);
                if (OPTS.creator == NULL) errexit("invalid creator (wrong text encoding?)");
                if (CFStringGetLength(OPTS.creator) != 4) errexit("creator (argument of -c) must be four characters long");
                OPTS.matchType = MATCH_CREATOR;
                break;
            case 'i':
                if (OPTS.matchType != MATCH_UNKNOWN) errexit("choose only one of -c, -i, -p, -a options");
                OPTS.bundleID = CFStringCreateWithFileSystemRepresentation(NULL, optarg);
                if (OPTS.bundleID == NULL) errexit("invalid bundle ID (wrong text encoding?)");
                OPTS.matchType = MATCH_BUNDLE_ID;
                break;
            case 'a':
                if (OPTS.matchType != MATCH_UNKNOWN) errexit("choose only one of -c, -i, -p, -a options");
                OPTS.name = CFStringCreateWithFileSystemRepresentation(NULL, optarg);
                if (OPTS.name == NULL) errexit("invalid application name (wrong text encoding?)");
                OPTS.matchType = MATCH_NAME;
                break;
            case 's':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P, -f options");
                OPTS.appAction = APP_SHOW;
                break;
            case 'h':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P, -f options");
                OPTS.appAction = APP_HIDE;
                break;
            case 'q':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P, -f options");
                OPTS.appAction = APP_QUIT;
                break;
            case 'k':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P, -f options");
                OPTS.appAction = APP_KILL;
                break;
            case 'K':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P, -f options");
                OPTS.appAction = APP_KILL_HARD;
                break;
            case 'l':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P, -f options");
                OPTS.appAction = APP_LIST;
                break;
            case 'L':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P, -f options");
                OPTS.appAction = APP_LIST;
                OPTS.longList = true;
                break;
            case 'P':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P, -f options");
                OPTS.appAction = APP_PRINT_PID;
                break;
            case 'f':
                if (OPTS.appAction != APP_NONE) errexit("choose only one of -s, -h, -q, -k, -K, -l, -L, -P, -f options");
                OPTS.appAction = APP_FRONTMOST;
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
            OPTS.path = CFStringCreateWithFileSystemRepresentation(NULL, argv[0]);
            if (OPTS.path == NULL) errexit("invalid path (wrong text encoding?)");
            OPTS.matchType = MATCH_PATH;
        } else usage();
    }

    if (OPTS.matchType != MATCH_FRONT && OPTS.appAction == APP_NONE)
        OPTS.appAction = APP_SWITCH;

}

ProcessSerialNumber frontApplication() {
    ProcessSerialNumber psn;
    OSStatus err = GetFrontProcess(&psn);
    if (err != noErr) osstatusexit(err, "can't get frontmost process");
#if DEBUG
    fprintf(stderr, "front application PSN %ld.%ld\n", psn.lowLongOfPSN, psn.highLongOfPSN);
#endif
    return psn;
}

OSStatus quitApplication(ProcessSerialNumber *psn) {
    AppleEvent event;
    AEAddressDesc appDesc;
    OSStatus err;

    AEInitializeDesc(&appDesc);
    err = AECreateDesc(typeProcessSerialNumber, psn, sizeof(*psn), &appDesc);
    if (err != noErr) return err;

    err = AECreateAppleEvent(kCoreEventClass, kAEQuitApplication, &appDesc, kAutoGenerateReturnID, kAnyTransactionID, &event);
    if (err != noErr) return err;

    AppleEvent nullReply = {typeNull, nil};
    err = AESendMessage(&event, &nullReply, kAENoReply, kNoTimeOut);
    (void)AEDisposeDesc(&event);
    if (err != noErr) return err;

    (void)AEDisposeDesc(&nullReply); // according to docs, don't call unless AESend returned successfully

    return noErr;
}

pid_t getPID(const ProcessSerialNumber *psn) {
    pid_t pid;
    OSStatus err = GetProcessPID(psn, &pid);
    if (err != noErr) osstatusexit(err, "can't get process ID");
    return pid;
}

bool infoStringMatches(CFDictionaryRef info, CFStringRef key, CFStringRef matchStr) {
    CFStringRef str = CFDictionaryGetValue(info, key);
    if (str == NULL)
        return false;
    /* note: this means we might match names/paths that are wrong, but works better in the common case */
    return CFStringCompare(str, matchStr, kCFCompareCaseInsensitive) == kCFCompareEqualTo;
}

CFStringRef stringTrimmedToWidth(CFStringRef str, CFIndex width) {
    if (str == NULL)
        str = CFSTR("");
    CFIndex length = CFStringGetLength(str);
    if (length == width)
        return CFRetain(str);
    
    CFMutableStringRef padStr = CFStringCreateMutableCopy(NULL, width, str);
    CFStringPad(padStr, CFSTR(" "), width, 0);
    return padStr;
}

ProcessSerialNumber matchApplication(void) {
    if (OPTS.matchType == MATCH_FRONT) return frontApplication();

    OSStatus err;
    ProcessSerialNumber psn = {
        kNoProcess, kNoProcess
    };
    pid_t pid;
    CFStringRef format = NULL;
    CFIndex nameWidth = 19;
    CFIndex pathWidth = 0;
    if (OPTS.appAction == APP_LIST) {
        int termwidth = 80;
        struct winsize ws;
        char *banner = "        PSN   PID TYPE CREA NAME               ";
                     // 123456789.0 12345 1234 1234 1234567890123456789
        if ((ioctl(STDOUT_FILENO, TIOCGWINSZ, (char *)&ws) != -1 ||
             ioctl(STDERR_FILENO, TIOCGWINSZ, (char *)&ws) != -1 ||
             ioctl(STDIN_FILENO,  TIOCGWINSZ, (char *)&ws) != -1) ||
            ws.ws_col != 0) termwidth = ws.ws_col;
        char *formatButPath = "%9ld.%ld %5ld %@ %@ %@";
        // XXX don't ever release 'format', should fix if we get called repeatedly
        if (OPTS.longList) {
            pathWidth = 1;
            printf("%s PATH (bundle identifier)\n", banner);
            format = CFStringCreateWithFormat(NULL, NULL, CFSTR("%s %%@"), formatButPath);
        } else {
            pathWidth = termwidth - strlen(banner) - 1;
            if (pathWidth >= 4) {
                printf("%s PATH\n", banner);
                format = CFStringCreateWithFormat(NULL, NULL, CFSTR("%s %%@"), formatButPath);
            } else {
                pathWidth = 0;
                format = CFStringCreateWithCString(NULL, formatButPath, kCFStringEncodingUTF8);
            }
        }
    }
    
    CFDictionaryRef info = NULL;
    while ( (err = GetNextProcess(&psn)) == noErr) {
        if (info != NULL) CFRelease(info);
        info = ProcessInformationCopyDictionary(&psn, kProcessDictionaryIncludeAllInformationMask);
        if (info == NULL) errexit("can't get information for process with PSN %ld.%ld",
                                  psn.lowLongOfPSN, psn.highLongOfPSN);

        switch (OPTS.matchType) {
            case MATCH_ALL:
                break;
            case MATCH_CREATOR: if (!infoStringMatches(info, CFSTR("FileCreator"), OPTS.creator)) continue;
                break;
            case MATCH_NAME: if (!infoStringMatches(info, CFSTR("CFBundleName"), OPTS.name)) continue;
                break;
            case MATCH_PID: err = GetProcessPID(&psn, &pid); if (err != noErr || OPTS.pid != pid) continue;
                break;
            case MATCH_PATH: if (!infoStringMatches(info, CFSTR("BundlePath"), OPTS.path) &&
                !infoStringMatches(info, CFSTR("CFBundleExecutable"), OPTS.path)) continue;
                break;
            case MATCH_BUNDLE_ID: if (!infoStringMatches(info, CFSTR("CFBundleIdentifier"), OPTS.bundleID)) continue;
                break;
            default:
                errexit("internal error: invalid match type");
        }
        if (OPTS.appAction == APP_LIST) {
            if (GetProcessPID(&psn, &pid) != noErr)
                pid = -1;
            CFStringRef path = NULL;
            // XXX padding/truncation probably breaks with double-width characters
            if (pathWidth) {
                path = CFDictionaryGetValue(info, CFSTR("BundlePath"));
                if (path == NULL)
                    path = CFDictionaryGetValue(info, CFSTR("CFBundleExecutable"));
                if (!OPTS.longList)
                    path = stringTrimmedToWidth(path, pathWidth);
            }
            CFStringRef name = stringTrimmedToWidth(CFDictionaryGetValue(info, CFSTR("CFBundleName")), nameWidth);
            CFStringRef type = stringTrimmedToWidth(CFDictionaryGetValue(info, CFSTR("FileType")), 4);
            CFStringRef creator = stringTrimmedToWidth(CFDictionaryGetValue(info, CFSTR("FileCreator")), 4);
            CFStringRef line = CFStringCreateWithFormat(NULL, NULL, format,
                psn.lowLongOfPSN, psn.highLongOfPSN, pid, type, creator, name, path);
            CFRelease(name);
            CFRelease(type);
            CFRelease(creator);
            if (!OPTS.longList)
                CFRelease(path);
            else {
                CFStringRef bundleID = CFDictionaryGetValue(info, CFSTR("CFBundleIdentifier"));
                if (bundleID != NULL && CFStringGetLength(bundleID) != 0) {
                    CFStringRef origLine = line;
                    line = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@ (%@)"), line, bundleID);
                    CFRelease(origLine);
                }
            }
            char *cStr = (char *)CFStringGetCStringPtr(line, CFStringGetSystemEncoding());
            if (cStr != NULL) {
                puts(cStr);
            } else {
                CFIndex cStrLength = CFStringGetMaximumSizeOfFileSystemRepresentation(line);
                cStr = (char *)malloc(cStrLength * sizeof(char));
                if (!CFStringGetFileSystemRepresentation(line, cStr, cStrLength)) {
                    CFShow(cStr);
                    errexit("internal error: string encoding conversion failed");
                }
                puts(cStr);
                free(cStr);
            }
            continue;
        }
        return psn;
    }
    if (err != procNotFound) osstatusexit(err, "can't get next process");

    if (OPTS.appAction == APP_LIST) return frontApplication();

    errexit("can't find matching process");
    return psn; // not reached
}

int main(int argc, char * const argv[]) {
    OSStatus err = noErr;

    APP_NAME = argv[0];
    getargs(argc, argv);

    ProcessSerialNumber psn;
    
    // required in Leopard to prevent paramErr - rdar://problem/5579375
    err = GetCurrentProcess(&psn);
    if (err != noErr) osstatusexit(err, "can't contact window server");
    
    psn = matchApplication();

    const char *verb = NULL;
    switch (OPTS.appAction) {
        case APP_NONE: break;
        case APP_LIST: break; // already handled in matchApplication
        case APP_SWITCH: err = SetFrontProcess(&psn); verb = "set front"; break;
        case APP_SHOW: err = ShowHideProcess(&psn, true); verb = "show"; break;
        case APP_HIDE: err = ShowHideProcess(&psn, false); verb = "hide"; break;
        case APP_QUIT: err = quitApplication(&psn); verb = "quit"; break;
        case APP_KILL: err = KillProcess(&psn); verb = "send SIGTERM to"; break;
        case APP_KILL_HARD:
        {
            // no Process Manager equivalent - rdar://problem/4808400
            if (kill(getPID(&psn), SIGKILL) == -1)
                err = (errno == ESRCH) ? procNotFound : (errno == EPERM ? permErr : paramErr);
            verb = "send SIGKILL to";
            break;
        }
        case APP_PRINT_PID: printf("%d\n", getPID(&psn)); break;
        case APP_FRONTMOST: err = SetFrontProcessWithOptions(&psn, kSetFrontProcessFrontWindowOnly);
            verb = "bring frontmost window to front"; break;
        default:
            errexit("internal error: invalid application action");
    }
    if (err != noErr) osstatusexit(err, "can't %s process", verb);

    switch (OPTS.action) {
        case ACTION_NONE: break;
        // no Process Manager equivalents - rdar://problem/4808397
        case ACTION_SHOW_ALL:
            [[NSAutoreleasePool alloc] init];
            [[NSApplication sharedApplication] unhideAllApplications: nil];
            err = noErr;
            verb = "show all";
            break;
        case ACTION_HIDE_OTHERS:
            [[NSAutoreleasePool alloc] init];
            [[NSApplication sharedApplication] hideOtherApplications: nil];
            err = noErr;
            verb = "hide other";
            break;
        default:
            errexit("internal error: invalid action");
    }
    if (err != noErr) osstatusexit(err, "can't %s processes", verb);

    switch (OPTS.finalAction) {
        case FINAL_NONE: break;
        case FINAL_SWITCH:
            psn = frontApplication();
#if DEBUG
            fprintf(stderr, "posting show request for %ld.%ld\n", psn.lowLongOfPSN, psn.highLongOfPSN);
#endif
            if (OPTS.action != ACTION_NONE) usleep(750000); // XXX
            err = ShowHideProcess(&psn, true) || SetFrontProcess(&psn);
            verb = "bring current application's windows to the front";
            break;
        default:
            errexit("internal error: invalid final action");    
    }
    if (err != noErr) osstatusexit(err, "can't %s", verb);

    exit(0);
}
