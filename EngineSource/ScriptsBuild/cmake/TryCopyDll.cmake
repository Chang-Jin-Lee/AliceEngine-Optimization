# 빌드 산출물을 대상 폴더로 복사하되, 대상이 잠겨 있으면(에디터가 로드 중)
# 빌드를 실패시키지 않고 넘어간다. 에디터의 Reload Scripts가 언로드 후 직접 복사한다.
# 사용법: cmake -DSRC=<file> -DDST_DIR=<dir> -P TryCopyDll.cmake
execute_process(
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DST_DIR}"
)
execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SRC}" "${DST_DIR}"
    RESULT_VARIABLE _copy_result
)
if(NOT _copy_result EQUAL 0)
    message(STATUS "[AliceScripts] 대상이 사용 중이라 복사를 건너뜀 (에디터 Reload Scripts가 처리): ${DST_DIR}")
endif()
