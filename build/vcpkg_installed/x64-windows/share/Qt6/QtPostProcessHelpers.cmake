# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

function(qt_internal_write_depends_file target)
    get_target_property(module_depends_header ${target} _qt_module_depends_header)
    set(outfile "${module_depends_header}")
    set(contents "/* This file was generated by cmake with the info from ${target} target. */\n")
    string(APPEND contents "#ifdef __cplusplus /* create empty PCH in C mode */\n")
    foreach (m ${ARGN})
        string(APPEND contents "#  include <${m}/${m}>\n")
    endforeach()
    string(APPEND contents "#endif\n")

    file(GENERATE OUTPUT "${outfile}" CONTENT "${contents}")
endfunction()

macro(qt_collect_third_party_deps target)
    set(_target_is_static OFF)
    get_target_property(_target_type ${target} TYPE)
    if (${_target_type} STREQUAL "STATIC_LIBRARY")
        set(_target_is_static ON)
    endif()
    unset(_target_type)
    # If we are doing a non-static Qt build, we only want to propagate public dependencies.
    # If we are doing a static Qt build, we need to propagate all dependencies.
    set(depends_var "public_depends")
    if(_target_is_static)
        set(depends_var "depends")
    endif()
    unset(_target_is_static)

    foreach(dep ${${depends_var}} ${optional_public_depends} ${extra_third_party_deps})
        # Gather third party packages that should be found when using the Qt module.
        # Also handle nolink target dependencies.
        string(REGEX REPLACE "_nolink$" "" base_dep "${dep}")
        if(NOT base_dep STREQUAL dep)
            # Resets target name like Vulkan_nolink to Vulkan, because we need to call
            # find_package(Vulkan).
            set(dep ${base_dep})
        endif()

        # Strip any directory scope tokens.
        __qt_internal_strip_target_directory_scope_token("${dep}" dep)
        if(TARGET ${dep})
            list(FIND third_party_deps_seen ${dep} dep_seen)

            get_target_property(package_name ${dep} INTERFACE_QT_PACKAGE_NAME)
            if(dep_seen EQUAL -1 AND package_name)
                list(APPEND third_party_deps_seen ${dep})
                get_target_property(package_is_optional ${dep} INTERFACE_QT_PACKAGE_IS_OPTIONAL)
                if(NOT package_is_optional AND dep IN_LIST optional_public_depends)
                    set(package_is_optional TRUE)
                endif()
                get_target_property(package_version ${dep} INTERFACE_QT_PACKAGE_VERSION)
                if(NOT package_version)
                    set(package_version "")
                endif()

                get_target_property(package_components ${dep} INTERFACE_QT_PACKAGE_COMPONENTS)
                if(NOT package_components)
                    set(package_components "")
                endif()

                get_target_property(package_optional_components ${dep}
                    INTERFACE_QT_PACKAGE_OPTIONAL_COMPONENTS)
                if(NOT package_optional_components)
                    set(package_optional_components "")
                endif()

                get_target_property(package_components_id ${dep} _qt_package_components_id)
                if(package_components_id)
                    list(APPEND third_party_deps_package_components_ids ${package_components_id})
                endif()

                list(APPEND third_party_deps
                    "${package_name}\;${package_is_optional}\;${package_version}\;${package_components}\;${package_optional_components}")
            endif()
        endif()
    endforeach()
endmacro()

# Collect provided targets for the given list of package component ids.
#
# ${target} is merely used as a key infix to avoid name clashes in the Dependencies.cmake files.
# package_component_ids is a list of '${package_name}-${components}-${optional_components}' keys
# that are sanitized not to contain spaces or semicolons.
#
# The output is a list of variable assignments to add to the dependencies file.
# Each variable assignment is the list of provided targets for a given package component id.
#
# We use these extra assignments instead of adding the info to the existing 'third_party_deps' list
# to make the information more readable. That list already has 5 items per package, making it
# quite hard to read.
function(qt_internal_collect_third_party_dep_packages_info
        target
        package_components_ids
        out_packages_info)

    # There might be multiple calls to find the same package, so remove the duplicates.
    list(REMOVE_DUPLICATES package_components_ids)

    set(packages_info "")

    foreach(package_key IN LISTS package_components_ids)
        get_cmake_property(provided_targets _qt_find_package_${package_key}_provided_targets)
        if(provided_targets)
            set(key "__qt_${target}_third_party_package_${package_key}_provided_targets")
            string(APPEND packages_info "set(${key} \"${provided_targets}\")\n")
        endif()
    endforeach()

    set(${out_packages_info} "${packages_info}" PARENT_SCOPE)
endfunction()

# Filter the dependency targets to collect unique set of the dependencies.
# non-Private and Private targets are treated as the single object in this context
# since they are defined by the same CMake package. For internal modules
# the CMake package will be always Private.
function(qt_internal_remove_qt_dependency_duplicates out_deps deps)
    set(${out_deps} "")
    foreach(dep ${deps})
        if(dep)
            list(FIND ${out_deps} "${dep}" dep_seen)

            if(dep_seen EQUAL -1)
                list(LENGTH dep len)
                if(NOT (len EQUAL 2))
                    message(FATAL_ERROR "List '${dep}' should look like QtFoo;version")
                endif()
                list(GET dep 0 dep_name)
                list(GET dep 1 dep_ver)

                # Skip over Qt6 dependency, because we will manually handle it in the Dependencies
                # file before everything else, to ensure that find_package(Qt6Core)-style works.
                if(dep_name STREQUAL "${INSTALL_CMAKE_NAMESPACE}")
                    continue()
                endif()
                list(APPEND ${out_deps} "${dep_name}\;${dep_ver}")
            endif()
        endif()
    endforeach()
    set(${out_deps} "${${out_deps}}" PARENT_SCOPE)
endfunction()

function(qt_internal_create_module_depends_file target)
    get_target_property(target_type "${target}" TYPE)
    set(is_interface_lib FALSE)
    if(target_type STREQUAL "INTERFACE_LIBRARY")
        set(is_interface_lib TRUE)
    endif()

    set(depends "")
    if(target_type STREQUAL "STATIC_LIBRARY")
        get_target_property(depends "${target}" LINK_LIBRARIES)
    endif()

    get_target_property(public_depends "${target}" INTERFACE_LINK_LIBRARIES)

    unset(optional_public_depends)
    if(TARGET "${target}Private")
        get_target_property(optional_public_depends "${target}Private" INTERFACE_LINK_LIBRARIES)
    endif()

    # Used for collecting Qt module dependencies that should be find_package()'d in
    # ModuleDependencies.cmake.
    get_target_property(target_deps "${target}" _qt_target_deps)
    set(target_deps_seen "")
    set(qt_module_dependencies "")

    if(NOT is_interface_lib)
        get_target_property(extra_depends "${target}" QT_EXTRA_PACKAGE_DEPENDENCIES)
    endif()
    if(NOT extra_depends MATCHES "-NOTFOUND$")
        list(APPEND target_deps "${extra_depends}")
    endif()

    # Extra 3rd party targets who's packages should be considered dependencies.
    get_target_property(extra_third_party_deps "${target}" _qt_extra_third_party_dep_targets)
    if(NOT extra_third_party_deps)
        set(extra_third_party_deps "")
    endif()

    # Used for assembling the content of an include/Module/ModuleDepends.h header.
    set(qtdeps "")

    # Used for collecting third party dependencies that should be find_package()'d in
    # ModuleDependencies.cmake.
    set(third_party_deps "")
    set(third_party_deps_seen "")
    set(third_party_deps_package_components_ids "")

    # Used for collecting Qt tool dependencies that should be find_package()'d in
    # ModuleToolsDependencies.cmake.
    set(tool_deps "")
    set(tool_deps_seen "")

    # Used for collecting Qt tool dependencies that should be find_package()'d in
    # ModuleDependencies.cmake.
    set(main_module_tool_deps "")

    # Extra QtFooModuleTools packages to be added as dependencies to
    # QtModuleDependencies.cmake. Needed for QtWaylandCompositor / QtWaylandClient.
    if(NOT is_interface_lib)
        get_target_property(extra_tools_package_dependencies "${target}"
                            QT_EXTRA_TOOLS_PACKAGE_DEPENDENCIES)
        if(extra_tools_package_dependencies)
            list(APPEND main_module_tool_deps "${extra_tools_package_dependencies}")
        endif()
    endif()

    qt_internal_get_qt_all_known_modules(known_modules)

    set(all_depends ${depends} ${public_depends})
    foreach (dep ${all_depends})
        # Normalize module by stripping leading "Qt::" and trailing "Private"
        if (dep MATCHES "(Qt|${QT_CMAKE_EXPORT_NAMESPACE})::([-_A-Za-z0-9]+)")
            set(dep "${CMAKE_MATCH_2}")
            set(real_dep_target "Qt::${dep}")

            if(TARGET "${real_dep_target}")
                get_target_property(is_versionless_target "${real_dep_target}"
                                    _qt_is_versionless_target)
                if(is_versionless_target)
                    set(real_dep_target "${QT_CMAKE_EXPORT_NAMESPACE}::${dep}")
                endif()

                get_target_property(skip_module_depends_include "${real_dep_target}"
                                    _qt_module_skip_depends_include)
                if(skip_module_depends_include)
                    continue()
                endif()

                get_target_property(module_has_headers "${real_dep_target}"
                                    _qt_module_has_headers)
                if(NOT module_has_headers)
                    continue()
                endif()
            endif()
        endif()

        list(FIND known_modules "${dep}" _pos)
        if (_pos GREATER -1)
            qt_internal_module_info(module ${QT_CMAKE_EXPORT_NAMESPACE}::${dep})
            list(APPEND qtdeps ${module})

            # Make the ModuleTool package depend on dep's ModuleTool package.
            list(FIND tool_deps_seen ${dep} dep_seen)
            if(dep_seen EQUAL -1 AND ${dep} IN_LIST QT_KNOWN_MODULES_WITH_TOOLS)
                qt_internal_get_package_version_of_target("${dep}" dep_package_version)
                list(APPEND tool_deps_seen ${dep})
                list(APPEND tool_deps
                            "${INSTALL_CMAKE_NAMESPACE}${dep}Tools\;${dep_package_version}")
            endif()
        endif()
    endforeach()

    qt_collect_third_party_deps(${target})
    qt_internal_collect_third_party_dep_packages_info(${target}
        "${third_party_deps_package_components_ids}"
        packages_info)

    set(third_party_deps_extra_info "")
    if(packages_info)
        string(APPEND third_party_deps_extra_info "${packages_info}")
    endif()

    # Add dependency to the main ModuleTool package to ModuleDependencies file.
    if(${target} IN_LIST QT_KNOWN_MODULES_WITH_TOOLS)
        qt_internal_get_package_version_of_target("${target}" main_module_tool_package_version)
        list(APPEND main_module_tool_deps
            "${INSTALL_CMAKE_NAMESPACE}${target}Tools\;${main_module_tool_package_version}")
    endif()

    foreach(dep ${target_deps})
        if(NOT dep MATCHES ".+Private$" AND
           dep MATCHES "${INSTALL_CMAKE_NAMESPACE}(.+)")
            # target_deps contains elements that are a pair of target name and version,
            # e.g. 'Core\;6.2'
            # After the extracting from the target_deps list, the element becomes a list itself,
            # because it loses escape symbol before the semicolon, so ${CMAKE_MATCH_1} is the list:
            # Core;6.2.
            # We need to store only the target name in the qt_module_dependencies variable.
            list(GET CMAKE_MATCH_1 0 dep_name)
            if(dep_name)
                list(APPEND qt_module_dependencies "${dep_name}")
            endif()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES qt_module_dependencies)

    qt_internal_remove_qt_dependency_duplicates(target_deps "${target_deps}")


    if (DEFINED qtdeps)
        list(REMOVE_DUPLICATES qtdeps)
    endif()

    get_target_property(hasModuleHeaders "${target}" _qt_module_has_headers)
    if (${hasModuleHeaders})
        qt_internal_write_depends_file(${target} ${qtdeps})
    endif()

    if(third_party_deps OR main_module_tool_deps OR target_deps)
        set(path_suffix "${INSTALL_CMAKE_NAMESPACE}${target}")
        qt_path_join(config_build_dir ${QT_CONFIG_BUILD_DIR} ${path_suffix})
        qt_path_join(config_install_dir ${QT_CONFIG_INSTALL_DIR} ${path_suffix})

        # All module packages should look for the Qt6 package version that qtbase was originally
        # built as.
        qt_internal_get_package_version_of_target(Platform main_qt_package_version)

        # Configure and install ModuleDependencies file.
        configure_file(
            "${QT_CMAKE_DIR}/QtModuleDependencies.cmake.in"
            "${config_build_dir}/${INSTALL_CMAKE_NAMESPACE}${target}Dependencies.cmake"
            @ONLY
        )

        qt_install(FILES
            "${config_build_dir}/${INSTALL_CMAKE_NAMESPACE}${target}Dependencies.cmake"
            DESTINATION "${config_install_dir}"
            COMPONENT Devel
        )

        message(TRACE "Recorded dependencies for module: ${target}\n"
            "    Qt dependencies: ${target_deps}\n"
            "    3rd-party dependencies: ${third_party_deps}")
    endif()
    if(tool_deps)
        # The value of the property will be used by qt_export_tools.
        set_property(TARGET "${target}" PROPERTY _qt_tools_package_deps "${tool_deps}")
    endif()
endfunction()

function(qt_internal_create_plugin_depends_file target)
    get_target_property(plugin_install_package_suffix "${target}" _qt_plugin_install_package_suffix)
    get_target_property(depends "${target}" LINK_LIBRARIES)
    get_target_property(public_depends "${target}" INTERFACE_LINK_LIBRARIES)
    get_target_property(target_deps "${target}" _qt_target_deps)
    unset(optional_public_depends)
    set(target_deps_seen "")


    # Extra 3rd party targets who's packages should be considered dependencies.
    get_target_property(extra_third_party_deps "${target}" _qt_extra_third_party_dep_targets)
    if(NOT extra_third_party_deps)
        set(extra_third_party_deps "")
    endif()

    qt_collect_third_party_deps(${target})

    qt_internal_remove_qt_dependency_duplicates(target_deps "${target_deps}")

    if(third_party_deps OR target_deps)
        # Setup build and install paths

        # Plugins should look for their dependencies in their associated module package folder as
        # well as the Qt6 package folder which is stored by the Qt6 package in _qt_cmake_dir.
        set(find_dependency_paths "\${CMAKE_CURRENT_LIST_DIR}/..;\${_qt_cmake_dir}")
        if(plugin_install_package_suffix)
            set(path_suffix "${INSTALL_CMAKE_NAMESPACE}${plugin_install_package_suffix}")
            if(plugin_install_package_suffix MATCHES "/QmlPlugins")
                # Qml plugins are one folder deeper.
                set(find_dependency_paths "\${CMAKE_CURRENT_LIST_DIR}/../..;\${_qt_cmake_dir}")
            endif()

        else()
            set(path_suffix "${INSTALL_CMAKE_NAMESPACE}${target}")
        endif()

        qt_path_join(config_build_dir ${QT_CONFIG_BUILD_DIR} ${path_suffix})
        qt_path_join(config_install_dir ${QT_CONFIG_INSTALL_DIR} ${path_suffix})

        # Configure and install ModuleDependencies file.
        configure_file(
            "${QT_CMAKE_DIR}/QtPluginDependencies.cmake.in"
            "${config_build_dir}/${INSTALL_CMAKE_NAMESPACE}${target}Dependencies.cmake"
            @ONLY
        )

        qt_install(FILES
            "${config_build_dir}/${INSTALL_CMAKE_NAMESPACE}${target}Dependencies.cmake"
            DESTINATION "${config_install_dir}"
            COMPONENT Devel
        )

        message(TRACE "Recorded dependencies for plugin: ${target}\n"
            "    Qt dependencies: ${target_deps}\n"
            "    3rd-party dependencies: ${third_party_deps}")
    endif()
endfunction()

function(qt_internal_create_qt6_dependencies_file)
    # This is used for substitution in the configured file.
    set(target "${INSTALL_CMAKE_NAMESPACE}")

    # This is the actual target we're querying.
    set(actual_target Platform)
    get_target_property(public_depends "${actual_target}" INTERFACE_LINK_LIBRARIES)
    unset(depends)
    unset(optional_public_depends)

    set(third_party_deps "")
    set(third_party_deps_seen "")
    set(third_party_deps_package_components_ids "")

    # We need to collect third party deps that are set on the public Platform target,
    # like Threads::Threads.
    # This mimics find_package part of the CONFIG += thread assignment in mkspecs/features/qt.prf.
    qt_collect_third_party_deps(${actual_target})
    qt_internal_collect_third_party_dep_packages_info("${INSTALL_CMAKE_NAMESPACE}"
        "${third_party_deps_package_components_ids}"
        packages_info)

    set(third_party_deps_extra_info "")
    if(packages_info)
        string(APPEND third_party_deps_extra_info "${packages_info}")
    endif()

    # For Threads we also need to write an extra variable assignment.
    set(third_party_extra "")
    if(third_party_deps MATCHES "Threads")
        string(APPEND third_party_extra "if(NOT QT_NO_THREADS_PREFER_PTHREAD_FLAG)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
endif()")
    endif()

    _qt_internal_determine_if_host_info_package_needed(platform_requires_host_info_package)

    if(platform_requires_host_info_package)
        # TODO: Figure out how to make the initial QT_HOST_PATH var relocatable in relation
        # to the target CMAKE_INSTALL_DIR, if at all possible to do so in a reliable way.
        get_filename_component(qt_host_path_absolute "${QT_HOST_PATH}" ABSOLUTE)
        get_filename_component(qt_host_path_cmake_dir_absolute
            "${Qt${PROJECT_VERSION_MAJOR}HostInfo_DIR}/.." ABSOLUTE)
    endif()

    if(third_party_deps OR platform_requires_host_info_package)
        # Setup build and install paths.
        set(path_suffix "${INSTALL_CMAKE_NAMESPACE}")

        qt_path_join(config_build_dir ${QT_CONFIG_BUILD_DIR} ${path_suffix})
        qt_path_join(config_install_dir ${QT_CONFIG_INSTALL_DIR} ${path_suffix})

        # Configure and install QtDependencies file.
        configure_file(
            "${QT_CMAKE_DIR}/QtConfigDependencies.cmake.in"
            "${config_build_dir}/${target}Dependencies.cmake"
            @ONLY
        )

        qt_install(FILES
            "${config_build_dir}/${target}Dependencies.cmake"
            DESTINATION "${config_install_dir}"
            COMPONENT Devel
        )
    endif()
endfunction()

# Create Depends.cmake & Depends.h files for all modules and plug-ins.
function(qt_internal_create_depends_files)
    qt_internal_get_qt_repo_known_modules(repo_known_modules)

    if(PROJECT_NAME STREQUAL "QtBase")
        qt_internal_create_qt6_dependencies_file()
    endif()

    foreach (target ${repo_known_modules})
        qt_internal_create_module_depends_file(${target})
    endforeach()

    foreach (target ${QT_KNOWN_PLUGINS})
        qt_internal_create_plugin_depends_file(${target})
    endforeach()
endfunction()

# This function creates Qt<Module>Plugins.cmake files used to include all
# the plugin Config files that belong to that module.
function(qt_internal_create_plugins_auto_inclusion_files)
    # For static library builds, the plugin targets need to be available for linking.
    # For shared library builds, the plugin targets are useful for deployment purposes.
    qt_internal_get_qt_repo_known_modules(repo_known_modules)

    set(modules_with_plugins "")
    foreach (QT_MODULE ${repo_known_modules})
        get_target_property(target_type "${QT_MODULE}" TYPE)
        if(target_type STREQUAL "INTERFACE_LIBRARY")
            # No plugins are provided by a header only module.
            continue()
        endif()
        qt_path_join(config_build_dir ${QT_CONFIG_BUILD_DIR} ${INSTALL_CMAKE_NAMESPACE}${QT_MODULE})
        qt_path_join(config_install_dir ${QT_CONFIG_INSTALL_DIR} ${INSTALL_CMAKE_NAMESPACE}${QT_MODULE})
        set(QT_MODULE_PLUGIN_INCLUDES "")

        if(QT_MODULE STREQUAL "Qml")
            set(QT_MODULE_PLUGIN_INCLUDES "${QT_MODULE_PLUGIN_INCLUDES}
__qt_internal_include_qml_plugin_packages()
")
        endif()

        get_target_property(module_plugin_types "${QT_MODULE}" MODULE_PLUGIN_TYPES)
        if(module_plugin_types OR QT_MODULE_PLUGIN_INCLUDES)
            list(APPEND modules_with_plugins "${QT_MODULE}")
            configure_file(
                "${QT_CMAKE_DIR}/QtPlugins.cmake.in"
                "${config_build_dir}/${INSTALL_CMAKE_NAMESPACE}${QT_MODULE}Plugins.cmake"
                @ONLY
            )
            qt_install(FILES
                "${config_build_dir}/${INSTALL_CMAKE_NAMESPACE}${QT_MODULE}Plugins.cmake"
                DESTINATION "${config_install_dir}"
                COMPONENT Devel
            )
        endif()
    endforeach()
    if(modules_with_plugins)
        message(STATUS "Generated QtModulePlugins.cmake files for the following modules:"
            " ${modules_with_plugins}")
    endif()
endfunction()

function(qt_generate_install_prefixes out_var)
    set(content "\n")
    set(vars INSTALL_BINDIR INSTALL_INCLUDEDIR INSTALL_LIBDIR INSTALL_MKSPECSDIR INSTALL_ARCHDATADIR
        INSTALL_PLUGINSDIR INSTALL_LIBEXECDIR INSTALL_QMLDIR INSTALL_DATADIR INSTALL_DOCDIR
        INSTALL_TRANSLATIONSDIR INSTALL_SYSCONFDIR INSTALL_EXAMPLESDIR INSTALL_TESTSDIR
        INSTALL_DESCRIPTIONSDIR INSTALL_SBOMDIR)
    # INSTALL_PUBLICBINDIR is processed only if it is not empty
    # See usage in qt_internal_generate_user_facing_tools_info
    if(NOT "${INSTALL_PUBLICBINDIR}" STREQUAL "")
        list(APPEND vars INSTALL_PUBLICBINDIR)
    endif()

    foreach(var ${vars})
        get_property(docstring CACHE "${var}" PROPERTY HELPSTRING)
        string(APPEND content "set(${var} \"${${var}}\" CACHE STRING \"${docstring}\")\n")
    endforeach()

    set(${out_var} "${content}" PARENT_SCOPE)
endfunction()

function(qt_wrap_string_in_if_multi_config content out_var)
    set(${out_var} "
get_property(__qt_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(__qt_is_multi_config)
${content}endif()
unset(__qt_is_multi_config)\n" PARENT_SCOPE)
endfunction()

function(qt_wrap_string_in_if_ninja_multi_config content out_var)
    set(${out_var} "if(CMAKE_GENERATOR STREQUAL \"Ninja Multi-Config\")
${content}endif()\n" PARENT_SCOPE)
endfunction()

function(qt_create_hostinfo_package)
    set(package "${INSTALL_CMAKE_NAMESPACE}HostInfo")
    qt_path_join(config_file_path "${QT_CONFIG_BUILD_DIR}/${package}/${package}Config.cmake")
    qt_path_join(install_destination ${QT_CONFIG_INSTALL_DIR} ${package})
    set(var_prefix "QT${PROJECT_VERSION_MAJOR}_HOST_INFO_")
    configure_package_config_file(
        "${CMAKE_CURRENT_LIST_DIR}/QtHostInfoConfig.cmake.in"
        "${config_file_path}"
        INSTALL_DESTINATION "${install_destination}"
        NO_SET_AND_CHECK_MACRO
        NO_CHECK_REQUIRED_COMPONENTS_MACRO)
    qt_install(FILES "${config_file_path}" DESTINATION "${install_destination}")
endfunction()

function(qt_generate_build_internals_extra_cmake_code)
    if(PROJECT_NAME STREQUAL "QtBase")
        qt_create_hostinfo_package()

        foreach(var IN LISTS QT_BASE_CONFIGURE_TESTS_VARS_TO_EXPORT)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS "set(${var} \"${${var}}\" CACHE INTERNAL \"\")\n")
        endforeach()

        set(QT_SOURCE_TREE "${QtBase_SOURCE_DIR}")
        qt_path_join(extra_file_path
                     ${QT_CONFIG_BUILD_DIR}
                     ${INSTALL_CMAKE_NAMESPACE}BuildInternals/QtBuildInternalsExtra.cmake)

        if(CMAKE_BUILD_TYPE)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "
# Used by qt_internal_set_cmake_build_type.
set(__qt_internal_initial_qt_cmake_build_type \"${CMAKE_BUILD_TYPE}\")
")
        endif()
        if(CMAKE_CONFIGURATION_TYPES)
            string(APPEND multi_config_specific
                "    set(CMAKE_CONFIGURATION_TYPES \"${CMAKE_CONFIGURATION_TYPES}\" CACHE STRING \"\" FORCE)\n")
        endif()
        if(CMAKE_TRY_COMPILE_CONFIGURATION)
            string(APPEND multi_config_specific
                "    set(CMAKE_TRY_COMPILE_CONFIGURATION \"${CMAKE_TRY_COMPILE_CONFIGURATION}\")\n")
        endif()
        if(multi_config_specific)
            qt_wrap_string_in_if_multi_config(
                "${multi_config_specific}"
                multi_config_specific)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS "${multi_config_specific}")
        endif()

        if(QT_MULTI_CONFIG_FIRST_CONFIG)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "\nset(QT_MULTI_CONFIG_FIRST_CONFIG \"${QT_MULTI_CONFIG_FIRST_CONFIG}\")\n")
        endif()

        if(CMAKE_CROSS_CONFIGS)
            string(APPEND ninja_multi_config_specific
                "    set(CMAKE_CROSS_CONFIGS \"${CMAKE_CROSS_CONFIGS}\" CACHE STRING \"\")\n")
        endif()
        if(CMAKE_DEFAULT_BUILD_TYPE)
            string(APPEND ninja_multi_config_specific
                "    set(CMAKE_DEFAULT_BUILD_TYPE \"${CMAKE_DEFAULT_BUILD_TYPE}\" CACHE STRING \"\")\n")
        endif()
        if(CMAKE_DEFAULT_CONFIGS)
            string(APPEND ninja_multi_config_specific
                "    set(CMAKE_DEFAULT_CONFIGS \"${CMAKE_DEFAULT_CONFIGS}\" CACHE STRING \"\")\n")
        endif()
        if(ninja_multi_config_specific)
            qt_wrap_string_in_if_ninja_multi_config(
                "${ninja_multi_config_specific}"
                ninja_multi_config_specific)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS "${ninja_multi_config_specific}")
        endif()

        if(DEFINED BUILD_WITH_PCH)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(BUILD_WITH_PCH \"${BUILD_WITH_PCH}\" CACHE STRING \"\")\n")
        endif()

        if(DEFINED QT_IS_MACOS_UNIVERSAL)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_IS_MACOS_UNIVERSAL \"${QT_IS_MACOS_UNIVERSAL}\" CACHE BOOL \"\")\n")
        endif()

        if(DEFINED QT_APPLE_SDK)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_APPLE_SDK \"${QT_APPLE_SDK}\" CACHE BOOL \"\")\n")
        endif()

        if(QT_FORCE_FIND_TOOLS)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_FORCE_FIND_TOOLS \"TRUE\" CACHE BOOL \"\" FORCE)\n")
        endif()

        if(QT_FORCE_BUILD_TOOLS)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_FORCE_BUILD_TOOLS \"TRUE\" CACHE BOOL \"\" FORCE)\n")
        endif()

        if(QT_INTERNAL_EXAMPLES_INSTALL_PREFIX)
            file(TO_CMAKE_PATH
                "${QT_INTERNAL_EXAMPLES_INSTALL_PREFIX}" examples_install_prefix)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_INTERNAL_EXAMPLES_INSTALL_PREFIX \"${examples_install_prefix}\" CACHE STRING \"\")\n")
        endif()

        # Save the default qpa platform.
        if(DEFINED QT_QPA_DEFAULT_PLATFORM)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_QPA_DEFAULT_PLATFORM \"${QT_QPA_DEFAULT_PLATFORM}\" CACHE STRING \"\")\n")
        endif()

        # Save the list of default qpa platforms.
        # Used by qtwayland/src/plugins/platforms/qwayland-generic/CMakeLists.txt. Otherwise
        # the DEFAULT_IF condition is evaluated incorrectly.
        if(DEFINED QT_QPA_PLATFORMS)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_QPA_PLATFORMS \"${QT_QPA_PLATFORMS}\" CACHE STRING \"\")\n")
        endif()

        # Save minimum and policy-related CMake versions to ensure the same minimum is
        # checked for when building other downstream repos (qtsvg, etc) and the policy settings
        # will be consistent unless the downstream repos explicitly override them.
        # Policy settings can be overridden per-repo, but the minimum CMake version is global for all of
        # Qt.
        qt_internal_get_supported_min_cmake_version_for_building_qt(
            supported_min_version_for_building_qt)
        qt_internal_get_computed_min_cmake_version_for_building_qt(
            computed_min_version_for_building_qt)
        qt_internal_get_min_new_policy_cmake_version(min_new_policy_version)
        qt_internal_get_max_new_policy_cmake_version(max_new_policy_version)

        # Rpath related things that need to be re-used when building other repos.
        string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
            "set(CMAKE_INSTALL_RPATH \"${CMAKE_INSTALL_RPATH}\" CACHE STRING \"\")\n")
        if(DEFINED QT_DISABLE_RPATH)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_DISABLE_RPATH \"${QT_DISABLE_RPATH}\" CACHE STRING \"\")\n")
        endif()
        if(DEFINED QT_EXTRA_DEFINES)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_EXTRA_DEFINES \"${QT_EXTRA_DEFINES}\" CACHE STRING \"\")\n")
        endif()
        if(DEFINED QT_EXTRA_INCLUDEPATHS)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_EXTRA_INCLUDEPATHS \"${QT_EXTRA_INCLUDEPATHS}\" CACHE STRING \"\")\n")
        endif()
        if(DEFINED QT_EXTRA_FRAMEWORKPATHS)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_EXTRA_FRAMEWORKPATHS \"${QT_EXTRA_FRAMEWORKPATHS}\" CACHE STRING \"\")\n")
        endif()
        if(DEFINED QT_EXTRA_LIBDIRS)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_EXTRA_LIBDIRS \"${QT_EXTRA_LIBDIRS}\" CACHE STRING \"\")\n")
        endif()
        if(DEFINED QT_EXTRA_RPATHS)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_EXTRA_RPATHS \"${QT_EXTRA_RPATHS}\" CACHE STRING \"\")\n")
        endif()
        if(DEFINED QT_DISABLE_DEPRECATED_UP_TO)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "set(QT_DISABLE_DEPRECATED_UP_TO \"${QT_DISABLE_DEPRECATED_UP_TO}\""
                " CACHE STRING \"\")\n")
        endif()

        # Save pkg-config feature value to be able to query it internally as soon as BuildInternals
        # package is loaded. This is to avoid any pkg-config package from being found when
        # find_package(Qt6Core) is called in case if the feature was disabled.
        string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS "
if(NOT QT_SKIP_BUILD_INTERNALS_PKG_CONFIG_FEATURE)
    set(FEATURE_pkg_config \"${FEATURE_pkg_config}\" CACHE BOOL \"Using pkg-config\" FORCE)
endif()\n")

        # The OpenSSL root dir needs to be saved so that repos other than qtbase (like qtopcua) can
        # still successfully find_package(WrapOpenSSL) in the CI.
        # qmake saves any additional include paths passed via the configure like '-I/foo'
        # in mkspecs/qmodule.pri, so this file is the closest equivalent.
        if(DEFINED OPENSSL_ROOT_DIR)
            file(TO_CMAKE_PATH "${OPENSSL_ROOT_DIR}" openssl_root_cmake_path)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                   "set(OPENSSL_ROOT_DIR \"${openssl_root_cmake_path}\" CACHE STRING \"\")\n")
        endif()

        qt_generate_install_prefixes(install_prefix_content)

        string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS "${install_prefix_content}")

        if(DEFINED OpenGL_GL_PREFERENCE)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
                "
# Use the OpenGL_GL_PREFERENCE value qtbase was built with. But do not FORCE it.
set(OpenGL_GL_PREFERENCE \"${OpenGL_GL_PREFERENCE}\" CACHE STRING \"\")
")
        endif()

        string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS
            "
set(QT_COPYRIGHT \"${QT_COPYRIGHT}\" CACHE STRING \"\")
")

        # Add the apple version requirements to the BuildInternals extra code, so the info is
        # available when configuring a standalone test.
        # Otherwise when QtSetup is included after a
        #   find_package(Qt6BuildInternals REQUIRED COMPONENTS STANDALONE_TEST)
        # call, Qt6ConfigExtras.cmake is not included yet, the requirements are not available and
        # _qt_internal_check_apple_sdk_and_xcode_versions() would fail.
        _qt_internal_export_apple_sdk_and_xcode_version_requirements(apple_requirements)
        if(apple_requirements)
            string(APPEND QT_EXTRA_BUILD_INTERNALS_VARS "
${apple_requirements}
")
        endif()

        qt_compute_relative_path_from_cmake_config_dir_to_prefix()
        configure_file(
            "${CMAKE_CURRENT_LIST_DIR}/QtBuildInternalsExtra.cmake.in"
            "${extra_file_path}"
            @ONLY
        )
    endif()
endfunction()

# For every Qt module check if there any android dependencies that require
# processing.
function(qt_modules_process_android_dependencies)
    qt_internal_get_qt_repo_known_modules(repo_known_modules)
    foreach (target ${repo_known_modules})
        qt_internal_android_dependencies(${target})
    endforeach()
endfunction()

function(qt_create_tools_config_files)
    # Create packages like Qt6CoreTools/Qt6CoreToolsConfig.cmake.
    foreach(module_name ${QT_KNOWN_MODULES_WITH_TOOLS})
        qt_export_tools("${module_name}")
    endforeach()
endfunction()

function(qt_internal_create_config_file_for_standalone_tests)
    set(standalone_tests_config_dir "StandaloneTests")
    qt_path_join(config_build_dir
                 ${QT_CONFIG_BUILD_DIR}
                 "${INSTALL_CMAKE_NAMESPACE}BuildInternals" "${standalone_tests_config_dir}")
    qt_path_join(config_install_dir
                 ${QT_CONFIG_INSTALL_DIR}
                 "${INSTALL_CMAKE_NAMESPACE}BuildInternals" "${standalone_tests_config_dir}")

    # Filter out bundled system libraries. Otherwise when looking for their dependencies
    # (like PNG for Freetype) FindWrapPNG is searched for during configuration of
    # standalone tests, and it can happen that Core or Gui features are not
    # imported early enough, which means FindWrapPNG will try to find a system PNG library instead
    # of the bundled one.
    set(modules)
    foreach(m ${QT_REPO_KNOWN_MODULES})
        get_target_property(target_type "${m}" TYPE)

        # Interface libraries are never bundled system libraries (hopefully).
        if(target_type STREQUAL "INTERFACE_LIBRARY")
            list(APPEND modules "${m}")
            continue()
        endif()

        get_target_property(is_3rd_party "${m}" _qt_module_is_3rdparty_library)
        if(NOT is_3rd_party)
            list(APPEND modules "${m}")
        endif()
    endforeach()

    list(JOIN modules " " QT_REPO_KNOWN_MODULES_STRING)
    string(STRIP "${QT_REPO_KNOWN_MODULES_STRING}" QT_REPO_KNOWN_MODULES_STRING)

    # Skip generating and installing file if no modules were built. This make sure not to install
    # anything when build qtx11extras on macOS for example.
    if(NOT QT_REPO_KNOWN_MODULES_STRING)
        return()
    endif()

    # Create a Config file that calls find_package on the modules that were built as part
    # of the current repo. This is used for standalone tests.
    qt_internal_get_standalone_parts_config_file_name(tests_config_file_name)

    # Standalone tests Config files should follow the main versioning scheme.
    qt_internal_get_package_version_of_target(Platform main_qt_package_version)

    configure_file(
        "${QT_CMAKE_DIR}/QtStandaloneTestsConfig.cmake.in"
        "${config_build_dir}/${tests_config_file_name}"
        @ONLY
    )
    qt_install(FILES
        "${config_build_dir}/${tests_config_file_name}"
        DESTINATION "${config_install_dir}"
        COMPONENT Devel
    )
endfunction()

function(qt_internal_install_prl_files)
    # Get locations relative to QT_BUILD_DIR from which prl files should be installed.
    get_property(prl_install_dirs GLOBAL PROPERTY QT_PRL_INSTALL_DIRS)

    # Clear the list of install dirs so the previous values don't pollute the list of install dirs
    # for the next repository in a top-level build.
    set_property(GLOBAL PROPERTY QT_PRL_INSTALL_DIRS "")

    foreach(prl_install_dir ${prl_install_dirs})
        qt_install(DIRECTORY "${QT_BUILD_DIR}/${prl_install_dir}/"
            DESTINATION ${prl_install_dir}
            FILES_MATCHING PATTERN "*.prl"
        )
    endforeach()
endfunction()

function(qt_internal_generate_user_facing_tools_info)
    if("${INSTALL_PUBLICBINDIR}" STREQUAL "")
        return()
    endif()
    qt_path_join(tool_link_base_dir "${CMAKE_INSTALL_PREFIX}" "${INSTALL_PUBLICBINDIR}")
    get_property(user_facing_tool_targets GLOBAL PROPERTY QT_USER_FACING_TOOL_TARGETS)
    set(lines "")
    foreach(target ${user_facing_tool_targets})
        get_target_property(filename ${target} OUTPUT_NAME)
        if(NOT filename)
            set(filename ${target})
        endif()
        set(linkname ${filename})
        if(APPLE)
            get_target_property(is_macos_bundle ${target} MACOSX_BUNDLE )
            if(is_macos_bundle)
                set(filename "${filename}.app/Contents/MacOS/${filename}")
            endif()
        endif()
        qt_path_join(tool_target_path "${CMAKE_INSTALL_PREFIX}" "${INSTALL_BINDIR}" "${filename}")
        qt_path_join(tool_link_path "${INSTALL_PUBLICBINDIR}" "${linkname}${PROJECT_VERSION_MAJOR}")
        _qt_internal_relative_path(tool_target_path BASE_DIRECTORY ${tool_link_base_dir})
        list(APPEND lines "${tool_target_path} ${tool_link_path}")
    endforeach()
    string(REPLACE ";" "\n" content "${lines}")
    string(APPEND content "\n")
    set(out_file "${PROJECT_BINARY_DIR}/user_facing_tool_links.txt")
    file(WRITE "${out_file}" "${content}")
endfunction()
