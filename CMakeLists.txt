cmake_minimum_required(VERSION 3.22)
project(tremu C)

set(GLUE "${ANDROID_NDK}/sources/android/native_app_glue")
set(XR "${TOOLS}/openxr/prefab/modules")

add_library(native_app_glue STATIC "${GLUE}/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC "${GLUE}")

add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION "${XR}/openxr_loader/libs/android.arm64-v8a/libopenxr_loader.so")

add_library(tremu SHARED src/main.c src/core.c src/gun.c src/menu.c src/options.c src/carousel.c src/netplay.c src/internet.c src/extgun.c src/gamepad.c src/systems.c src/voxel.c src/depth.c src/lang.c src/padmap.c src/passthrough.c src/perf.c)
target_include_directories(tremu PRIVATE "${XR}/headers/include" third_party)
target_compile_options(tremu PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
# NativeActivity looks up ANativeActivity_onCreate from the glue library.
target_link_options(tremu PRIVATE -u ANativeActivity_onCreate)
target_link_libraries(tremu native_app_glue openxr_loader EGL GLESv3 android log aaudio dl m z)

# Netplay test bench run from adb shell (test/nettest.c); not part of the app.
if(TREMU_NETTEST)
  add_executable(tremu_nettest test/nettest.c src/core.c src/netplay.c src/internet.c src/lang.c)
  target_include_directories(tremu_nettest PRIVATE third_party)
  target_link_libraries(tremu_nettest EGL GLESv3 android log aaudio dl m z)
endif()
