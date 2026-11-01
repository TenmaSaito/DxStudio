cmake_minimum_required(VERSION 3.22.1)
project(DxStudioAndroid LANGUAGES C CXX)

set(NATIVE_GLUE "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(native_app_glue STATIC "${NATIVE_GLUE}/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC "${NATIVE_GLUE}")

add_library(dxstudio_android SHARED AndroidMain.cpp)
target_compile_features(dxstudio_android PRIVATE cxx_std_20)
target_link_libraries(dxstudio_android PRIVATE native_app_glue android log EGL GLESv3)
# NativeActivity starts the app through ANativeActivity_onCreate, which lives in
# the static glue library; without -u the linker drops it and the app stops
# immediately on every launch.
target_link_options(dxstudio_android PRIVATE "-u" "ANativeActivity_onCreate")
