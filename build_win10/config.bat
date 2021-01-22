set FFMPEG_DIR=C:\ffmpeg
set MSDK_DIR=C:\Program Files (x86)\IntelSWTools\Intel(R) Media SDK 2020 R1\Software Development Kit
set BUILD_TYPE=Release

cmake.exe -DHAVE_FFMPEG=TRUE ^
  -DFFMPEG_INCLUDE_DIRS="%FFMPEG_DIR%\include;%MSDK_DIR%\include;%~dp0..\3rdparty\include\opencl\1.2" ^
  -DFFMPEG_LIBRARIES="%FFMPEG_DIR%\lib\avcodec.lib;%FFMPEG_DIR%\lib\avformat.lib;%FFMPEG_DIR%\lib\avutil.lib;%FFMPEG_DIR%\lib\swscale.lib;%FFMPEG_DIR%\lib\swresample.lib" ^
  -DHAVE_FFMPEG_WRAPPER=FALSE -DOPENCV_FFMPEG_USE_FIND_PACKAGE=TRUE -DOPENCV_FFMPEG_SKIP_BUILD_CHECK=ON ^
  -DFFMPEG_libavcodec_VERSION=54.35.0 -DFFMPEG_libavformat_VERSION=54.20.4 -DFFMPEG_libavutil_VERSION=52.3.0 -DFFMPEG_libswscale_VERSION=2.1.1 ^
  -DWITH_MFX=ON -DHAVE_MFX=TRUE -DMFX_INCLUDE_DIRS="%MSDK_DIR%\include" -DMFX_LIBRARIES="%MSDK_DIR%\lib\x64\libmfx_vs2015.lib" ^
  -DBUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ..

md bin
md bin\%BUILD_TYPE%
copy "%FFMPEG_DIR%\bin\*" bin\%BUILD_TYPE%
