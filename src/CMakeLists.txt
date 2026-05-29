add_executable(segd2segy
    main.cpp
    segy_writer.cpp
)

target_link_libraries(segd2segy PRIVATE segdcore)

target_compile_options(segd2segy PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

install(TARGETS segd2segy RUNTIME DESTINATION bin)
