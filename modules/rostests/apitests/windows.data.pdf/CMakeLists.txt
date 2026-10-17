
add_executable(windows.data.pdf_apitest PdfInterop.c testlist.c)
target_link_libraries(windows.data.pdf_apitest wine)
set_module_type(windows.data.pdf_apitest win32cui)
add_importlibs(windows.data.pdf_apitest combase msvcrt kernel32)
add_rostests_file(TARGET windows.data.pdf_apitest)
