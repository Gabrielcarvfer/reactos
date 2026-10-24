
add_definitions(-DUSE_WINE_TODOS)
add_executable(bcp47langs_winetest bcp47langs.c testlist.c)
set_module_type(bcp47langs_winetest win32cui)
add_importlibs(bcp47langs_winetest bcp47langs combase msvcrt kernel32)
add_rostests_file(TARGET bcp47langs_winetest)
