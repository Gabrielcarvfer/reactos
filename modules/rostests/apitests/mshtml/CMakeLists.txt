
add_executable(mshtml_apitest ShowHTMLDialogEx.c testlist.c)
set_module_type(mshtml_apitest win32cui)
add_importlibs(mshtml_apitest msvcrt kernel32)
add_rostests_file(TARGET mshtml_apitest)
