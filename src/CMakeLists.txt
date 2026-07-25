add_library(runyard_domain domain/model.cpp)
target_include_directories(runyard_domain PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(runyard_domain PUBLIC runyard_options)
