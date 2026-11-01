add_executable(ime-probe main.cpp)
target_link_libraries(ime-probe PRIVATE imm32 oleacc ole32 oleaut32 user32 shell32)
