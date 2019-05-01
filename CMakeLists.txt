project(CommonLib)

set(header_files
    ./inc/circular_list.h
)

set(source_files
    ./src/circular_list.h
)

add_library(CommonLib ${header_files} ${source_files})


target_link_libraries(CommonLib 
    
)

#add_subdirectory(tests)

