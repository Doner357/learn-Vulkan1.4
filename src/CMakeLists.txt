add_subdirectory(gplab)

add_executable(
    main
    "main.cpp"
)

target_link_libraries(
    main
    PRIVATE
        graphic-lab
)

add_dependencies(
    graphic-lab
    shader 
)

graphic_lab_apply_project_settings(main)
