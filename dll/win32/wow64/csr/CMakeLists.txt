
# The CSR client of 32-bit programs (built with the NT headers, not Wine's)
include_directories(${REACTOS_SOURCE_DIR}/sdk/include/reactos/subsys)
add_library(wow64_csr csr.c)
target_link_libraries(wow64_csr pseh)
add_dependencies(wow64_csr psdk xdk)
