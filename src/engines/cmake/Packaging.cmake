# CPack stuff
set(CPACK_PACKAGE_NAME "ioda_engines")
set(CPACK_PACKAGE_VENDOR "Joint Center for Satellite Data Assimilation")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ioda_engines")
set(CPACK_PACKAGE_VERSION "${IODA_ENGINES_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${IODA_ENGINES_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${IODA_ENGINES_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${IODA_ENGINES_REVISION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_RELEASE "1")

SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ryan Honeyager")
SET(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/docs/README-package.txt") #!!!
set(CPACK_PACKAGE_INSTALL_DIRECTORY "ioda_engines")
set(CPACK_PACKAGE_CONTACT "Ryan Honeyager (honeyage@ucar.edu)")
set(CPACK_WIX_UPGRADE_GUID "3EDABBE8-8315-49C8-BCDA-3B460FA2CD50")
set(CPACK_WIX_PRODUCT_GUID "A5D5C729-29FF-4CAE-B0AC-ECA70B3E0667")
set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/share/icons\\\\favicon.ico") #!!!
set(CPACK_WIX_CMAKE_PACKAGE_REGISTRY "ioda_engines")
set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "https://github.com/JCSDA/ioda-engines")
if (NOT CPACK_SYSTEM_NAME)
	set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_PROCESSOR}")
	if (CPACK_SYSTEM_NAME STREQUAL "x86_64")
		set(CPACK_SYSTEM_NAME "amd64")
	endif ()
endif ()
if (CPACK_SYSTEM_NAME STREQUAL "AMD64")
	set(CPACK_SYSTEM_NAME "amd64")
endif ()
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	set(CPACK_SYSTEM_NAME "${CPACK_SYSTEM_NAME}-${LSB_DISTRIBUTION_NAME_SHORT}-${LSB_RELEASE_ID_SHORT}")
endif()
IF(WIN32 AND NOT UNIX)
	set(CPACK_SYSTEM_NAME "${CPACK_SYSTEM_NAME}-Windows")
endif()
option(PACKAGE_CI_BUILD "Is this a testing (continuous integration) build?" OFF)
#set(PACKAGE_GIT_BRANCH "${GITBRANCH}" CACHE STRING "What is the branch of the code used to produce this build?")
string(TIMESTAMP PACKAGE_TIMESTAMP "%Y%m%d")
if(PACKAGE_CI_BUILD)
	SET(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}-${PACKAGE_GIT_BRANCH}-${CMAKE_BUILD_TYPE}-${PACKAGE_TIMESTAMP}")
endif()
#else()
#	SET(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}")
#endif()
IF(WIN32 AND NOT UNIX)
	#set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}\\\\LICENSE")
	
	# There is a bug in NSIS that does not handle full unix paths properly. Make
	# sure there is at least one set of four (4) backslashes.
	#SET(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/share/icons\\\\favicon-96x96.png")
	#SET(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/share/icons\\\\favicon.ico")
	#SET(CPACK_NSYS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/share/icons\\\\favicon.ico")

#	SET(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\MyExecutable.exe")
	SET(CPACK_NSIS_DISPLAY_NAME "ioda-engines")
	SET(CPACK_NSIS_HELP_LINK "https:\\\\\\\\github.com/JCSDA/ioda-engines")
	SET(CPACK_NSIS_URL_INFO_ABOUT "https:\\\\\\\\github.com/JCSDA/ioda-engines")
	SET(CPACK_NSIS_CONTACT "honeyage@ucar.edu")
	SET(CPACK_NSIS_MODIFY_PATH ON)
ELSE(WIN32 AND NOT UNIX)
	#set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
	SET(CPACK_STRIP_FILES FALSE)
	SET(CPACK_SOURCE_STRIP_FILES FALSE)
ENDIF(WIN32 AND NOT UNIX)
#SET(CPACK_PACKAGE_EXECUTABLES "MyExecutable" "My Executable")

set (CPACK_COMPONENTS_ALL 
	Libraries 
	Headers
)
if(BUILD_DOCUMENTATION STREQUAL "BuildAndInstall")
	list(APPEND CPACK_COMPONENTS_ALL Documentation)
endif()

set(CPACK_COMPONENT_DOCUMENTATION_DESCRIPTION 
	"Doxygen HTML docs")
set(CPACK_COMPONENT_LIBRARIES_DESCRIPTION 
	"The compiled libraries")
set(CPACK_COMPONENT_HEADERS_DESCRIPTION 
	"Headers for code development")

set(CPACK_COMPONENT_HEADERS_DEPENDS Libraries)

set(CPACK_COMPONENT_LIBRARIES_REQUIRED 1)

set(CPACK_RPM_PACKAGE_REQUIRES 
	"cmake >= 3.12, hdf5-devel, hdf5, git, zlib-devel, gcc-c++ >= 7"
	)

set(CPACK_DEBIAN_PACKAGE_DEPENDS
	"cmake (>= 3.12), libhdf5-dev, zlib1g-dev"
	)

set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "g++ (>= 7), hdf5-tools, libaec-dev, libsz2")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")

# This must always be last!
include(CPack)

