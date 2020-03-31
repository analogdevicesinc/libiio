if (WITH_MAN)
    find_program(BASH_EXECUTABLE bash)
    find_program(DATE_EXECUTABLE date)
    execute_process(
        COMMAND ${DATE_EXECUTABLE} "+%d %B %Y"
        OUTPUT_VARIABLE CMAKE_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
        COMMAND
            ${BASH_EXECUTABLE} "-c"
            "${CMAKE_CURRENT_SOURCE_DIR}/make_man.sh > ${CMAKE_BINARY_DIR}/libiio.3.in"
    )
    configure_file(${CMAKE_BINARY_DIR}/libiio.3.in
                   ${CMAKE_BINARY_DIR}/man/libiio.3 @ONLY)

    if (WITH_TESTS)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/iio_attr.1.in
                       ${CMAKE_BINARY_DIR}/man/iio_attr.1 @ONLY)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/iio_info.1.in
                       ${CMAKE_BINARY_DIR}/man/iio_info.1 @ONLY)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/iio_readdev.1.in
                       ${CMAKE_BINARY_DIR}/man/iio_readdev.1 @ONLY)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/iio_reg.1.in
                       ${CMAKE_BINARY_DIR}/man/iio_reg.1 @ONLY)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/iio_writedev.1.in
                       ${CMAKE_BINARY_DIR}/man/iio_writedev.1 @ONLY)
    endif ()
    # install man files into the BINARY directories, section 3 = library
    # functions
    install(
        DIRECTORY ${CMAKE_BINARY_DIR}/man/
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man3
        COMPONENT doc
        FILES_MATCHING
        PATTERN "*.3*")
    # section 1 = user commands
    install(
        DIRECTORY ${CMAKE_BINARY_DIR}/man/
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
        COMPONENT doc
        FILES_MATCHING
        PATTERN "*.1*")

endif ()
