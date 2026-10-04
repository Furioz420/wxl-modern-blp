# wxl-modern-blp owns the shared BLP byte reshaper. Keeping it in this
# extension avoids linking modern texture policy into the core DLL.
file(GLOB_RECURSE WXL_EXT_SHARED_SRC CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/assets/shared/textures/blp/*.cpp")
