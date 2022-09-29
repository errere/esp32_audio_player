
file(GLOB_RECURSE CSRCS "./*.c")

idf_component_register(SRCS ${CSRCS}
                       INCLUDE_DIRS "include"
                       )
