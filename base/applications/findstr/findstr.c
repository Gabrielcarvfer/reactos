/*
 * PROJECT:     ReactOS FINDSTR command
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Searches for strings in files, as Windows' FINDSTR
 * COPYRIGHT:   Copyright 2026 Gabriel Ferreira
 */

/*
 * FINDSTR [/B] [/E] [/L | /R] [/S] [/I] [/X] [/V] [/N] [/M] [/O] [/P]
 *         [/F:file] [/C:string] [/G:file] [/D:dirlist] [/A:color] [/OFF[LINE]]
 *         strings [[drive:][path]filename[ ...]]
 *
 * As on Windows:
 * - Without /C or /G, the first argument is a space separated list of strings
 *   (any of them matches); with them, every argument is a file.
 * - Without /L or /R, the strings are regular expressions if the first one
 *   has a regular expression character (. * ^ $ [ \), literal otherwise.
 * - Regular expressions: . * ^ $ [class] [^class] [x-y] \x \< \>
 * - Lines are matched as bytes (CR LF or LF end them); without files, the
 *   standard input is searched.
 * - Exit code: 0 when a line matched, 1 when none did, 2 on a usage error.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>

#include "resource.h"

/* Options */
static BOOL bBegin, bEnd, bLiteral, bRegex, bSubdirs, bIgnoreCase, bExact,
            bInvert, bNumbers, bFileNames, bOffsets, bSkipBinary;
static BOOL bShowNames;     /* Prefix lines with the file name */

/* A compiled search string */
#define TOK_CHAR    0
#define TOK_ANY     1
#define TOK_CLASS   2
#define TOK_BOL     3
#define TOK_EOL     4
#define TOK_WORDBEG 5
#define TOK_WORDEND 6

typedef struct _TOKEN
{
    UCHAR Type;
    UCHAR Char;
    BOOL Star;
    UCHAR Class[32];    /* Bit set of the 256 bytes */
} TOKEN;

typedef struct _PATTERN
{
    char *Text;         /* Literal: the string */
    size_t Length;
    TOKEN *Tokens;      /* Regex: the tokens */
    int Count;
} PATTERN;

static PATTERN *Patterns;
static int PatternCount;

static int MatchedAny;

/* PRIVATE FUNCTIONS **********************************************************/

static void PrintResource(UINT Id, const char *Arg)
{
    char Format[4096];

    if (!LoadStringA(GetModuleHandleA(NULL), Id, Format, sizeof(Format)))
        return;
    fprintf(stderr, Format, Arg ? Arg : "");
}

static UCHAR Fold(UCHAR c)
{
    if (bIgnoreCase)
    {
        CHAR s[2] = { (CHAR)c, 0 };
        CharUpperBuffA(s, 1);
        return (UCHAR)s[0];
    }
    return c;
}

static BOOL IsWordChar(UCHAR c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c >= 0x80;
}

static void ClassSet(TOKEN *Token, UCHAR c)
{
    Token->Class[c >> 3] |= (UCHAR)(1 << (c & 7));
}

static BOOL ClassHas(const TOKEN *Token, UCHAR c)
{
    return (Token->Class[c >> 3] >> (c & 7)) & 1;
}

static BOOL HasRegexChars(const char *s)
{
    return strpbrk(s, ".*^$[\\") != NULL;
}

/* Compiles a regular expression; FALSE if it is invalid */
static BOOL CompileRegex(PATTERN *Pattern, const char *s)
{
    size_t Length = strlen(s), i;
    TOKEN *Tokens = calloc(Length + 3, sizeof(TOKEN));
    int n = 0;

    if (!Tokens) return FALSE;

    if (bBegin || bExact)
        Tokens[n++].Type = TOK_BOL;

    for (i = 0; i < Length; i++)
    {
        TOKEN *t = &Tokens[n];
        UCHAR c = (UCHAR)s[i];

        if (c == '*' && n > 0 && Tokens[n - 1].Type <= TOK_CLASS && !Tokens[n - 1].Star)
        {
            Tokens[n - 1].Star = TRUE;
            continue;
        }
        if (c == '^' && i == 0)
        {
            if (!(bBegin || bExact)) t->Type = TOK_BOL; else continue;
        }
        else if (c == '$' && i == Length - 1)
        {
            t->Type = TOK_EOL;
        }
        else if (c == '.')
        {
            t->Type = TOK_ANY;
        }
        else if (c == '\\' && i + 1 < Length)
        {
            c = (UCHAR)s[++i];
            if (c == '<') t->Type = TOK_WORDBEG;
            else if (c == '>') t->Type = TOK_WORDEND;
            else
            {
                t->Type = TOK_CHAR;
                t->Char = Fold(c);
            }
        }
        else if (c == '[')
        {
            BOOL Negate = FALSE;
            int b;

            t->Type = TOK_CLASS;
            i++;
            if (i < Length && s[i] == '^')
            {
                Negate = TRUE;
                i++;
            }
            /* A ] right after [ or [^ is a member */
            if (i < Length && s[i] == ']')
            {
                ClassSet(t, ']');
                i++;
            }
            while (i < Length && s[i] != ']')
            {
                UCHAR From = (UCHAR)s[i], To = From;
                if (From == '\\' && i + 1 < Length) From = To = (UCHAR)s[++i];
                if (i + 2 < Length && s[i + 1] == '-' && s[i + 2] != ']')
                {
                    To = (UCHAR)s[i + 2];
                    i += 2;
                }
                for (b = From; b <= To; b++)
                {
                    ClassSet(t, (UCHAR)b);
                    if (bIgnoreCase) ClassSet(t, Fold((UCHAR)b));
                }
                i++;
            }
            if (i >= Length)
            {
                free(Tokens);
                return FALSE;   /* No ] */
            }
            if (bIgnoreCase)
            {
                /* Fold the members: the text is folded too */
                TOKEN Folded = *t;
                memset(Folded.Class, 0, sizeof(Folded.Class));
                for (b = 0; b < 256; b++)
                    if (ClassHas(t, (UCHAR)b)) ClassSet(&Folded, Fold((UCHAR)b));
                memcpy(t->Class, Folded.Class, sizeof(t->Class));
            }
            if (Negate)
            {
                for (b = 0; b < 32; b++) t->Class[b] = (UCHAR)~t->Class[b];
            }
        }
        else
        {
            t->Type = TOK_CHAR;
            t->Char = Fold(c);
        }
        n++;
    }

    if ((bEnd || bExact) && !(n > 0 && Tokens[n - 1].Type == TOK_EOL))
        Tokens[n++].Type = TOK_EOL;

    Pattern->Tokens = Tokens;
    Pattern->Count = n;
    return TRUE;
}

static BOOL TokenMatches(const TOKEN *t, UCHAR c)
{
    switch (t->Type)
    {
    case TOK_CHAR:  return t->Char == c;
    case TOK_ANY:   return TRUE;
    case TOK_CLASS: return ClassHas(t, c);
    }
    return FALSE;
}

/* Backtracking match of Tokens[k...] at Line[Pos] */
static BOOL MatchHere(const TOKEN *Tokens, int k, int Count,
                      const UCHAR *Line, size_t Pos, size_t Length)
{
    for (; k < Count; k++)
    {
        const TOKEN *t = &Tokens[k];

        switch (t->Type)
        {
        case TOK_BOL:
            if (Pos != 0) return FALSE;
            continue;
        case TOK_EOL:
            if (Pos != Length) return FALSE;
            continue;
        case TOK_WORDBEG:
            if ((Pos > 0 && IsWordChar(Line[Pos - 1])) || Pos >= Length || !IsWordChar(Line[Pos]))
                return FALSE;
            continue;
        case TOK_WORDEND:
            if (Pos == 0 || !IsWordChar(Line[Pos - 1]) || (Pos < Length && IsWordChar(Line[Pos])))
                return FALSE;
            continue;
        }

        if (t->Star)
        {
            /* As many as possible, then fewer */
            size_t End = Pos;
            while (End < Length && TokenMatches(t, Line[End])) End++;
            for (;;)
            {
                if (MatchHere(Tokens, k + 1, Count, Line, End, Length)) return TRUE;
                if (End == Pos) return FALSE;
                End--;
            }
        }

        if (Pos >= Length || !TokenMatches(t, Line[Pos])) return FALSE;
        Pos++;
    }
    return TRUE;
}

static BOOL MatchPattern(const PATTERN *Pattern, const UCHAR *Line, size_t Length)
{
    size_t Start;

    if (Pattern->Tokens)
    {
        for (Start = 0; Start <= Length; Start++)
        {
            if (MatchHere(Pattern->Tokens, 0, Pattern->Count, Line, Start, Length))
                return TRUE;
            if (Pattern->Count && Pattern->Tokens[0].Type == TOK_BOL) break;
        }
        return FALSE;
    }

    /* Literal */
    if (bExact || (bBegin && bEnd))
        return Length == Pattern->Length && !memcmp(Line, Pattern->Text, Length);
    if (Pattern->Length > Length) return FALSE;
    if (bBegin && !bEnd) return !memcmp(Line, Pattern->Text, Pattern->Length);
    if (bEnd && !bBegin) return !memcmp(Line + Length - Pattern->Length, Pattern->Text, Pattern->Length);
    for (Start = 0; Start + Pattern->Length <= Length; Start++)
    {
        if (!memcmp(Line + Start, Pattern->Text, Pattern->Length))
            return TRUE;
    }
    return FALSE;
}

static BOOL LineMatches(const UCHAR *Line, size_t Length)
{
    static UCHAR *Folded;
    static size_t FoldedSize;
    const UCHAR *Text = Line;
    int i;

    if (bIgnoreCase)
    {
        if (Length + 1 > FoldedSize)
        {
            free(Folded);
            FoldedSize = Length + 256;
            Folded = malloc(FoldedSize);
            if (!Folded)
            {
                FoldedSize = 0;
                return FALSE;
            }
        }
        memcpy(Folded, Line, Length);
        Folded[Length] = 0;
        CharUpperBuffA((LPSTR)Folded, (DWORD)Length);
        Text = Folded;
    }

    for (i = 0; i < PatternCount; i++)
    {
        if (MatchPattern(&Patterns[i], Text, Length))
            return TRUE;
    }
    return FALSE;
}

static void AddPattern(const char *s)
{
    PATTERN *p;

    if (!*s) return;
    Patterns = realloc(Patterns, (PatternCount + 1) * sizeof(PATTERN));
    if (!Patterns) exit(2);
    p = &Patterns[PatternCount];
    memset(p, 0, sizeof(*p));
    p->Text = _strdup(s);
    p->Length = strlen(s);
    if (bIgnoreCase) CharUpperBuffA(p->Text, (DWORD)p->Length);
    PatternCount++;
}

/* Space separated strings (without /C) */
static void AddPatternList(char *s)
{
    char *Token = strtok(s, " ");
    while (Token)
    {
        AddPattern(Token);
        Token = strtok(NULL, " ");
    }
}

/* One string per line of a file ("/" is the standard input) */
static char **ReadLines(const char *Path, int *Count)
{
    FILE *f = strcmp(Path, "/") ? fopen(Path, "rb") : stdin;
    char Line[4096], **Lines = NULL;

    *Count = 0;
    if (!f)
    {
        PrintResource(IDS_CANNOT_OPEN, Path);
        return NULL;
    }
    while (fgets(Line, sizeof(Line), f))
    {
        Line[strcspn(Line, "\r\n")] = 0;
        Lines = realloc(Lines, (*Count + 1) * sizeof(char *));
        if (!Lines) exit(2);
        Lines[(*Count)++] = _strdup(Line);
    }
    if (f != stdin) fclose(f);
    return Lines;
}

static void SearchStream(FILE *f, const char *Name)
{
    static char *Line;
    static size_t Size;
    size_t Length = 0;
    long LineNumber = 0;
    unsigned long Offset = 0, LineOffset;
    int c;
    BOOL Eof = FALSE;

    if (!Line)
    {
        Size = 4096;
        Line = malloc(Size);
        if (!Line) exit(2);
    }

    while (!Eof)
    {
        /* Read one line, as bytes */
        Length = 0;
        LineOffset = Offset;
        for (;;)
        {
            c = fgetc(f);
            if (c == EOF)
            {
                Eof = TRUE;
                break;
            }
            Offset++;
            if (Length + 2 > Size)
            {
                Size *= 2;
                Line = realloc(Line, Size);
                if (!Line) exit(2);
            }
            Line[Length++] = (char)c;
            if (c == '\n') break;
        }
        if (Length == 0 && Eof) break;
        LineNumber++;

        {
            /* The text without its CR LF */
            size_t TextLength = Length;
            BOOL Match;

            if (TextLength && Line[TextLength - 1] == '\n') TextLength--;
            if (TextLength && Line[TextLength - 1] == '\r') TextLength--;

            if (bSkipBinary)
            {
                size_t i;
                for (i = 0; i < TextLength; i++)
                {
                    UCHAR b = (UCHAR)Line[i];
                    if (b < 0x20 && b != '\t' && b != '\f' && b != 0x1A) return;
                }
            }

            Match = LineMatches((UCHAR *)Line, TextLength);
            if (bInvert) Match = !Match;
            if (!Match) continue;

            MatchedAny = 1;
            if (bFileNames)
            {
                printf("%s\r\n", Name ? Name : "");
                return;
            }
            if (bShowNames && Name) printf("%s:", Name);
            if (bNumbers) printf("%ld:", LineNumber);
            if (bOffsets) printf("%lu:", LineOffset);
            fwrite(Line, 1, TextLength, stdout);
            fputs("\r\n", stdout);
        }
    }
}

static void SearchFile(const char *Path)
{
    FILE *f = fopen(Path, "rb");

    if (!f)
    {
        PrintResource(IDS_CANNOT_OPEN, Path);
        return;
    }
    SearchStream(f, Path);
    fclose(f);
}

/* Files matching Spec (wildcards allowed), and in subfolders with /S */
static BOOL SearchSpec(const char *Dir, const char *Spec)
{
    char Path[MAX_PATH * 2];
    WIN32_FIND_DATAA Data;
    HANDLE Find;
    BOOL Found = FALSE;

    _snprintf(Path, sizeof(Path), "%s%s", Dir, Spec);
    Path[sizeof(Path) - 1] = 0;
    Find = FindFirstFileA(Path, &Data);
    if (Find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            _snprintf(Path, sizeof(Path), "%s%s", Dir, Data.cFileName);
            Path[sizeof(Path) - 1] = 0;
            SearchFile(Path);
            Found = TRUE;
        } while (FindNextFileA(Find, &Data));
        FindClose(Find);
    }

    if (bSubdirs)
    {
        _snprintf(Path, sizeof(Path), "%s*", Dir);
        Path[sizeof(Path) - 1] = 0;
        Find = FindFirstFileA(Path, &Data);
        if (Find != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    !strcmp(Data.cFileName, ".") || !strcmp(Data.cFileName, ".."))
                    continue;
                _snprintf(Path, sizeof(Path), "%s%s\\", Dir, Data.cFileName);
                Path[sizeof(Path) - 1] = 0;
                if (SearchSpec(Path, Spec)) Found = TRUE;
            } while (FindNextFileA(Find, &Data));
            FindClose(Find);
        }
    }
    return Found;
}

/* A file argument: split into folder and name, search it */
static void SearchArgument(const char *Dir, const char *Arg)
{
    char Folder[MAX_PATH * 2];
    const char *Name = strrchr(Arg, '\\'), *Colon = strchr(Arg, ':');

    if (!Name && Colon) Name = Colon;
    _snprintf(Folder, sizeof(Folder), "%s%.*s", Dir, Name ? (int)(Name - Arg + 1) : 0, Arg);
    Folder[sizeof(Folder) - 1] = 0;
    if (!SearchSpec(Folder, Name ? Name + 1 : Arg))
        PrintResource(IDS_NO_SUCH_FILE, Arg);
}

/* PUBLIC FUNCTIONS ***********************************************************/

int main(int argc, char **argv)
{
    char **Strings = NULL, **Files = NULL, **Dirs = NULL;
    int StringCount = 0, FileCount = 0, DirCount = 0, i;
    BOOL HaveC = FALSE, HaveG = FALSE;
    const char *FileListPath = NULL, *StringListPath = NULL;

    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);

    for (i = 1; i < argc; i++)
    {
        char *a = argv[i];

        if ((a[0] == '/' || a[0] == '-') && a[1])
        {
            char *o = a + 1;

            if (!_strnicmp(o, "C:", 2))
            {
                Strings = realloc(Strings, (StringCount + 1) * sizeof(char *));
                if (!Strings) return 2;
                Strings[StringCount++] = o + 2;
                HaveC = TRUE;
                continue;
            }
            if (!_strnicmp(o, "G:", 2)) { StringListPath = o + 2; HaveG = TRUE; continue; }
            if (!_strnicmp(o, "F:", 2)) { FileListPath = o + 2; continue; }
            if (!_strnicmp(o, "D:", 2))
            {
                char *Dir = strtok(o + 2, ";");
                while (Dir)
                {
                    Dirs = realloc(Dirs, (DirCount + 1) * sizeof(char *));
                    if (!Dirs) return 2;
                    Dirs[DirCount++] = Dir;
                    Dir = strtok(NULL, ";");
                }
                continue;
            }
            if (!_strnicmp(o, "A:", 2)) continue;           /* Colors: not shown */
            if (!_strnicmp(o, "OFF", 3)) continue;          /* Offline files: searched */
            if (!strcmp(o, "?"))
            {
                PrintResource(IDS_USAGE, NULL);
                return 0;
            }

            /* Letters, alone or together (/SIN) */
            for (; *o; o++)
            {
                switch (*o | 0x20)
                {
                case 'b': bBegin = TRUE; break;
                case 'e': bEnd = TRUE; break;
                case 'l': bLiteral = TRUE; break;
                case 'r': bRegex = TRUE; break;
                case 's': bSubdirs = TRUE; break;
                case 'i': bIgnoreCase = TRUE; break;
                case 'x': bExact = TRUE; break;
                case 'v': bInvert = TRUE; break;
                case 'n': bNumbers = TRUE; break;
                case 'm': bFileNames = TRUE; break;
                case 'o': bOffsets = TRUE; break;
                case 'p': bSkipBinary = TRUE; break;
                default:
                    fprintf(stderr, "FINDSTR: /%c ignored\n", *o);
                    break;
                }
            }
            continue;
        }

        /* The first plain argument is the strings, unless /C or /G */
        if (!HaveC && !HaveG && StringCount == 0)
        {
            Strings = realloc(Strings, sizeof(char *));
            if (!Strings) return 2;
            Strings[StringCount++] = a;
            continue;
        }
        Files = realloc(Files, (FileCount + 1) * sizeof(char *));
        if (!Files) return 2;
        Files[FileCount++] = a;
    }

    /* /C and /G may come after the files */
    if (HaveC || HaveG)
    {
        /* A first plain argument taken as the strings was a file */
        if (StringCount && !HaveC)
        {
            Files = realloc(Files, (FileCount + 1) * sizeof(char *));
            if (!Files) return 2;
            memmove(Files + 1, Files, FileCount * sizeof(char *));
            Files[0] = Strings[0];
            FileCount++;
            StringCount = 0;
        }
    }
    if (StringListPath)
    {
        int Count, j;
        char **Lines = ReadLines(StringListPath, &Count);
        for (j = 0; Lines && j < Count; j++)
        {
            Strings = realloc(Strings, (StringCount + 1) * sizeof(char *));
            if (!Strings) return 2;
            Strings[StringCount++] = Lines[j];
        }
    }
    if (StringCount == 0)
    {
        PrintResource(IDS_USAGE, NULL);
        return 2;
    }

    /* Literal or regex: as the first string says, unless /L or /R */
    if (!bLiteral && !bRegex) bRegex = HasRegexChars(Strings[0]);
    if (bLiteral) bRegex = FALSE;

    for (i = 0; i < StringCount; i++)
    {
        if (HaveC || HaveG) AddPattern(Strings[i]);
        else AddPatternList(Strings[i]);
    }
    if (bRegex)
    {
        for (i = 0; i < PatternCount; i++)
        {
            /* (With /I the text is already upper case, as the lines will be) */
            char *Original = _strdup(Patterns[i].Text);
            if (!Original) return 2;
            if (!CompileRegex(&Patterns[i], Original))
            {
                fprintf(stderr, "FINDSTR: Bad regular expression: %s\n", Original);
                return 2;
            }
            free(Original);
        }
    }

    if (FileListPath)
    {
        int Count, j;
        char **Lines = ReadLines(FileListPath, &Count);
        for (j = 0; Lines && j < Count; j++)
        {
            Files = realloc(Files, (FileCount + 1) * sizeof(char *));
            if (!Files) return 2;
            Files[FileCount++] = Lines[j];
        }
    }

    if (FileCount == 0)
    {
        SearchStream(stdin, NULL);
        return MatchedAny ? 0 : 1;
    }

    /* Names before the lines when more than one file may match */
    bShowNames = FileCount > 1 || bSubdirs || DirCount > 0;
    for (i = 0; i < FileCount && !bShowNames; i++)
        bShowNames = strpbrk(Files[i], "*?") != NULL;

    for (i = 0; i < FileCount; i++)
    {
        if (DirCount)
        {
            int d;
            for (d = 0; d < DirCount; d++)
            {
                char Dir[MAX_PATH];
                size_t Len = strlen(Dirs[d]);
                _snprintf(Dir, sizeof(Dir), "%s%s", Dirs[d], (Len && Dirs[d][Len - 1] != '\\') ? "\\" : "");
                Dir[sizeof(Dir) - 1] = 0;
                SearchArgument(Dir, Files[i]);
            }
        }
        else
        {
            SearchArgument("", Files[i]);
        }
    }

    return MatchedAny ? 0 : 1;
}

/* EOF */
