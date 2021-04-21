add_library(mlog-lib STATIC)
target_include_directories(mlog-lib PUBLIC .)
target_sources(mlog-lib PRIVATE mlog.c)
