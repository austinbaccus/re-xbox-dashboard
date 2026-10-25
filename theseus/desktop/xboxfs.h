// Desktop drive letter to host path translation. C: lands in Configs,
// Q: in Data, E: in Library, all under the user data dir. F:, G:, R: and
// friends have no analog and come back empty, so XAP scripts that iterate
// drives just see nothing.
//
// Xbox build is unaffected: it uses real C:/Q:/E: drives via XTL.

#pragma once

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#ifndef _WIN32
#include <dirent.h>
#endif
#include <sys/stat.h>
#include "virtual_games.h"

#include "xboxfs_drive.h"  // XboxFS_DriveToPrefix shared helper
#include "path_ci.h"       // Plat_ResolveCaseInsensitive

// Case-insensitive path resolution (Xbox FATX is case-insensitive).
// Walks each path component and matches against actual directory entries.
// Trailing wildcard components like "*.*" or "Track*.mp3" are not file
// names and shouldn't be case-mapped; peel them off, resolve the directory
// prefix, then re-attach the wildcard so callers like FindFirstFile keep
// working on case-sensitive Linux filesystems.
inline bool XboxFS_ResolveCaseInsensitive(char* resolvedPath, size_t maxLen) {
    return Plat_ResolveCaseInsensitive(resolvedPath, maxLen);
}

// Translate an Xbox-style path to a local path
// Returns a pointer to a static buffer (NOT thread-safe, but matches Xbox single-threaded model)
#ifdef _WIN32
// On Windows a real path and an Xbox drive spec look identical: both are
// "C:\...". Everything AppPath hands back is now absolute, so without this a
// genuine C:\Users\...\Data\shaders\dx11\vs_ff.bin gets rewritten into the
// Configs folder and the whole scene silently loses its shaders. POSIX never
// hits this since host paths start with a slash.
// The dashboard's own trees. X: and Q: are ours outright; C: and E: are
// shared with Windows, so only the listed subtrees are virtual. Game
// categories match gameCats[] in virtual_games.h.
inline bool XboxFS_IsVirtualTree(const char* p) {
    if (!p || !p[0] || p[1] != ':') return false;
    char drive = (char)toupper((unsigned char)p[0]);
    if (drive == 'Q' || drive == 'X') return true;

    static const char* kC[] = { "UIX Configs", "icons", "version",
                                "Discord Presence", 0 };
    static const char* kE[] = { "Games", "Applications", "Apps", "Homebrew",
                                "Emulators", "Dashboards", "ES-DE", "TDATA",
                                "UDATA", "Plex", "Jellyfin", "Xcloud", "Music", 0 };
    const char** list = (drive == 'C') ? kC : (drive == 'E') ? kE : 0;
    if (!list) return false;

    const char* rest = p + 2;
    while (*rest == '\\' || *rest == '/') rest++;
    if (!*rest) return true;   // bare "E:\" root listing is still ours

    for (int i = 0; list[i]; i++) {
        size_t n = strlen(list[i]);
        if (strncasecmp(rest, list[i], n) == 0 &&
            (rest[n] == '\0' || rest[n] == '\\' || rest[n] == '/'))
            return true;
    }
    return false;
}

inline bool XboxFS_IsHostPath(const char* p) {
    // "\Device\Harddisk0\PartitionN\..." is what launch() hands us. It has
    // no drive letter, so without this it reads as a real Windows path and
    // never reaches the partition remap below.
    if (strncmp(p, "\\Device\\", 8) == 0) return false;

    // Already resolved through AppPath, so never translate it twice.
    const char* roots[2] = { Plat_ShippedDir(), Plat_UserDataDir() };
    for (int i = 0; i < 2; i++) {
        const char* r = roots[i];
        if (!r || !*r) continue;
        size_t n = strlen(r), j = 0;
        for (; j < n; j++) {
            char a = p[j], b = r[j];
            if (a == '\\') a = '/';
            if (b == '\\') b = '/';
            if (tolower((unsigned char)a) != tolower((unsigned char)b)) break;
        }
        if (j == n && (p[n] == '\0' || p[n] == '/' || p[n] == '\\')) return true;
    }
    // Virtual regardless of what sits on the host disk, so E:\Games still
    // works with a real E: volume mounted.
    if (XboxFS_IsVirtualTree(p)) return false;

    // Anything else is a real Windows path. Identifiers the XAP hands us are
    // always under one of the trees above, so an unlisted C: or E: subtree is
    // a genuine install like C:\Program Files\Game\bin\game.exe.
    return true;
}
#endif

inline const char* XboxFS_TranslatePath(const char* xboxPath) {
    static char s_buf[512];

    if (!xboxPath || !*xboxPath) {
        s_buf[0] = '\0';
        return s_buf;
    }

#ifdef _WIN32
    if (XboxFS_IsHostPath(xboxPath)) {
        snprintf(s_buf, sizeof(s_buf), "%s", xboxPath);
        return s_buf;
    }
#endif

    // Device-path form: "\Device\Harddisk0\PartitionN\..." Used by the
    // XAP `launch()` builtin to address Xbox HDD partitions. Map the
    // partition number back to a drive letter so the rest of this
    // function's drive-letter logic handles it. Only Partition1 (E)
    // and Partition2 (C) actually exist on desktop; everything else
    // (6/7/8/9 = F/G/R/S) is dead-letter.
    {
        static const char kDevPrefix[] = "\\Device\\Harddisk0\\Partition";
        const size_t kDevPrefixLen = sizeof(kDevPrefix) - 1;
        if (strncmp(xboxPath, kDevPrefix, kDevPrefixLen) == 0) {
            const char* p = xboxPath + kDevPrefixLen;
            int partNum = 0;
            while (*p >= '0' && *p <= '9') { partNum = partNum * 10 + (*p - '0'); p++; }
            if (*p == '\\' || *p == '/') p++;
            char drive = 0;
            switch (partNum) {
                case 1: drive = 'E'; break;
                case 2: drive = 'C'; break;
                case 6: drive = 'F'; break;
                case 7: drive = 'G'; break;
                case 8: drive = 'R'; break;
                case 9: drive = 'S'; break;
                default: drive = 0; break;
            }
            const char* prefix = drive ? XboxFS_DriveToPrefix(drive) : 0;
            if (!prefix) {
                s_buf[0] = '\0';
                return s_buf;
            }
            snprintf(s_buf, sizeof(s_buf), "%s/%s", prefix, p);
            for (char* q = s_buf; *q; q++) if (*q == '\\') *q = '/';
            XboxFS_ResolveCaseInsensitive(s_buf, sizeof(s_buf));
            return s_buf;
        }
    }

    // Look for drive letter pattern: "X:\" or "X:/" or "WORD:\".
    const char* colon = strchr(xboxPath, ':');
    if (colon && (colon[1] == '\\' || colon[1] == '/')) {
        int driveLen = (int)(colon - xboxPath);
        if (driveLen > 0 && driveLen < 32) {
            char drive[32];
            memcpy(drive, xboxPath, driveLen);
            drive[driveLen] = '\0';

            // Skip ":\" or ":/" then route the rest of the path based on
            // which Xbox drive this is.
            const char* rest = colon + 2;
            const char* prefix = NULL;

            if (driveLen == 1) {
                if (drive[0] == 'C' || drive[0] == 'c') {
                    // Strip "UIX Configs/" if present so the on-disk Configs/
                    // is a flat dir without the UIX-Lite legacy folder name.
                    static const char kUix[] = "UIX Configs";
                    size_t uixLen = sizeof(kUix) - 1;
                    if (strncmp(rest, kUix, uixLen) == 0 &&
                        (rest[uixLen] == '\\' || rest[uixLen] == '/')) {
                        rest += uixLen + 1;
                    }
                    prefix = AppPath_Tree("Configs");
                } else {
                    prefix = XboxFS_DriveToPrefix(drive[0]);
                }
            } else if (strcasecmp(drive, "MUSIC") == 0) {
                // Legacy MUSIC: virtual drive. The dashboard mounts the
                // user's music collection here; we point it at the bundled
                // Library/Music fallback (the desktop's [Library] MusicRoot
                // setting in desktop.ini is consumed separately by
                // CMediaCollection / DashMusic_Scan).
                prefix = AppPath_Tree("Library/Music");
            }

            if (!prefix) {
                // F:, G:, H:, I:, R:, etc. No analog on desktop. Return
                // an empty string so the caller's stat()/opendir()/fopen()/
                // mkdir() all fail predictably. Critically, do NOT fall
                // through to the "pass through unchanged" branch, that
                // would hand callers like Preloader_Mkdirp a literal
                // "F:/Foo/Bar" path, and mkdir would happily create a
                // directory named "F:" (which Finder helpfully renders as
                // "F/", confusing everyone).
                s_buf[0] = '\0';
                return s_buf;
            }

            snprintf(s_buf, sizeof(s_buf), "%s/%s", prefix, rest);

            // Convert backslashes to forward slashes.
            for (char* p = s_buf; *p; p++) {
                if (*p == '\\') *p = '/';
            }

            // Legacy aliases. Xbox shipped engine-level settings in
            // Q:\System\config.ini (XBE config: resolution, current skin)
            // and a separate XAP-level config in C:\UIX Configs\config.ini
            // (scene wiring). On desktop the engine-level settings are
            // collapsed into Configs/desktop.ini alongside CRT, library
            // roots, etc., so XAP code that calls
            // CSettingsFile::Open("Q:\\System\\Config.ini") reads + writes
            // through the same source-of-truth file.
            char sysCfg[512];
            snprintf(sysCfg, sizeof(sysCfg), "%s/System/config.ini", AppPath_Tree("Data"));
            if (strcasecmp(s_buf, sysCfg) == 0) {
                strncpy(s_buf, AppPath("Configs/desktop.ini"), sizeof(s_buf) - 1);
                s_buf[sizeof(s_buf) - 1] = '\0';
            }

            // Case-insensitive resolve (Xbox FATX doesn't care about case).
            XboxFS_ResolveCaseInsensitive(s_buf, sizeof(s_buf));

            // Virtual-game icon redirect: if the translated file doesn't
            // exist and it looks like a game icon path, fall through to
            // the virtual games DB.
            {
                struct stat _vst;
                if (stat(s_buf, &_vst) != 0) {
                    const char* iconTail = strstr(s_buf, "/icon.jpg");
                    if (!iconTail) iconTail = strstr(s_buf, "/icon.png");
                    if (iconTail) {
                        char tmpBuf[512];
                        strncpy(tmpBuf, s_buf, sizeof(tmpBuf) - 1);
                        tmpBuf[iconTail - s_buf] = 0;
                        extern int VGames_MatchFolder(const char*);
                        int vgIdx = VGames_MatchFolder(tmpBuf);
                        if (vgIdx >= 0) {
                            extern const char* VGames_GetIconPath(int);
                            const char* realIcon = VGames_GetIconPath(vgIdx);
                            if (realIcon) {
                                strncpy(s_buf, realIcon, sizeof(s_buf) - 1);
                                s_buf[sizeof(s_buf) - 1] = 0;
                            }
                        }
                    }
                }
            }
            return s_buf;
        }
    }

    // No drive letter found - return path as-is (with backslash conversion)
    strncpy(s_buf, xboxPath, sizeof(s_buf) - 1);
    s_buf[sizeof(s_buf) - 1] = '\0';
    for (char* p = s_buf; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    return s_buf;
}

// ============================================================================
// Shared helpers used by both Windows and POSIX paths
// ============================================================================

// Fills `buf` with a minimal fake XBE for a virtual game. Returns bytes
// written, or 0 if `buf` is too small. Layout: 0x178-byte header followed
// by a 0x1EC-byte certificate carrying the title name (UTF-16) and title
// ID. The dashboard's title enumeration only reads these two structures.
inline size_t XboxFS_FillFakeXBEBytes(uint8_t* buf, size_t bufSize,
                                       const char* titleName,
                                       unsigned long titleID) {
    const size_t need = 0x178 + 0x1EC;
    if (bufSize < need) return 0;
    memset(buf, 0, need);

    // XBE magic
    buf[0] = 'X'; buf[1] = 'B'; buf[2] = 'E'; buf[3] = 'H';

    uint32_t base = 0x00010000;
    memcpy(buf + 0x104, &base, 4);

    uint32_t hdrSize = (uint32_t)need;
    memcpy(buf + 0x108, &hdrSize, 4);

    uint32_t certAddr = base + 0x178;
    memcpy(buf + 0x118, &certAddr, 4);

    uint8_t* cert = buf + 0x178;
    uint32_t certSize = 0x1EC;
    memcpy(cert + 0x00, &certSize, 4);
    memcpy(cert + 0x08, &titleID, 4);

    uint16_t* titleNameU = (uint16_t*)(cert + 0x0C);
    for (int i = 0; i < 40 && titleName[i]; i++)
        titleNameU[i] = (uint16_t)(unsigned char)titleName[i];

    return need;
}

// Windows-style wildcard match. '*' = any sequence, '?' = single char.
// Case-insensitive. Used to filter synthesized virtual-game names against
// the FindFirstFile pattern; real entries are matched by Win32 / readdir.
// directly. Recursion depth is bounded by the number of '*' tokens in the
// pattern, which is tiny in practice.
inline bool XboxFS_WildcardMatch(const char* pattern, const char* name) {
    while (*pattern && *name) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return true;
            while (*name) {
                if (XboxFS_WildcardMatch(pattern, name)) return true;
                name++;
            }
            return false;
        }
        char p = (char)tolower((unsigned char)*pattern);
        char n = (char)tolower((unsigned char)*name);
        if (*pattern != '?' && p != n) return false;
        pattern++;
        name++;
    }
    while (*pattern == '*') pattern++;
    return *pattern == 0 && *name == 0;
}

#ifdef _WIN32
// On Windows, translate Xbox paths then call real Win32 CreateFileA
// First, save a reference to the real function name
#undef CreateFile
#undef CreateFileA
// Re-declare the real Win32 function after the #undef dropped the macro.
extern "C" __declspec(dllimport) HANDLE __stdcall CreateFileA(
    LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

inline void XboxFS_ToBackslash(char* buf, size_t bufSize, const char* src) {
    strncpy(buf, src, bufSize - 1);
    buf[bufSize - 1] = '\0';
    for (char* p = buf; *p; p++) { if (*p == '/') *p = '\\'; }
}

// Forward decl. Definition lower down, alongside the Find* wrappers.
inline HANDLE XboxFS_CreateFakeXBE_Win32(const char* titleName, unsigned long titleID);

inline HANDLE XboxFS_CreateFileA(LPCSTR name, DWORD access, DWORD share,
                                  void* sa, DWORD disp, DWORD flags, HANDLE templ) {
    const char* translated = XboxFS_TranslatePath(name);
    // Convert forward slashes back to backslashes for Win32 APIs
    char winPath[512];
    XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
    // Strip Xbox flags that cause issues on desktop Windows
    // FILE_FLAG_NO_BUFFERING requires sector-aligned reads; FILE_FLAG_OVERLAPPED needs async I/O setup
    flags &= ~(FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED);
    HANDLE h = ::CreateFileA(winPath, access, share, (LPSECURITY_ATTRIBUTES)sa, disp, flags, templ);
    if (h != INVALID_HANDLE_VALUE) return h;

    // Real open failed; check if this is the default.xbe inside a virtual
    // game folder (matching the behavior the POSIX CreateFileA gives the
    // dashboard's title enumeration).
    char folderPath[512];
    strncpy(folderPath, translated, sizeof(folderPath) - 1);
    folderPath[sizeof(folderPath) - 1] = '\0';
    // Both forward slashes (translated) and backslashes (Windows-native)
    // can show up here; normalize to forward for VGames_MatchFolder, which
    // expects xboxfs/<drive>/<cat>/<name> form.
    for (char* p = folderPath; *p; p++) { if (*p == '\\') *p = '/'; }
    char* lastSl = strrchr(folderPath, '/');
    if (lastSl) {
        const char* fileName = lastSl + 1;
        *lastSl = '\0';
        if (_stricmp(fileName, "default.xbe") == 0) {
            int vgIdx = VGames_MatchFolder(folderPath);
            if (vgIdx >= 0) {
                unsigned long tid = strtoul(g_vgames.games[vgIdx].titleID, NULL, 16);
                HANDLE fakeXbe = XboxFS_CreateFakeXBE_Win32(g_vgames.games[vgIdx].name, tid);
                if (fakeXbe != INVALID_HANDLE_VALUE) {
                    fprintf(stderr, "[xboxfs]   Virtual XBE for '%s' (TID=%08lX)\n",
                            g_vgames.games[vgIdx].name, tid);
                    return fakeXbe;
                }
            }
        }
    }
    return INVALID_HANDLE_VALUE;
}
#define CreateFileA XboxFS_CreateFileA
#define CreateFile CreateFileA

// Windows wrappers for FindFirstFile, FindNextFile, FindClose, GetFileAttributes, etc.
// These translate Xbox paths (E:\Games\*) to xboxfs paths before calling real Win32 APIs.
#undef FindFirstFile
#undef FindFirstFileA
#undef FindNextFile
#undef FindNextFileA
#undef FindClose
#undef GetFileAttributes
#undef GetFileAttributesA
#undef GetFileAttributesEx
#undef GetFileAttributesExA
#undef RemoveDirectory
#undef RemoveDirectoryA

extern "C" __declspec(dllimport) HANDLE __stdcall FindFirstFileA(
    LPCSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData);
extern "C" __declspec(dllimport) BOOL __stdcall FindNextFileA(
    HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData);
extern "C" __declspec(dllimport) BOOL __stdcall FindClose(HANDLE hFindFile);
extern "C" __declspec(dllimport) DWORD __stdcall GetFileAttributesA(LPCSTR lpFileName);
extern "C" __declspec(dllimport) BOOL __stdcall GetFileAttributesExA(
    LPCSTR lpFileName, GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation);
extern "C" __declspec(dllimport) BOOL __stdcall RemoveDirectoryA(LPCSTR lpPathName);
extern "C" __declspec(dllimport) DWORD __stdcall GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer);
extern "C" __declspec(dllimport) UINT  __stdcall GetTempFileNameA(LPCSTR lpPathName, LPCSTR lpPrefixString, UINT uUnique, LPSTR lpTempFileName);
extern "C" __declspec(dllimport) BOOL  __stdcall WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
extern "C" __declspec(dllimport) DWORD __stdcall SetFilePointer(HANDLE hFile, LONG lDistanceToMove, PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod);

// Windows fake XBE for virtual games
// On POSIX we hand back a tmpfile() FILE* cast to HANDLE; on Windows real
// HANDLEs and FILE*s aren't interchangeable, so create a temp file on disk
// flagged DELETE_ON_CLOSE, write the synthesized XBE bytes into it, rewind,
// and hand the OS HANDLE to the caller. The OS deletes the backing file
// when the caller closes the handle.
inline HANDLE XboxFS_CreateFakeXBE_Win32(const char* titleName, unsigned long titleID) {
    char tempDir[260];
    char tempFile[260];
    DWORD n = ::GetTempPathA(260, tempDir);
    if (n == 0 || n > 260) return INVALID_HANDLE_VALUE;
    if (::GetTempFileNameA(tempDir, "xbe", 0, tempFile) == 0)
        return INVALID_HANDLE_VALUE;
    HANDLE h = ::CreateFileA(tempFile,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        NULL);
    if (h == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    uint8_t buf[0x178 + 0x1EC];
    size_t bytes = XboxFS_FillFakeXBEBytes(buf, sizeof(buf), titleName, titleID);
    if (bytes == 0) { ::FindClose(h); return INVALID_HANDLE_VALUE; }
    DWORD written = 0;
    ::WriteFile(h, buf, (DWORD)bytes, &written, NULL);
    ::SetFilePointer(h, 0, NULL, 0 /* FILE_BEGIN */);
    return h;
}

// Windows wrapped Find handle
// Carries the real Win32 find handle plus the virtual-game cursor for the
// directory we're enumerating. XboxFS_FindFirstFileA always returns one of
// these; XboxFS_FindNextFileA / XboxFS_FindClose detect ours via the magic
// field and fall through to real Win32 if it doesn't match (defensive --
// in practice every entry point goes through our macro overrides).
#define XBOXFS_FFH_MAGIC 0x46464858  /* 'XHFF' */
struct XboxFSFindHandle {
    DWORD magic;
    HANDLE realHandle;     // Win32 find handle, or INVALID_HANDLE_VALUE if no real dir
    char dirPath[512];     // translated dir path with backslashes (for shadow checks)
    char pattern[256];     // basename pattern from the original call
    int  vgIndices[VGAMES_MAX];
    int  vgCount;
    int  vgPos;
};

// Inject virtual games for a translated dirPath. Shared with POSIX.
inline void XboxFS_PopulateVirtualGames(XboxFSFindHandle* ffh, const char* dirPath) {
    ffh->vgCount = 0;
    ffh->vgPos = 0;

    char drive[2] = {0};
    char cat[32] = {0};
    if (!VGames_ParseDir(dirPath, drive, cat, sizeof(cat))) return;
    ffh->vgCount = VGames_GetForDirectory(drive, cat, ffh->vgIndices, VGAMES_MAX);
}

// Returns the next virtual-game entry that (a) matches the pattern and
// (b) doesn't have a real folder of the same name in dirPath. Fills `fd`
// and returns TRUE on success; FALSE when virtual entries are exhausted.
inline BOOL XboxFS_NextVirtualEntry(XboxFSFindHandle* ffh, LPWIN32_FIND_DATAA fd) {
    while (ffh->vgPos < ffh->vgCount) {
        int idx = ffh->vgIndices[ffh->vgPos++];
        if (idx < 0 || idx >= VGAMES_MAX) continue;
        VirtualGame& vg = g_vgames.games[idx];
        if (!vg.valid) continue;
        if (!XboxFS_WildcardMatch(ffh->pattern, vg.name)) continue;
        // Skip if a real folder with this name already exists alongside.
        char shadow[768];
        snprintf(shadow, sizeof(shadow), "%s\\%s", ffh->dirPath, vg.name);
        DWORD attrs = ::GetFileAttributesA(shadow);
        if (attrs != INVALID_FILE_ATTRIBUTES) continue;
        memset(fd, 0, sizeof(*fd));
        strncpy(fd->cFileName, vg.name, sizeof(fd->cFileName) - 1);
        fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
        return TRUE;
    }
    return FALSE;
}

inline HANDLE XboxFS_FindFirstFileA(LPCSTR path, LPWIN32_FIND_DATAA fd) {
    if (!path || !fd) return INVALID_HANDLE_VALUE;

    // Translate Xbox drive paths (E:\Games\* -> xboxfs\E\Games\*)
    char winPath[512];
    if (strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
    } else {
        strncpy(winPath, path, sizeof(winPath) - 1);
        winPath[sizeof(winPath) - 1] = '\0';
    }

    // Allocate the wrapper unconditionally. Even a pure pattern-not-found
    // case may have virtual matches to inject.
    XboxFSFindHandle* ffh = (XboxFSFindHandle*)calloc(1, sizeof(XboxFSFindHandle));
    if (!ffh) return INVALID_HANDLE_VALUE;
    ffh->magic = XBOXFS_FFH_MAGIC;
    ffh->realHandle = INVALID_HANDLE_VALUE;

    // Split dir + pattern at the last backslash (Win32 paths are backslashed).
    const char* lastSep = strrchr(winPath, '\\');
    if (!lastSep) lastSep = strrchr(winPath, '/');
    if (lastSep) {
        size_t dirLen = (size_t)(lastSep - winPath);
        if (dirLen >= sizeof(ffh->dirPath)) dirLen = sizeof(ffh->dirPath) - 1;
        memcpy(ffh->dirPath, winPath, dirLen);
        ffh->dirPath[dirLen] = '\0';
        strncpy(ffh->pattern, lastSep + 1, sizeof(ffh->pattern) - 1);
    } else {
        strcpy(ffh->dirPath, ".");
        strncpy(ffh->pattern, winPath, sizeof(ffh->pattern) - 1);
    }

    // Win32 / POSIX FindFirstFile both treat "*.*" as "match everything".
    // Our wildcard matcher is strict (literal '.' required), so collapse
    // the convention here; otherwise virtual game names without a dot
    // (most of them) get filtered out during injection.
    if (strcmp(ffh->pattern, "*.*") == 0)
        strcpy(ffh->pattern, "*");

    // Populate virtual game list for this directory before any filesystem call.
    XboxFS_PopulateVirtualGames(ffh, ffh->dirPath);

    // Now query the real filesystem.
    ffh->realHandle = ::FindFirstFileA(winPath, fd);
    if (ffh->realHandle != INVALID_HANDLE_VALUE)
        return (HANDLE)ffh;

    // Real find failed; try virtual entries directly.
    if (XboxFS_NextVirtualEntry(ffh, fd))
        return (HANDLE)ffh;

    free(ffh);
    return INVALID_HANDLE_VALUE;
}

inline BOOL XboxFS_FindNextFileA(HANDLE h, LPWIN32_FIND_DATAA fd) {
    if (h == INVALID_HANDLE_VALUE || !fd) return FALSE;
    XboxFSFindHandle* ffh = (XboxFSFindHandle*)h;
    if (ffh->magic != XBOXFS_FFH_MAGIC) {
        // Defensive; something passed a raw Win32 handle in.
        return ::FindNextFileA(h, fd);
    }
    // Exhaust real entries first.
    if (ffh->realHandle != INVALID_HANDLE_VALUE) {
        if (::FindNextFileA(ffh->realHandle, fd))
            return TRUE;
        // Real entries done; close the real handle so subsequent calls
        // fall through to virtual entries directly.
        ::FindClose(ffh->realHandle);
        ffh->realHandle = INVALID_HANDLE_VALUE;
    }
    return XboxFS_NextVirtualEntry(ffh, fd);
}

inline BOOL XboxFS_FindClose(HANDLE h) {
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    XboxFSFindHandle* ffh = (XboxFSFindHandle*)h;
    if (ffh->magic != XBOXFS_FFH_MAGIC) {
        return ::FindClose(h);
    }
    BOOL ok = TRUE;
    if (ffh->realHandle != INVALID_HANDLE_VALUE)
        ok = ::FindClose(ffh->realHandle);
    free(ffh);
    return ok;
}

inline DWORD XboxFS_GetFileAttributesA(LPCSTR path) {
    if (path && strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        char winPath[512];
        XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
        return ::GetFileAttributesA(winPath);
    }
    return ::GetFileAttributesA(path);
}

inline BOOL XboxFS_GetFileAttributesExA(LPCSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    if (path && strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        char winPath[512];
        XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
        return ::GetFileAttributesExA(winPath, level, info);
    }
    return ::GetFileAttributesExA(path, level, info);
}

inline BOOL XboxFS_RemoveDirectoryA(LPCSTR path) {
    if (path && strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        char winPath[512];
        XboxFS_ToBackslash(winPath, sizeof(winPath), translated);
        return ::RemoveDirectoryA(winPath);
    }
    return ::RemoveDirectoryA(path);
}

#define FindFirstFileA XboxFS_FindFirstFileA
#define FindFirstFile FindFirstFileA
#define FindNextFileA XboxFS_FindNextFileA
#define FindNextFile FindNextFileA
#define FindClose XboxFS_FindClose
#define GetFileAttributesA XboxFS_GetFileAttributesA
#define GetFileAttributes GetFileAttributesA
#define GetFileAttributesExA XboxFS_GetFileAttributesExA
#define GetFileAttributesEx GetFileAttributesExA
#define RemoveDirectoryA XboxFS_RemoveDirectoryA
#define RemoveDirectory RemoveDirectoryA
#else
// On non-Windows, use fopen-based implementation (HANDLE = FILE*)
#undef CreateFileA

// Create an in-memory FILE* containing a minimal fake XBE header for virtual games.
// Wraps the shared XboxFS_FillFakeXBEBytes helper in a tmpfile().
inline FILE* XboxFS_CreateFakeXBE(const char* titleName, unsigned long titleID) {
    FILE* f = tmpfile();
    if (!f) return NULL;
    uint8_t buf[0x178 + 0x1EC];
    size_t n = XboxFS_FillFakeXBEBytes(buf, sizeof(buf), titleName, titleID);
    if (n == 0) { fclose(f); return NULL; }
    fwrite(buf, 1, n, f);
    fseek(f, 0, SEEK_SET);
    return f;
}

// Try to load a file from mounted qcow2 FATX when xboxfs host file is missing.
// Parses Xbox path like "E:\UDATA\4d530004\TitleImage.xbx", walks the FATX
// directory tree, reads file data, writes to tmpfile() and returns it.
inline FILE* XboxFS_TryFATXFallback(const char* xboxPath) {
    // Only handle drive-letter paths
    if (!xboxPath || strlen(xboxPath) < 3 || xboxPath[1] != ':') return nullptr;

    // The global HDD instance lives in desktop_nodes.cpp; go through this
    // free function to avoid circular includes.
    FILE* XboxFS_FATXReadFile(const char* xboxPath);
    return XboxFS_FATXReadFile(xboxPath);
}

inline HANDLE CreateFileA(LPCSTR name, DWORD access, DWORD share,
                          void* sa, DWORD disp, DWORD flags, HANDLE templ) {
    // xboxfs host filesystem is the primary source for UIX Desktop data
    const char* translated = XboxFS_TranslatePath(name);
    const char* mode = (access & 0x40000000) ? "wb" : "rb"; // GENERIC_WRITE : GENERIC_READ
    FILE* f = fopen(translated, mode);
    if (!f) {
        // Check if this is a virtual game file (e.g. default.xbe inside a virtual folder)
        char folderPath[512];
        strncpy(folderPath, translated, sizeof(folderPath) - 1);
        folderPath[sizeof(folderPath) - 1] = 0;
        char* lastSl = strrchr(folderPath, '/');
        if (lastSl) {
            const char* fileName = lastSl + 1;
            *lastSl = 0;
            extern int VGames_MatchFolder(const char*);
            int vgIdx = VGames_MatchFolder(folderPath);
            if (vgIdx >= 0) {
                if (strcasecmp(fileName, "default.xbe") == 0) {
                    extern VirtualGameDB g_vgames;
                    unsigned long tid = strtoul(g_vgames.games[vgIdx].titleID, NULL, 16);
                    FILE* fakeXbe = XboxFS_CreateFakeXBE(g_vgames.games[vgIdx].name, tid);
                    if (fakeXbe) {
                        fprintf(stderr, "[xboxfs]   Virtual XBE for '%s' (TID=%08lX)\n",
                                g_vgames.games[vgIdx].name, tid);
                        return (HANDLE)fakeXbe;
                    }
                }
            }
        }
        // Fallback: try qcow2 FATX but ONLY for save data paths (UDATA/TDATA)
        // Don't interfere with xboxfs/VGames for game icons, configs, etc.
        extern char g_qcowPath[512];
        if (g_qcowPath[0] && name && (strstr(name, "UDATA") || strstr(name, "TDATA"))) {
            f = XboxFS_TryFATXFallback(name);
            if (f) fprintf(stderr, "[xboxfs]   Loaded from qcow2 FATX\n");
        }
        // silently fail - not all lookups are expected to succeed
    }
    return f ? (HANDLE)f : INVALID_HANDLE_VALUE;
}

// Map CreateFile to CreateFileA (ANSI mode, matching TCHAR=char)
#undef CreateFile
#define CreateFile CreateFileA
#endif

// Override fopen/_tfopen calls that use Xbox paths
// settingsfile.cpp, runner.cpp, image.cpp all use fopen with raw Xbox paths like "C:\UIX Configs\cache.ini"
// Without this, macOS creates files with literal backslashes in their names
inline FILE* XboxFS_fopen(const char* path, const char* mode) {
    if (path && strchr(path, ':')) {
        const char* translated = XboxFS_TranslatePath(path);
        // Ensure parent directories exist for write modes
        if (mode[0] == 'w' || mode[0] == 'a') {
            char dirBuf[512];
            strncpy(dirBuf, translated, sizeof(dirBuf) - 1);
            dirBuf[sizeof(dirBuf) - 1] = '\0';
            char* lastSlash = strrchr(dirBuf, '/');
            if (lastSlash) {
                *lastSlash = '\0';
                // Recursively create parent dirs
                for (char* p = dirBuf; *p; p++) {
                    if (*p == '/') {
                        *p = '\0';
                        #ifdef _WIN32
                        _mkdir(dirBuf);
#else
                        mkdir(dirBuf, 0755);
#endif
                        *p = '/';
                    }
                }
                #ifdef _WIN32
                        _mkdir(dirBuf);
#else
                        mkdir(dirBuf, 0755);
#endif
            }
        }
        return fopen(translated, mode);
    }
    return fopen(path, mode);
}
#define fopen XboxFS_fopen
#define _tfopen XboxFS_fopen
