function(json_append_string VAR VALUE)
    set(initial ${${VAR}})
    string(REGEX REPLACE "([\\\"])" "\\\\\\1" result ${VALUE})
    string(REGEX REPLACE "\n" "\\\\n" result ${result})
    set(${VAR} "${initial}\"${result}\"" PARENT_SCOPE)
endfunction()

function(json_append_comma VAR)
    if(NOT (${${VAR}} MATCHES "[[{]$"))
        set(${VAR} "${${VAR}}," PARENT_SCOPE)
    endif()
endfunction()

macro(json_append_key VAR KEY VALUE)
    json_append_comma(${VAR})
    json_append_string(${VAR} ${KEY})
    string(APPEND ${VAR} ":")
    json_append_string(${VAR} ${VALUE})
endmacro()

function(json_append_list VAR KEY)
    set(result ${${VAR}})
    json_append_comma(result)
    json_append_string(result ${KEY})
    string(APPEND result ":")
    string(APPEND result "[")
    foreach(item IN LISTS ARGN)
        json_append_comma(result)
        json_append_string(result ${item})
    endforeach()
    string(APPEND result "]")
    set(${VAR} ${result} PARENT_SCOPE)
endfunction()

function(write_meta)
    cmake_parse_arguments(PARSE_ARGV 0 "" "" "NAME;DESCRIPTION;OUTPUT" "DEPENDS;ACCOUNTS")
    set(result)
    string(APPEND result "{")
    json_append_key(result "name" ${_NAME})
    json_append_key(result "description" ${_DESCRIPTION})
    json_append_list(result "depends" ${_DEPENDS})
    json_append_list(result "accounts" ${_ACCOUNTS})
    string(APPEND result "}")
    file(GENERATE OUTPUT ${_OUTPUT} CONTENT ${result})
endfunction()

function(write_service_info)
    cmake_parse_arguments(PARSE_ARGV 0 "" "" "SERVER;OUTPUT" "INIT;FLAGS")
    set(result)
    string(APPEND result "{")
    json_append_list(result "flags" ${_FLAGS})
    if (_SERVER)
        json_append_key(result "server" ${_SERVER})
    endif()
    if (_INIT)
        json_append_comma(result)
        json_append_string(result "init")
        string(APPEND result ":")
        string(APPEND result true)
    endif()
    string(APPEND result "}")
    file(GENERATE OUTPUT ${_OUTPUT} CONTENT ${result})
endfunction()

# NAME <name>               - The name of the package
# DESCRIPTION <text>        - The package description
# OUTPUT <filename>         - The package file. Defaults to ${NAME}.psi
# ACCONTS <name>...         - Additional non-service accounts to create
# DEPENDS <targets>...      - Targets that this target depends on
# PACKAGE_DEPENDS <name>... - Other psibase packages that the package depends on
# SERVICE <name>            - The account name of a service
#   WASM <filename>         - The wasm file that will be deployed
#   FLAGS <flags>...        - Flags to set on the service account (requires chain admin powers to install)
#   SERVER <name>           - The service that handles HTTP requests
#   DATA <path> <dest>      - Uploads a file or directory to the target location
#   DATA GLOB <path>... <dir> - Uploads multiple files to a directory
#   INIT                    - The service has an init action that should be run with no arguments
function(psibase_package)
    set(keywords NAME DESCRIPTION OUTPUT PACKAGE_DEPENDS DEPENDS ACCOUNTS SERVICE DATA WASM FLAGS SERVER INIT POSTINSTALL)
    foreach(keyword IN LISTS keywords)
        set(_${keyword})
    endforeach()
    set(_SERVICES)
    math(EXPR end ${ARGC}-1)
    foreach(n RANGE ${end}})
        set(my_keyword)
        foreach(keyword IN LISTS keywords)
            if (ARGV${n} STREQUAL keyword)
                set(my_keyword ${keyword})
            endif()
        endforeach()
        if(my_keyword)
            if(current_keyword STREQUAL "DATA")
                list(APPEND _DATA_${_SERVICE} DATA)
            endif()
            if (my_keyword STREQUAL "INIT")
                set(_INIT_${_SERVICE} TRUE)
                set(current_keyword)
            else()
                set(current_keyword ${my_keyword})
            endif()
        else()
            if(current_keyword STREQUAL "NAME")
                set(_NAME ${ARGV${n}})
            elseif(current_keyword STREQUAL "DESCRIPTION")
                set(_DESCRIPTION ${ARGV${n}})
            elseif(current_keyword STREQUAL "OUTPUT")
                set(_OUTPUT ${ARGV${n}})
            elseif(current_keyword STREQUAL "DEPENDS")
                list(APPEND _DEPENDS ${ARGV${n}})
            elseif(current_keyword STREQUAL "PACKAGE_DEPENDS")
                list(APPEND _PACKAGE_DEPENDS ${ARGV${n}})
            elseif(current_keyword STREQUAL "ACCOUNTS")
                list(APPEND _ACCOUNTS ${ARGV${n}})
            elseif(current_keyword STREQUAL "SERVICE")
                set(_SERVICE ${ARGV${n}})
                list(APPEND _SERVICES ${_SERVICE})
                set(_DATA_${_SERVICE})
                set(_WASM_${_SERVICE})
                set(_FLAGS_${_SERVICE})
                set(_SERVER_${_SERVICE})
                set(_INIT_${_SERVICE})
            elseif(current_keyword STREQUAL "DATA")
                list(APPEND _DATA_${_SERVICE} ${ARGV${n}})
            elseif(current_keyword STREQUAL "WASM")
                set(_WASM_${_SERVICE} ${ARGV${n}})
            elseif(current_keyword STREQUAL "FLAGS")
                list(APPEND _FLAGS_${_SERVICE} ${ARGV${n}})
            elseif(current_keyword STREQUAL "SERVER")
                set(_SERVER_${_SERVICE} ${ARGV${n}})
            elseif(current_keyword STREQUAL "POSTINSTALL")
                set(_POSTINSTALL ${ARGV${n}})
            else()
                message(FATAL_ERROR "Invalid arguments to psibase_package")
            endif()
        endif()
    endforeach()
    if(NOT _NAME)
        message(FATAL_ERROR "missing NAME")
    endif()
    if(NOT _DESCRIPTION)
        set(_DESCRIPTION "${_NAME}")
    endif()
    if(NOT _OUTPUT)
        set(_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${_NAME}.psi)
    endif()
    set(outdir ${CMAKE_CURRENT_BINARY_DIR}/${_NAME})
    set(contents meta.json)
    set(zip-deps)
    foreach(service IN LISTS _SERVICES)
        if(_WASM_${service})
            write_service_info(
                OUTPUT ${outdir}/service/${service}.json
                FLAGS ${_FLAGS_${service}}
                SERVER ${_SERVER_${service}}
                INIT ${_INIT_${service}}
            )
            set(wasm ${_WASM_${service}})
            set(deps ${wasm})
            if (TARGET ${wasm})
                set(wasm $<TARGET_FILE:${_WASM}>)
            endif()
            add_custom_command(
                OUTPUT ${outdir}/service/${service}.wasm
                DEPENDS ${deps}
                COMMAND cmake -E create_symlink ${wasm} ${outdir}/service/${service}.wasm
            )
            list(APPEND contents service/${service}.wasm service/${service}.json)
        endif()
        if(_DATA_${service})
            set(commands)
            set(dirs)
            set(last)
            set(current_group)
            set(is_glob)
            foreach(item IN ITEMS ${_DATA_${service}} DATA)
                if(item STREQUAL "DATA")
                    if (last MATCHES "^/")
                        set(dest ${outdir}/data/${service}${last})
                    else()
                        set(dest ${outdir}/data/${service}/${last})
                    endif()
                    list(LENGTH last n)
                    if(n GREATER 0)
                        string(JOIN " " current_group ${current_group})
                        if(is_glob)
                            list(APPEND commands COMMAND bash -c "ln -fs ${current_group} ${dest}")
                            list(APPEND dirs ${dest})
                        else()
                            string(REGEX REPLACE "/$" "" dest ${dest})
                            list(APPEND commands COMMAND ${CMAKE_COMMAND} -E create_symlink ${current_group} ${dest})
                            list(APPEND zip-deps ${current_group})
                            string(REGEX REPLACE "/[^/]+$" "" parent ${dest})
                            list(APPEND dirs ${parent})
                        endif()
                    endif()
                    set(last)
                    set(current_group)
                    set(is_glob)
                elseif(item STREQUAL "GLOB")
                    set(is_glob 1)
                else()
                    list(APPEND current_group ${last})
                    set(last ${item})
                endif()
            endforeach()
            add_custom_command(
                OUTPUT ${outdir}/data/${service}
                DEPENDS ${_DEPENDS}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${dirs}
                ${commands}
            )
            list(APPEND contents data/${service})
        endif()
    endforeach()

    if(_POSTINSTALL)
        add_custom_command(
            OUTPUT ${outdir}/script/postinstall.json
            DEPENDS ${_POSTINSTALL}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${outdir}/script
            COMMAND ${CMAKE_COMMAND} -E create_symlink ${_POSTINSTALL} ${outdir}/script/postinstall.json
        )
        list(APPEND contents script/postinstall.json)
    endif()

    set(deps)
    foreach(file IN LISTS contents)
        list(APPEND deps ${outdir}/${file})
    endforeach()
    
    write_meta(
        OUTPUT ${outdir}/meta.json
        NAME ${_NAME}
        DESCRIPTION ${_DESCRIPTION}
        ACCOUNTS ${_ACCOUNTS} ${_SERVICES}
        DEPENDS ${_PACKAGE_DEPENDS}
    )
    string(REGEX REPLACE "/[^/]+/?$" "" output-dir ${_OUTPUT})
    set(tempdir ${outdir}.tmp)
    add_custom_command(
        OUTPUT ${_OUTPUT}
        DEPENDS ${zip-deps} ${deps} ${_DEPENDS}
        WORKING_DIRECTORY ${outdir}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${output-dir}
        COMMAND ${CMAKE_COMMAND} -E remove -f ${_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${outdir} ${tempdir}
        COMMAND cd ${tempdir} && ${CMAKE_COMMAND} -E tar cf ${_OUTPUT} --format=zip ${contents}
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${tempdir}
    )
    add_custom_target(${_NAME}.psi ALL DEPENDS ${_OUTPUT})
endfunction()
