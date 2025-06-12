project(examples)

# Create the executable
add_executable(simple-extractor src/simple-extractor.cpp)
add_executable(test src/test.cpp)

target_link_libraries(simple-extractor
    gw2dattools
)

target_link_libraries(test
    gw2dattools
)
