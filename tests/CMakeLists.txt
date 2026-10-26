find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_databasehelper tst_databasehelper.cpp)
target_link_libraries(tst_databasehelper PRIVATE kalibreboxcore Qt6::Test)
add_test(NAME tst_databasehelper COMMAND tst_databasehelper)

qt_add_executable(tst_libraryrepository tst_libraryrepository.cpp)
target_link_libraries(tst_libraryrepository PRIVATE kalibreboxcore Qt6::Test)
add_test(NAME tst_libraryrepository COMMAND tst_libraryrepository)

qt_add_executable(tst_librarymodel tst_librarymodel.cpp)
target_link_libraries(tst_librarymodel PRIVATE kalibreboxcore Qt6::Test)
add_test(NAME tst_librarymodel COMMAND tst_librarymodel)

qt_add_executable(tst_textnormalization tst_textnormalization.cpp)
target_link_libraries(tst_textnormalization PRIVATE kalibreboxcore Qt6::Test)
add_test(NAME tst_textnormalization COMMAND tst_textnormalization)

qt_add_executable(tst_dropboxurls tst_dropboxurls.cpp)
target_link_libraries(tst_dropboxurls PRIVATE kalibreboxcore Qt6::Test)
add_test(NAME tst_dropboxurls COMMAND tst_dropboxurls)

qt_add_executable(tst_dropboxcredential tst_dropboxcredential.cpp)
target_link_libraries(tst_dropboxcredential PRIVATE kalibreboxcore Qt6::Test)
add_test(NAME tst_dropboxcredential COMMAND tst_dropboxcredential)
