AC_INIT([CraftOS-PC plugins], [1.0])
AC_CONFIG_SRCDIR([ccemux.cpp])
m4_include([m4/find_cxx.m4])
m4_include([m4/find_cxx11.m4])

AC_ARG_WITH(sdl_mixer, [AS_HELP_STRING([--with-sdl_mixer], [build sound plugin])])
AC_ARG_WITH(sdl_mixer, [AS_HELP_STRING([--without-sdl_mixer], [do not build sound plugin])])
AC_ARG_WITH(discord, [AS_HELP_STRING([--with-discord], [build Discord plugin])])
AC_ARG_WITH(discord, [AS_HELP_STRING([--without-discord], [do not build Discord plugin])])
AC_ARG_WITH(glasses, [AS_HELP_STRING([--with-glasses], [build glasses plugin])])
AC_ARG_WITH(glasses, [AS_HELP_STRING([--without-glasses], [do not build glasses plugin])])

AC_LANG([C++])
CXXFLAGS="$CXXFLAGS -std=c++14"
AC_PROG_CC
AC_PROG_CXX
AX_CHECK_COMPILE_FLAG([-std=c++14], [], [AC_MSG_ERROR([C++ compiler does not support -std=c++14.])])

AC_SEARCH_LIBS(dlopen, dl, [], [AC_MSG_ERROR([Could not find dl library.])])
AC_CHECK_HEADERS(lua.h, [], [AC_MSG_ERROR([Could not find Lua header.])])
AC_SEARCH_LIBS(lua_halt, craftos2-lua lua lua51, [], [AC_MSG_ERROR([Could not find Lua library.])])
AC_CHECK_HEADERS([CraftOS-PC.hpp], [], [AC_MSG_ERROR([Could not find CraftOS-PC header.])])
AC_CHECK_HEADERS(SDL2/SDL.h, [], [AC_MSG_ERROR([Could not find SDL2 header.])])

AS_IF([test "x$with_glasses" != "xno"], [
    AC_SEARCH_LIBS(SDL_RenderGeometry, SDL2, [], [AS_IF([test "x$with_glasses" = "xyes"], [AC_MSG_ERROR([SDL2 library is too old.])], [NO_GLASSES=1])])
    AC_CHECK_HEADERS([SDL2/SDL2_gfxPrimitives.h], [], [AS_IF([test "x$with_glasses" = "xyes"], [AC_MSG_ERROR([Could not find SDL2_gfx header.])], [NO_GLASSES=1])])
    AC_SEARCH_LIBS(boxColor, SDL2_gfx, [], [AS_IF([test "x$with_glasses" = "xyes"], [AC_MSG_ERROR([Could not find SDL2_gfx library.])], [NO_GLASSES=1])])
    AC_CHECK_HEADERS([SDL2/SDL_ttf.h], [], [AS_IF([test "x$with_glasses" = "xyes"], [AC_MSG_ERROR([Could not find SDL2_ttf header.])], [NO_GLASSES=1])])
    AC_SEARCH_LIBS(TTF_Init, SDL2_ttf, [], [AS_IF([test "x$with_glasses" = "xyes"], [AC_MSG_ERROR([Could not find SDL2_ttf library.])], [NO_GLASSES=1])])
], [
    AC_SEARCH_LIBS(SDL_Init, SDL2, [], [AC_MSG_ERROR([Could not find SDL2 library.])])
    NO_DISCORD=1
])

AS_IF([test "x$with_sdl_mixer" != "xno"], [
    AC_CHECK_HEADERS(SDL2/SDL_mixer.h, [], [AS_IF([test "x$with_sdl_mixer" = "xyes"], [AC_MSG_ERROR([Could not find SDL2_mixer header.])], [NO_MIXER=1])])
    AC_SEARCH_LIBS(Mix_Init, SDL2_mixer, [], [AS_IF([test "x$with_sdl_mixer" = "xyes"], [AC_MSG_ERROR([Could not find SDL2_mixer library.])], [NO_MIXER=1])])
    AC_SEARCH_LIBS(mpg123_init, mpg123)
    AC_SEARCH_LIBS(FLAC__stream_decoder_new, flac)
    AC_SEARCH_LIBS(new_fluid_synth, fluidsynth, [], [AS_IF([test "x$with_sdl_mixer" = "xyes"], [AC_MSG_WARN([Could not find FluidSynth library. Soundfont support will be limited or non-existent.])], [])])
], [NO_MIXER=1])

AS_IF([test "x$with_discord" != "xno"], [
    AC_CHECK_HEADERS([discord/discord.h], [], [AS_IF([test "x$with_discord" = "xyes"], [AC_MSG_ERROR([Could not find Discord header.])], [NO_DISCORD=1])])
    AX_CXX_CHECK_LIB(discord_game_sdk, [discord::Core::Create], [], [AS_IF([test "x$with_discord" = "xyes"], [AC_MSG_ERROR([Could not find Discord library.])], [NO_DISCORD=1])])
], [NO_DISCORD=1])

AS_IF([test "$(uname)" = "Darwin"], [
    SO=bundle
    AC_SUBST(so, bundle)
    LDFLAGS="$LDFLAGS -bundle -fpic"
], [
    SO=so
    AC_SUBST(so, so)
    LDFLAGS="$LDFLAGS -shared -fPIC"
])

AS_IF([test "x$NO_MIXER" != "x1"], [AC_SUBST(SOUND_TARGET, sound.$SO)], [AC_SUBST(SOUND_TARGET, [])])
AS_IF([test "x$NO_DISCORD" != "x1"], [AC_SUBST(DISCORD_TARGET, discord.$SO)], [AC_SUBST(DISCORD_TARGET, [])])
AS_IF([test "x$NO_GLASSES" != "x1"], [AC_SUBST(GLASSES_TARGET, glasses.$SO)], [AC_SUBST(GLASSES_TARGET, [])])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT