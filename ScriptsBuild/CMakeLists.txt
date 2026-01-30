cmake_minimum_required(VERSION 3.20)

# ============================================================================
# AliceRenderer - User Script 전용 CMake 프로젝트
# ============================================================================
project(AliceUserScripts LANGUAGES CXX)

if(MSVC)
    add_compile_options("/utf-8")  # 소스 및 실행 문자셋을 UTF-8로 강제
    add_compile_options("/wd4819") # 인코딩 경고 무시 (이미 utf-8 설정을 했으므로 안전)
endif()

# =========================================================================
# VCPKG 경로 설정 (자동 감지)
# =========================================================================
set(ALICE_VCPKG_PATH_OPTION "AUTO")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# 경로를 '진짜 절대 경로(Real Path)'로 확정
# ============================================================================
get_filename_component(ALICE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)

message(STATUS "-----------------------------------------------------------")
message(STATUS ">> Alice Root Path (Absolute): ${ALICE_ROOT}")

# ============================================================================
# [1] VCPKG 경로 감지 (변수 설정만 먼저 수행)
# ============================================================================
if(MSVC)
    if(ALICE_VCPKG_PATH_OPTION STREQUAL "AUTO")
        if(DEFINED ENV{VCPKG_ROOT})
            set(VCPKG_ROOT_DETECTED $ENV{VCPKG_ROOT})
            message(STATUS ">> [ScriptBuild] VCPKG: 환경변수 감지 -> ${VCPKG_ROOT_DETECTED}")
        elseif(EXISTS "D:/vcpkg/vcpkg.exe")
            set(VCPKG_ROOT_DETECTED "D:/vcpkg")
            message(STATUS ">> [ScriptBuild] VCPKG: D드라이브 감지 -> ${VCPKG_ROOT_DETECTED}")
        elseif(EXISTS "C:/vcpkg/vcpkg.exe")
            set(VCPKG_ROOT_DETECTED "C:/vcpkg")
            message(STATUS ">> [ScriptBuild] VCPKG: C드라이브 감지 -> ${VCPKG_ROOT_DETECTED}")
        else()
            message(FATAL_ERROR ">> [ScriptBuild] 오류: vcpkg를 찾을 수 없습니다.")
        endif()
    else()
        set(VCPKG_ROOT_DETECTED ${ALICE_VCPKG_PATH_OPTION})
        message(STATUS ">> [ScriptBuild] VCPKG: 사용자 지정 경로 사용 -> ${VCPKG_ROOT_DETECTED}")
    endif()

    # 변수 확정
    set(VCPKG_ROOT "${VCPKG_ROOT_DETECTED}" CACHE PATH "Path to vcpkg root" FORCE)
    set(VCPKG_TRIPLET_STATIC "x64-windows-static-md" CACHE STRING "vcpkg static triplet")
    set(VCPKG_TRIPLET_DYNAMIC "x64-windows"          CACHE STRING "vcpkg dynamic triplet")
endif()


# window, std 둘의 min, max 충돌삭제
add_compile_definitions(NOMINMAX)

# ==========================================
# RTTR 라이브러리 설정
# ==========================================
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(BUILD_INSTALLER OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(BUILD_STATIC OFF CACHE BOOL "" FORCE)
set(BUILD_RTTR_DYNAMIC ON CACHE BOOL "" FORCE)
set(BUILD_WITH_STATIC_RUNTIME_LIBS OFF CACHE BOOL "" FORCE)

# RTTR 서브모듈 추가
add_subdirectory("${ALICE_ROOT}/Engine/ThirdParty/rttr" "${CMAKE_BINARY_DIR}/Engine/ThirdParty/rttr")

message(STATUS "-----------------------------------------------------------")

# ============================================================================
# [2] 라이브러리 생성 (반드시 설정 적용보다 먼저 와야 함)
# ============================================================================
file(GLOB_RECURSE USER_SCRIPT_SOURCES CONFIGURE_DEPENDS
    "${ALICE_ROOT}/Assets/Scripts/*.cpp"
    "${ALICE_ROOT}/Assets/Scripts/*.h"
    "${ALICE_ROOT}/Assets/Scripts/*.hpp"
)

if(NOT USER_SCRIPT_SOURCES)
    message(STATUS "AliceUserScripts: no user script sources found under Assets/Scripts")
endif()

# ★ 여기서 타겟을 먼저 만듭니다
add_library(AliceScripts SHARED
    ${USER_SCRIPT_SOURCES}
)

# ============================================================================
# [3] VCPKG 및 Include 설정 적용 (타겟 생성 후 실행)
# ============================================================================
if(MSVC)
    # Include 경로 설정
    target_include_directories(AliceScripts
        PRIVATE
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/include"
            "${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/include"
    )

    # Lib 경로 설정
    target_link_directories(AliceScripts
        PRIVATE
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_STATIC}/lib>"
            "$<$<CONFIG:Debug>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/debug/lib>"
            "$<$<CONFIG:Release>:${VCPKG_ROOT}/installed/${VCPKG_TRIPLET_DYNAMIC}/lib>"
    )
endif()

# 나머지 헤더 경로 설정
target_include_directories(AliceScripts
    PRIVATE
        "${ALICE_ROOT}/Engine/src"
        "${ALICE_ROOT}/Engine/ThirdParty/ImGui"
        "${ALICE_ROOT}/Engine/ThirdParty/ImGui/backends"
        "${ALICE_ROOT}/Engine/ThirdParty/rttr/src"
)

# ==============================================================================
# [FMOD] 스크립트 프로젝트용 설정 (수정됨)
# ==============================================================================
# 1. 경로 설정 (ALICE_ROOT는 상단에서 정의된 프로젝트 루트)
set(FMOD_ROOT_SCRIPT "${ALICE_ROOT}/Engine/ThirdParty/FMOD")
set(FMOD_LIB_DIR_SCRIPT "${FMOD_ROOT_SCRIPT}/64x") # 폴더명: 64x
set(FMOD_INC_DIR_SCRIPT "${FMOD_ROOT_SCRIPT}/inc")

# 2. 헤더 경로 포함
if(EXISTS "${FMOD_INC_DIR_SCRIPT}")
    target_include_directories(AliceScripts PRIVATE "${FMOD_INC_DIR_SCRIPT}")
endif()

# 3. [핵심 수정] 라이브러리 검색 경로 등록 (이게 없어서 LNK1104 에러 발생)
target_link_directories(AliceScripts PRIVATE "${FMOD_LIB_DIR_SCRIPT}")

# 라이브러리 링크
target_link_libraries(AliceScripts
    PRIVATE
        "${ALICE_ROOT}/build/$<CONFIG>/Engine.lib"
        RTTR::Core

        d3d11
        dxgi
        dxguid
        d3dcompiler
        directxtk
        directxtex
        $<$<CONFIG:Debug>:assimp-vc143-mtd>
        $<$<CONFIG:Release>:assimp-vc143-mt>
)

# 4. FMOD 라이브러리 링크 (Debug/Release 구분)
#    Debug 빌드에서는 fmodL_vc.lib, Release 빌드에서는 fmod_vc.lib를 사용하도록 설정
if(EXISTS "${FMOD_LIB_DIR_SCRIPT}/fmod_vc.lib" AND EXISTS "${FMOD_LIB_DIR_SCRIPT}/fmodL_vc.lib")
    target_link_libraries(AliceScripts PRIVATE
        debug     fmodL_vc
        optimized fmod_vc
    )
endif()

# 참고: 이미 엔진(Engine.lib)이 FMOD DLL 복사를 담당하므로, 
# 스크립트 빌드에서는 DLL 복사 명령을 굳이 넣지 않아도 실행에는 문제없습니다.
# (실행 파일인 Launch.exe/Editor.exe가 있는 폴더에 DLL이 모이기 때문)

set_target_properties(AliceScripts PROPERTIES
    OUTPUT_NAME "AliceScripts"
)

# 핫리로드시 PDB 잠금 방지 (/DEBUG:NONE)
if(MSVC)
    target_link_options(AliceScripts PRIVATE "/DEBUG:NONE")

    # 소스 파일 UTF-8 인코딩 명시 (C4819 경고 방지)
    target_compile_options(AliceScripts PRIVATE "/utf-8")
endif()
