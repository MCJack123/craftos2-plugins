/*
 * periphemu_lua.cpp plugin for CraftOS-PC
 * Allows you to register custom peripherals that call back to Lua functions.
 * Windows: cl /EHsc /Feperiphemu_lua.dll /LD /Icraftos2\api /Icraftos2\craftos2-lua\include periphemu_lua.cpp /link craftos2\craftos2-lua\src\lua51.lib
 * Linux: g++ -fPIC -shared -Icraftos2/api -Icraftos2/craftos2-lua/include -o periphemu_lua.so periphemu_lua.cpp craftos2/craftos2-lua/src/liblua.a
 * Licensed under the MIT license.
 *
 * MIT License
 * 
 * Copyright (c) 2021 JackMacWindows
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
#include <CraftOS-PC.hpp>

class lua_peripheral: public peripheral {
public:
    library_t methods;
    const char * type;
    lua_Integer id;
    lua_peripheral(lua_State *L, const char * side, const char * type) {
        static const char * errmsg = "This peripheral type has been registered on another computer, but does not have a definition on this computer. Please call periphemu_lua.create with the definition on this computer first.";
        lua_getfield(L, LUA_REGISTRYINDEX, "periphemu_lua");
        if (lua_isnil(L, -1)) throw std::invalid_argument(errmsg);
        lua_getfield(L, -1, type);
        if (lua_isnil(L, -1)) throw std::invalid_argument(errmsg);
        std::vector<const char *> names;
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            lua_pop(L, 1);
            if (lua_isstring(L, -1)) {
                const char * str = lua_tostring(L, -1);
                if (strcmp(str, "__new") != 0 && strcmp(str, "__delete") != 0) names.push_back(str);
            }
        }

        lua_newtable(L);
        id = (lua_Integer)lua_topointer(L, -1);
        lua_pushinteger(L, id);
        lua_pushvalue(L, -2);
        lua_settable(L, -5);
        lua_getfield(L, -2, "__new");
        if (lua_isfunction(L, -1)) {
            lua_insert(L, 1);
            lua_insert(L, 2);
            lua_pop(L, 2);
            lua_call(L, lua_gettop(L)-1, 0);
        }

        this->type = type;
        methods.name = type;
        methods.functions = new luaL_Reg[names.size()+1];
        for (int i = 0; i < names.size(); i++) methods.functions[i] = {names[i], NULL};
        methods.functions[names.size()] = {NULL, NULL};
    }
    ~lua_peripheral() {
        delete[] methods.functions;
    }
    static void deinit(peripheral * p) {delete (lua_peripheral*)p;}
    destructor getDestructor() const override {return deinit;}
    int call(lua_State *L, const char * method) override {
        // We're going to assume that the peripheral is registered if it's at this point
        lua_getfield(L, LUA_REGISTRYINDEX, "periphemu_lua");
        lua_getfield(L, -1, type);
        lua_getfield(L, -1, method);
        if (!lua_isfunction(L, -1)) return luaL_error(L, "No such method");
        lua_insert(L, 1);
        lua_pushinteger(L, id);
        lua_gettable(L, -3);
        lua_insert(L, 2);
        lua_pop(L, 2);
        lua_call(L, lua_gettop(L)-1, LUA_MULTRET);
        return lua_gettop(L);
    }
    void update() override {}
    library_t getMethods() const override {return methods;}
};

static PluginFunctions * functions;
static PluginInfo info("periphemu_lua", 6);

static int periphemu_lua_create(lua_State *L) {
    const char * type = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, LUA_REGISTRYINDEX, "periphemu_lua");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_createtable(L, 0, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "periphemu_lua");
    }
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, type);
    functions->registerPeripheralFn(type, [type](lua_State *L, const char * side) {return new lua_peripheral(L, side, type);});
    return 0;
}

static const luaL_Reg periphemu_lua_methods[] = {
    {"create", periphemu_lua_create},
    {NULL, NULL}
};

extern "C" {
#ifdef _WIN32
_declspec(dllexport)
#endif
PluginInfo * plugin_init(PluginFunctions * func, const path_t& path) {
    functions = func;
    return &info;
}

#ifdef _WIN32
_declspec(dllexport)
#endif
int luaopen_periphemu_lua(lua_State *L) {
    luaL_register(L, "periphemu_lua", periphemu_lua_methods);
    return 1;
}
}
