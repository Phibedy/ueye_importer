set ( SOURCES 
    "src/ueye_importer.cpp"
    "src/ueye_camera.cpp"
    "src/interface.cpp"
)

set (HEADERS 
    "include/ueye_importer.h"
    "include/ueye_camera.h"
    ${HEADERS_SHARED}
)

include_directories("include")

# Debug flag
# add_definitions(-DUEYE_DEBUG)

add_library ( ueye_importer MODULE ${SOURCES} ${HEADERS})
target_link_libraries(ueye_importer PRIVATE lms imaging ueye_api)
