

RESOURCES_LIBRARY()

IF(GOSTD_VERSION STREQUAL 1.12.6)
    IF (HOST_OS_LINUX)
        DECLARE_EXTERNAL_RESOURCE(GO_TOOLS sbr:988803718)
    ELSEIF (HOST_OS_DARWIN)
        DECLARE_EXTERNAL_RESOURCE(GO_TOOLS sbr:988807464)
    ELSEIF (HOST_OS_WINDOWS)
        DECLARE_EXTERNAL_RESOURCE(GO_TOOLS sbr:988812329)
    ELSE()
        MESSAGE(FATAL_ERROR Unsupported host platform)
    ENDIF()
ELSEIF(GOSTD_VERSION STREQUAL 1.12.7)
    IF (HOST_OS_LINUX)
        DECLARE_EXTERNAL_RESOURCE(GO_TOOLS sbr:1026648961)
    ELSEIF (HOST_OS_DARWIN)
        DECLARE_EXTERNAL_RESOURCE(GO_TOOLS sbr:1026650357)
    ELSEIF (HOST_OS_WINDOWS)
        DECLARE_EXTERNAL_RESOURCE(GO_TOOLS sbr:1026651810)
    ELSE()
        MESSAGE(FATAL_ERROR Unsupported host platform)
    ENDIF()
ELSE()
    MESSAGE(FATAL_ERROR Unsupported version [$GOSTD] of Go Standard Library)
ENDIF()

END()
