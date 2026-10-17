
add_executable(prntvpt_winetest prntvpt.c testlist.c)
set_module_type(prntvpt_winetest win32cui)
add_importlibs(prntvpt_winetest prntvpt winspool ole32 oleaut32 msvcrt kernel32)
add_rostests_file(TARGET prntvpt_winetest)
