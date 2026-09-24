# Standalone core test: runs every processing operation on a sample image,
# saves the results as PNG and prints timings on one thread and on all of them.
qt_add_executable(kolorit_selftest
    selftest.cpp
    TestImage.h
    ../resources/kolorit.qrc
)
target_link_libraries(kolorit_selftest PRIVATE kolorit_core)

add_test(NAME selftest
    COMMAND kolorit_selftest - "${CMAKE_BINARY_DIR}/selftest_out")
set_tests_properties(selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Offscreen interface test: opens the main window and the dialogs without a display
# and saves snapshots of them as PNG to review the layout.
qt_add_executable(kolorit_uitest
    uitest.cpp
    TestImage.h
    ../resources/kolorit.qrc
)
target_link_libraries(kolorit_uitest PRIVATE kolorit_ui)

add_test(NAME uitest
    COMMAND kolorit_uitest - "${CMAKE_BINARY_DIR}/uitest_out")
set_tests_properties(uitest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
