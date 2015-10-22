function(make_pkg_config
    libname libs requires private_libs private_requires additional_flags
)
    SET(PKG_CONFIG_REQUIRES "")
    foreach(req IN LISTS requires)
        SET(PKG_CONFIG_REQUIRES
            "${PKG_CONFIG_REQUIRES}, ${req}"
        )
    endforeach(req)

    SET(PKG_CONFIG_LIBDIR
        "\${prefix}/${CMAKE_INSTALL_LIBDIR}"
    )

    SET(PKG_CONFIG_INCLUDEDIR
        "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}"
    )

    SET(PKG_CONFIG_LIBS
        "-L\${libdir}"
    )
    foreach(lib IN LISTS libs)
        SET(PKG_CONFIG_LIBS
            "${PKG_CONFIG_LIBS} -l${lib}"
        )
    endforeach(lib)

    SET(PKG_CONFIG_CFLAGS
        "-I\${includedir} ${additional_flags}"
    )

    SET(PKG_CONFIG_REQUIRES_PRIVATE "")
    foreach(priv_req IN LISTS private_requires)
        SET(PKG_CONFIG_REQUIRES_PRIVATE
            "${PKG_CONFIG_REQUIRES_PRIVATE}, ${priv_req}"
        )
    endforeach(priv_req)

    SET(PKG_CONFIG_LIBS_PRIVATE
        ${private_libs}
    )

    CONFIGURE_FILE(
      "${CMAKE_SOURCE_DIR}/config/pkgconfig.pc.in"
      "${CMAKE_BINARY_DIR}/${libname}.pc"
      @ONLY
    )



    INSTALL(FILES "${CMAKE_BINARY_DIR}/${libname}.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endfunction(make_pkg_config)
