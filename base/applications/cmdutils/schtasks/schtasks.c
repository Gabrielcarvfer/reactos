/*
 * Copyright 2012 Detlef Riekenberg
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS

#include "initguid.h"
#include "taskschd.h"

#include <stdio.h>

#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(schtasks);

static const WCHAR change_optW[] = {'/','c','h','a','n','g','e',0};
static const WCHAR create_optW[] = {'/','c','r','e','a','t','e',0};
static const WCHAR delete_optW[] = {'/','d','e','l','e','t','e',0};
static const WCHAR enable_optW[] = {'/','e','n','a','b','l','e',0};
static const WCHAR f_optW[] = {'/','f',0};
static const WCHAR tn_optW[] = {'/','t','n',0};
static const WCHAR tr_optW[] = {'/','t','r',0};

static ITaskFolder *get_tasks_root_folder(void)
{
    ITaskService *service;
    ITaskFolder *root;
    VARIANT empty;
    HRESULT hres;

    hres = CoCreateInstance(&CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER,
                            &IID_ITaskService, (void**)&service);
    if (FAILED(hres))
        return NULL;

    V_VT(&empty) = VT_EMPTY;
    hres = ITaskService_Connect(service, empty, empty, empty, empty);
    if (FAILED(hres)) {
        FIXME("Connect failed: %08x\n", hres);
        return NULL;
    }

    hres = ITaskService_GetFolder(service, NULL, &root);
    ITaskService_Release(service);
    if (FAILED(hres)) {
        FIXME("GetFolder failed: %08x\n", hres);
        return NULL;
    }

    return root;
}

static IRegisteredTask *get_registered_task(const WCHAR *name)
{
    IRegisteredTask *registered_task;
    ITaskFolder *root;
    BSTR str;
    HRESULT hres;

    root = get_tasks_root_folder();
    if (!root)
        return NULL;

    str = SysAllocString(name);
    hres = ITaskFolder_GetTask(root, str, &registered_task);
    SysFreeString(str);
    ITaskFolder_Release(root);
    if (FAILED(hres)) {
        FIXME("GetTask failed: %08x\n", hres);
        return NULL;
    }

    return registered_task;
}

static BSTR read_file_to_bstr(const WCHAR *file_name)
{
    LARGE_INTEGER file_size;
    DWORD read_size, size;
    unsigned char *data;
    HANDLE file;
    BOOL r = FALSE;
    BSTR ret;

    file = CreateFileW(file_name, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        FIXME("Could not open file\n");
        return NULL;
    }

    if (!GetFileSizeEx(file, &file_size) || !file_size.QuadPart) {
        FIXME("Empty file\n");
        CloseHandle(file);
        return NULL;
    }

    data = HeapAlloc(GetProcessHeap(), 0, file_size.QuadPart);
    if (data)
        r = ReadFile(file, data, file_size.QuadPart, &read_size, NULL);
    CloseHandle(file);
    if (!r) {
        FIXME("Read filed\n");
        HeapFree(GetProcessHeap(), 0, data);
        return NULL;
    }

    if (read_size > 2 && data[0] == 0xff && data[1] == 0xfe) { /* UTF-16 BOM */
        ret = SysAllocStringLen((const WCHAR *)(data + 2), (read_size - 2) / sizeof(WCHAR));
    }else {
        size = MultiByteToWideChar(CP_ACP, 0, (const char *)data, read_size, NULL, 0);
        ret = SysAllocStringLen(NULL, size);
        if (ret)
            MultiByteToWideChar(CP_ACP, 0, (const char *)data, read_size, ret, size);
    }
    HeapFree(GetProcessHeap(), 0, data);

    return ret;
}

static int change_command(int argc, WCHAR *argv[])
{
    BOOL have_option = FALSE, enable = FALSE;
    const WCHAR *task_name = NULL;
    IRegisteredTask *task;
    HRESULT hres;

    while (argc) {
        if(!strcmpiW(argv[0], tn_optW)) {
            if (argc < 2) {
                FIXME("Missing /tn value\n");
                return 1;
            }

            if (task_name) {
                FIXME("Duplicated /tn argument\n");
                return 1;
            }

            task_name = argv[1];
            argc -= 2;
            argv += 2;
        }else if (!strcmpiW(argv[0], enable_optW)) {
            enable = TRUE;
            have_option = TRUE;
            argc--;
            argv++;
        }else if (!strcmpiW(argv[0], tr_optW)) {
            if (argc < 2) {
                FIXME("Missing /tr value\n");
                return 1;
            }

            FIXME("Unsupported /tr option %s\n", debugstr_w(argv[1]));
            have_option = TRUE;
            argc -= 2;
            argv += 2;
        }else {
            FIXME("Unsupported arguments %s\n", debugstr_w(argv[0]));
            return 1;
        }
    }

    if (!task_name) {
        FIXME("Missing /tn option\n");
        return 1;
    }

    if (!have_option) {
        FIXME("Missing change options\n");
        return 1;
    }

    task = get_registered_task(task_name);
    if (!task)
        return 1;

    if (enable) {
        hres = IRegisteredTask_put_Enabled(task, VARIANT_TRUE);
        if (FAILED(hres)) {
            IRegisteredTask_Release(task);
            FIXME("put_Enabled failed: %08x\n", hres);
            return 1;
        }
    }

    IRegisteredTask_Release(task);
    return 0;
}

/* A growing string for the task XML that /create builds from its options */
typedef struct
{
    WCHAR *buf;
    size_t len, size;
} strbuf;

static BOOL append(strbuf *s, const WCHAR *str)
{
    size_t n = lstrlenW(str);

    if (s->len + n + 1 > s->size)
    {
        size_t size = max(s->size * 2, s->len + n + 256);
        WCHAR *buf = s->buf ? HeapReAlloc(GetProcessHeap(), 0, s->buf, size * sizeof(WCHAR))
                            : HeapAlloc(GetProcessHeap(), 0, size * sizeof(WCHAR));
        if (!buf) return FALSE;
        s->buf = buf;
        s->size = size;
    }
    memcpy(s->buf + s->len, str, (n + 1) * sizeof(WCHAR));
    s->len += n;
    return TRUE;
}

/* Appends the text with XML's special characters escaped */
static BOOL append_escaped(strbuf *s, const WCHAR *str, size_t count)
{
    WCHAR one[2] = {0};
    BOOL ok = TRUE;
    size_t i;

    for (i = 0; ok && i < count && str[i]; i++)
    {
        switch (str[i])
        {
        case '&': ok = append(s, L"&amp;"); break;
        case '<': ok = append(s, L"&lt;"); break;
        case '>': ok = append(s, L"&gt;"); break;
        case '"': ok = append(s, L"&quot;"); break;
        default: one[0] = str[i]; ok = append(s, one); break;
        }
    }
    return ok;
}

static BOOL append_element(strbuf *s, const WCHAR *name, const WCHAR *value, size_t count)
{
    return append(s, L"<") && append(s, name) && append(s, L">") &&
           append_escaped(s, value, count) &&
           append(s, L"</") && append(s, name) && append(s, L">");
}

/* /st HH:mm[:ss] */
static BOOL parse_time(const WCHAR *str, SYSTEMTIME *st)
{
    int h, m, sec = 0;

    if (swscanf(str, L"%d:%d:%d", &h, &m, &sec) < 2 || h < 0 || h > 23 || m < 0 || m > 59 || sec < 0 || sec > 59)
        return FALSE;
    st->wHour = h;
    st->wMinute = m;
    st->wSecond = sec;
    return TRUE;
}

/* /sd and /ed: mm/dd/yyyy like Windows' English version, or yyyy/mm/dd */
static BOOL parse_date(const WCHAR *str, SYSTEMTIME *st)
{
    int a, b, c;
    WCHAR sep1, sep2;

    if (swscanf(str, L"%d%c%d%c%d", &a, &sep1, &b, &sep2, &c) != 5)
        return FALSE;
    if (a > 31)
    {
        st->wYear = a; st->wMonth = b; st->wDay = c;
    }
    else
    {
        st->wMonth = a; st->wDay = b; st->wYear = c;
    }
    return st->wMonth >= 1 && st->wMonth <= 12 && st->wDay >= 1 && st->wDay <= 31 && st->wYear >= 1601;
}

static void format_boundary(const SYSTEMTIME *st, WCHAR *buf, size_t count)
{
    _snwprintf(buf, count, L"%04u-%02u-%02uT%02u:%02u:%02u",
               st->wYear, st->wMonth, st->wDay, st->wHour, st->wMinute, st->wSecond);
    buf[count - 1] = 0;
}

static const WCHAR *days_of_week[] = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
                                       L"Thursday", L"Friday", L"Saturday" };

/* /d MON,TUE,... (or *) for weekly schedules */
static BOOL append_days_of_week(strbuf *s, const WCHAR *days, WORD today)
{
    BOOL ok = append(s, L"<DaysOfWeek>"), any = FALSE;
    int i;

    for (i = 0; ok && i < 7; i++)
    {
        BOOL set;
        if (!days)
            set = (i == today);
        else if (!lstrcmpW(days, L"*"))
            set = TRUE;
        else
        {
            WCHAR abbr[4];
            const WCHAR *p;
            lstrcpynW(abbr, days_of_week[i], 4);
            set = FALSE;
            for (p = days; *p; )
            {
                if (!_wcsnicmp(p, abbr, 3) && (!p[3] || p[3] == ','))
                    set = TRUE;
                p = wcschr(p, ',');
                if (!p) break;
                p++;
            }
        }
        if (set)
        {
            ok = append(s, L"<") && append(s, days_of_week[i]) && append(s, L" />");
            any = TRUE;
        }
    }
    return ok && any && append(s, L"</DaysOfWeek>");
}

static BOOL append_trigger(strbuf *s, const WCHAR *schedule, int modifier, const WCHAR *days,
                           const WCHAR *start, const WCHAR *end, WORD today)
{
    WCHAR num[16];
    BOOL ok;

    _snwprintf(num, 16, L"%d", modifier);
    num[15] = 0;

#define BOUNDARIES (append_element(s, L"StartBoundary", start, ~(size_t)0) && \
                    (!end || append_element(s, L"EndBoundary", end, ~(size_t)0)) && \
                    append(s, L"<Enabled>true</Enabled>"))

    if (!_wcsicmp(schedule, L"ONSTART"))
        ok = append(s, L"<BootTrigger><Enabled>true</Enabled></BootTrigger>");
    else if (!_wcsicmp(schedule, L"ONLOGON"))
        ok = append(s, L"<LogonTrigger><Enabled>true</Enabled></LogonTrigger>");
    else if (!_wcsicmp(schedule, L"ONIDLE"))
        ok = append(s, L"<IdleTrigger><Enabled>true</Enabled></IdleTrigger>");
    else if (!_wcsicmp(schedule, L"ONCE"))
        ok = append(s, L"<TimeTrigger>") && BOUNDARIES && append(s, L"</TimeTrigger>");
    else if (!_wcsicmp(schedule, L"MINUTE") || !_wcsicmp(schedule, L"HOURLY"))
        ok = append(s, L"<TimeTrigger><Repetition><Interval>PT") && append(s, num) &&
             append(s, _wcsicmp(schedule, L"MINUTE") ? L"H" : L"M") &&
             append(s, L"</Interval><StopAtDurationEnd>false</StopAtDurationEnd></Repetition>") &&
             BOUNDARIES && append(s, L"</TimeTrigger>");
    else if (!_wcsicmp(schedule, L"DAILY"))
        ok = append(s, L"<CalendarTrigger>") && BOUNDARIES &&
             append(s, L"<ScheduleByDay><DaysInterval>") && append(s, num) &&
             append(s, L"</DaysInterval></ScheduleByDay></CalendarTrigger>");
    else if (!_wcsicmp(schedule, L"WEEKLY"))
        ok = append(s, L"<CalendarTrigger>") && BOUNDARIES &&
             append(s, L"<ScheduleByWeek><WeeksInterval>") && append(s, num) &&
             append(s, L"</WeeksInterval>") && append_days_of_week(s, days, today) &&
             append(s, L"</ScheduleByWeek></CalendarTrigger>");
    else if (!_wcsicmp(schedule, L"MONTHLY"))
        ok = append(s, L"<CalendarTrigger>") && BOUNDARIES &&
             append(s, L"<ScheduleByMonth><DaysOfMonth><Day>") && append(s, days ? days : L"1") &&
             append(s, L"</Day></DaysOfMonth><Months><January /><February /><March /><April /><May />"
                       L"<June /><July /><August /><September /><October /><November /><December />"
                       L"</Months></ScheduleByMonth></CalendarTrigger>");
    else
    {
        FIXME("Unsupported /sc value %s\n", debugstr_w(schedule));
        return FALSE;
    }
#undef BOUNDARIES
    return ok;
}

/* The task XML for "/create /tr command /sc schedule ..." */
static BSTR build_task_xml(const WCHAR *task_run, const WCHAR *schedule, int modifier, const WCHAR *days,
                           const WCHAR *start_time, const WCHAR *start_date, const WCHAR *end_date,
                           const WCHAR *run_as, const WCHAR *run_level, BOOL interactive)
{
    WCHAR start[32], end[32];
    SYSTEMTIME now, st;
    const WCHAR *args;
    size_t command_len;
    strbuf s = {0};
    BOOL ok, system;
    BSTR ret = NULL;

    GetLocalTime(&now);
    st = now;
    st.wSecond = 0;
    if (start_time && !parse_time(start_time, &st))
    {
        FIXME("Invalid /st value %s\n", debugstr_w(start_time));
        return NULL;
    }
    if (start_date && !parse_date(start_date, &st))
    {
        FIXME("Invalid /sd value %s\n", debugstr_w(start_date));
        return NULL;
    }
    format_boundary(&st, start, sizeof(start) / sizeof(start[0]));
    if (end_date)
    {
        SYSTEMTIME et = st;
        if (!parse_date(end_date, &et))
        {
            FIXME("Invalid /ed value %s\n", debugstr_w(end_date));
            return NULL;
        }
        et.wHour = 23; et.wMinute = 59; et.wSecond = 59;
        format_boundary(&et, end, sizeof(end) / sizeof(end[0]));
    }

    /* The program is the quoted first part of /tr, or the part up to the first space */
    while (*task_run == ' ') task_run++;
    if (*task_run == '"')
    {
        const WCHAR *quote = wcschr(task_run + 1, '"');
        task_run++;
        command_len = quote ? quote - task_run : lstrlenW(task_run);
        args = quote ? quote + 1 : task_run + command_len;
    }
    else
    {
        const WCHAR *space = wcschr(task_run, ' ');
        command_len = space ? space - task_run : lstrlenW(task_run);
        args = task_run + command_len;
    }
    while (*args == ' ') args++;

    system = run_as && (!_wcsicmp(run_as, L"SYSTEM") || !_wcsicmp(run_as, L"NT AUTHORITY\\SYSTEM"));

    ok = append(&s, L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>"
                    L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                    L"<RegistrationInfo /><Triggers>") &&
         append_trigger(&s, schedule, modifier, days, start, end_date ? end : NULL, now.wDayOfWeek) &&
         append(&s, L"</Triggers><Principals><Principal id=\"Author\">");
    if (ok && system)
        ok = append(&s, L"<UserId>S-1-5-18</UserId>");
    else if (ok && run_as && *run_as)
        ok = append_element(&s, L"UserId", run_as, ~(size_t)0) &&
             append(&s, interactive ? L"<LogonType>InteractiveToken</LogonType>" : L"<LogonType>Password</LogonType>");
    else if (ok)
        ok = append(&s, L"<LogonType>InteractiveToken</LogonType>");
    if (ok && run_level)
        ok = append(&s, !_wcsicmp(run_level, L"HIGHEST") ? L"<RunLevel>HighestAvailable</RunLevel>"
                                                         : L"<RunLevel>LeastPrivilege</RunLevel>");
    ok = ok && append(&s, L"</Principal></Principals><Settings><Enabled>true</Enabled></Settings>"
                          L"<Actions Context=\"Author\"><Exec>") &&
         append_element(&s, L"Command", task_run, command_len) &&
         (!*args || append_element(&s, L"Arguments", args, ~(size_t)0)) &&
         append(&s, L"</Exec></Actions></Task>");

    if (ok)
        ret = SysAllocString(s.buf);
    HeapFree(GetProcessHeap(), 0, s.buf);
    TRACE("%s\n", debugstr_w(ret));
    return ret;
}

/* Takes the value of an option that needs one */
static const WCHAR *option_value(int *argc, WCHAR **argv[])
{
    const WCHAR *value;

    if (*argc < 2)
    {
        FIXME("Missing %s value\n", debugstr_w((*argv)[0]));
        return NULL;
    }
    value = (*argv)[1];
    *argc -= 2;
    *argv += 2;
    return value;
}

static int create_command(int argc, WCHAR *argv[])
{
    const WCHAR *task_name = NULL, *xml_file = NULL, *task_run = NULL, *schedule = NULL;
    const WCHAR *days = NULL, *start_time = NULL, *start_date = NULL, *end_date = NULL;
    const WCHAR *run_as = NULL, *run_level = NULL, *modifier = NULL;
    BOOL interactive = FALSE;
    ITaskFolder *root = NULL;
    LONG flags = TASK_CREATE;
    IRegisteredTask *task;
    VARIANT empty;
    BSTR str, xml;
    HRESULT hres;

    while (argc) {
        const WCHAR *opt = argv[0], **value = NULL;

        if (!_wcsicmp(opt, L"/xml"))
            value = &xml_file;
        else if (!_wcsicmp(opt, L"/tn"))
            value = &task_name;
        else if (!_wcsicmp(opt, L"/tr"))
            value = &task_run;
        else if (!_wcsicmp(opt, L"/sc"))
            value = &schedule;
        else if (!_wcsicmp(opt, L"/mo"))
            value = &modifier;
        else if (!_wcsicmp(opt, L"/d"))
            value = &days;
        else if (!_wcsicmp(opt, L"/st"))
            value = &start_time;
        else if (!_wcsicmp(opt, L"/sd"))
            value = &start_date;
        else if (!_wcsicmp(opt, L"/ed"))
            value = &end_date;
        else if (!_wcsicmp(opt, L"/ru"))
            value = &run_as;
        else if (!_wcsicmp(opt, L"/rl"))
            value = &run_level;
        else if (!_wcsicmp(opt, L"/rp") || !_wcsicmp(opt, L"/delay") || !_wcsicmp(opt, L"/et") ||
                 !_wcsicmp(opt, L"/du") || !_wcsicmp(opt, L"/m") || !_wcsicmp(opt, L"/i") ||
                 !_wcsicmp(opt, L"/ri")) {
            /* (accepted, not part of the task we create) */
            FIXME("Ignoring %s %s\n", debugstr_w(opt), debugstr_w(argc > 1 ? argv[1] : NULL));
            if (!option_value(&argc, &argv))
                return 1;
            continue;
        }else if (!_wcsicmp(opt, L"/f")) {
            flags = TASK_CREATE_OR_UPDATE;
            argc--;
            argv++;
            continue;
        }else if (!_wcsicmp(opt, L"/it")) {
            interactive = TRUE;
            argc--;
            argv++;
            continue;
        }else if (!_wcsicmp(opt, L"/np") || !_wcsicmp(opt, L"/z") || !_wcsicmp(opt, L"/k") ||
                  !_wcsicmp(opt, L"/v1") || !_wcsicmp(opt, L"/hresult")) {
            TRACE("Ignoring %s\n", debugstr_w(opt));
            argc--;
            argv++;
            continue;
        }else {
            FIXME("Unsupported argument %s\n", debugstr_w(opt));
            return 1;
        }

        if (*value) {
            FIXME("Duplicated %s argument\n", debugstr_w(opt));
            return 1;
        }
        if (!(*value = option_value(&argc, &argv)))
            return 1;
    }

    if (!task_name) {
        FIXME("Missing /tn argument\n");
        return 1;
    }

    if (xml_file) {
        xml = read_file_to_bstr(xml_file);
    }else if (task_run && schedule) {
        int mo = modifier ? _wtoi(modifier) : 1;
        if (mo <= 0) {
            FIXME("Invalid /mo value %s\n", debugstr_w(modifier));
            return 1;
        }
        xml = build_task_xml(task_run, schedule, mo, days, start_time, start_date, end_date,
                             run_as, run_level, interactive);
    }else {
        FIXME("Missing /xml argument, or /tr and /sc\n");
        return 1;
    }
    if (!xml)
        return 1;

    root = get_tasks_root_folder();
    if (!root) {
        SysFreeString(xml);
        return 1;
    }

    V_VT(&empty) = VT_EMPTY;
    str = SysAllocString(task_name);
    hres = ITaskFolder_RegisterTask(root, str, xml, flags, empty, empty,
                                    TASK_LOGON_NONE, empty, &task);
    SysFreeString(str);
    SysFreeString(xml);
    ITaskFolder_Release(root);
    if (FAILED(hres)) {
        FIXME("RegisterTask failed: %08x\n", hres);
        return 1;
    }

    IRegisteredTask_Release(task);
    return 0;
}

static int delete_command(int argc, WCHAR *argv[])
{
    const WCHAR *task_name = NULL;
    ITaskFolder *root = NULL;
    BSTR str;
    HRESULT hres;

    while (argc) {
        if (!strcmpiW(argv[0], f_optW)) {
            TRACE("force opt\n");
            argc--;
            argv++;
        }else if(!strcmpiW(argv[0], tn_optW)) {
            if (argc < 2) {
                FIXME("Missing /tn value\n");
                return 1;
            }

            if (task_name) {
                FIXME("Duplicated /tn argument\n");
                return 1;
            }

            task_name = argv[1];
            argc -= 2;
            argv += 2;
        }else {
            FIXME("Unsupported argument %s\n", debugstr_w(argv[0]));
            return 1;
        }
    }

    if (!task_name) {
        FIXME("Missing /tn argument\n");
        return 1;
    }

    root = get_tasks_root_folder();
    if (!root)
        return 1;

    str = SysAllocString(task_name);
    hres = ITaskFolder_DeleteTask(root, str, 0);
    SysFreeString(str);
    ITaskFolder_Release(root);
    if (FAILED(hres))
        return 1;

    return 0;
}

int wmain(int argc, WCHAR *argv[])
{
    int i, ret = 0;

    for (i = 0; i < argc; i++)
        TRACE(" %s", wine_dbgstr_w(argv[i]));
    TRACE("\n");

    CoInitialize(NULL);

    if (argc < 2)
        FIXME("Print current tasks state\n");
    else if (!strcmpiW(argv[1], change_optW))
        ret = change_command(argc - 2, argv + 2);
    else if (!strcmpiW(argv[1], create_optW))
        ret = create_command(argc - 2, argv + 2);
    else if (!strcmpiW(argv[1], delete_optW))
        ret = delete_command(argc - 2, argv + 2);
    else
        FIXME("Unsupported command %s\n", debugstr_w(argv[1]));

    CoUninitialize();
    return ret;
}
