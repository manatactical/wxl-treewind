# wxl-treewind: per-extension build glue, included by the core's extension loop after the target is
# created. The core copies the DLL itself; the only extra step is deploying the INI beside it, so the
# module has a file to read at load and the user has one to edit while the client runs.

if(CLIENT_PATH)
    add_custom_command(TARGET ${wxl_ext_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CLIENT_PATH}/Extensions/${wxl_ext_name}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${wxl_ext_dir}/wxl-treewind.ini"
                "${CLIENT_PATH}/Extensions/${wxl_ext_name}/wxl-treewind.ini"
        COMMENT "Deploy wxl-treewind config -> ${CLIENT_PATH}/Extensions/${wxl_ext_name}")
endif()
