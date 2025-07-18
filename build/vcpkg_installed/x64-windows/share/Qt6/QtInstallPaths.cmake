# install layout information, following what qmake -query provides
get_filename_component(QT6_INSTALL_PREFIX
    ${CMAKE_CURRENT_LIST_DIR}/../../ ABSOLUTE)
set(QT6_INSTALL_ARCHDATA ".")
set(QT6_INSTALL_BINS "bin")
set(QT6_INSTALL_CONFIGURATION "etc/xdg")
set(QT6_INSTALL_DATA ".")
set(QT6_INSTALL_DOCS "doc/Qt6")
set(QT6_INSTALL_EXAMPLES "examples")
set(QT6_INSTALL_HEADERS "include/Qt6")
set(QT6_INSTALL_LIBS "lib")
set(QT6_INSTALL_LIBEXECS "bin")
set(QT6_INSTALL_PLUGINS "Qt6/plugins")
set(QT6_INSTALL_QML "Qt6/qml")
set(QT6_INSTALL_TESTS "tests")
set(QT6_INSTALL_TRANSLATIONS "translations/Qt6")
set(QT6_INSTALL_DESCRIPTIONSDIR "share/Qt6/modules")
