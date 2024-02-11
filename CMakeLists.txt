cmake_minimum_required (VERSION 3.2)
project (tools LANGUAGES C CXX)


if(MSVC)
	if(NOT MSVC_VERSION LESS 1400)
		add_definitions("-D _CRT_SECURE_NO_WARNINGS")
	endif()
endif()
if(CMAKE_COMPILER_IS_GNUCC)
endif(CMAKE_COMPILER_IS_GNUCC)


add_library(lzss-lib STATIC lzss-lib.c)


add_executable(CompileMLKTool CompileMLKTool.c)
install(TARGETS CompileMLKTool RUNTIME DESTINATION "bin")

add_executable(CompileWLKTool CompileWLKTool.c)
install(TARGETS CompileWLKTool RUNTIME DESTINATION "bin")

add_executable(danbidec danbidec.c)
install(TARGETS danbidec RUNTIME DESTINATION "bin")

add_executable(DiamondRushExtract DiamondRushExtract.c)
install(TARGETS DiamondRushExtract RUNTIME DESTINATION "bin")

add_executable(DIMUnpack DIMUnpack.c)
install(TARGETS DIMUnpack RUNTIME DESTINATION "bin")

add_executable(FoxRangerExtract FoxRangerExtract.c)
install(TARGETS FoxRangerExtract RUNTIME DESTINATION "bin")

add_executable(gensqu_dec gensqu_dec.c)
install(TARGETS gensqu_dec RUNTIME DESTINATION "bin")

add_executable(kenji_dec kenji_dec.c)
install(TARGETS kenji_dec RUNTIME DESTINATION "bin")

add_executable(LBXUnpack LBXUnpack.c)
install(TARGETS LBXUnpack RUNTIME DESTINATION "bin")

add_executable(lzss-tool lzss-tool.c lzss-lib)
install(TARGETS lzss-tool RUNTIME DESTINATION "bin")

add_executable(mrndec mrndec.c)
install(TARGETS mrndec RUNTIME DESTINATION "bin")

add_executable(piyo_dec piyo_dec.c)
install(TARGETS piyo_dec RUNTIME DESTINATION "bin")

add_executable(rekiai_dec rekiai_dec.c)
install(TARGETS rekiai_dec RUNTIME DESTINATION "bin")

add_executable(wolfteam_dec wolfteam_dec.c)
install(TARGETS wolfteam_dec RUNTIME DESTINATION "bin")

add_executable(x68k_sps_dec x68k_sps_dec.c)
install(TARGETS x68k_sps_dec RUNTIME DESTINATION "bin")

add_executable(xordec xordec.c)
install(TARGETS xordec RUNTIME DESTINATION "bin")
