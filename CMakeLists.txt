

file(GLOB_RECURSE COMMAND_SRC Src/*.c)


add_library("Command" ${COMMAND_SRC})
add_library("windwolf::Command" ALIAS "Command")


target_link_libraries("Command"
    PUBLIC
        azrtos::threadx)

target_include_directories("Command"
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Inc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Src)
