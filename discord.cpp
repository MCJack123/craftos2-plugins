/*
 * discord.cpp plugin for CraftOS-PC
 * Implements Discord Rich Presence for CraftOS-PC, showing the running program and computer name in Discord. Only works in CraftOS.
 * Uses the Discord GameSDK - download it from https://discord.com/developers/docs/game-sdk/sdk-starter-guide, then place `cpp/*` in `discord` and copy the relevant libraries from `lib`.
 * Requires a client ID at client_id.h, with the contents "static int64_t client_id = <YOUR CLIENT ID>;"
 * Windows: cl /EHsc /Fediscord.dll /LD /Icraftos2\api /Icraftos2\craftos2-lua\include discord.cpp /link craftos2\craftos2-lua\src\lua51.lib SDL2.lib discord_game_sdk.dll.lib
 * Linux: g++ -fPIC -shared -Icraftos2/api -Icraftos2/craftos2-lua/include -o discord.so discord.cpp craftos2/craftos2-lua/src/liblua.a -lSDL2 -ldiscord_game_sdk
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

#include <CraftOS-PC.hpp>
#include "discord/discord.h"
#include "client_id.h"

static PluginInfo info("discord");
static PluginFunctions * craftospc;
static discord::Core* core{};
static discord::Activity activity;
static bool connected = true;

static FileEntry autorun_hook {
    {"discord.lua", "-- Override loading functions to send info to Discord\n\
if not _G.discord then return end\n\
local discord = _G.discord\n\
_G.discord = nil\n\
local os, fs = os, fs\n\
local nativeload, nativeloadstring = load, loadstring\n\
local lastStatus = {'Computer ' .. os.computerID(), 'In the shell'}\n\
if os.computerLabel() then lastStatus[1] = 'Computer \"' .. os.computerLabel() .. '\"' end\n\
local function status(path, meta)\n\
    local name = 'Computer ' .. os.computerID()\n\
    if os.computerLabel() then name = 'Computer \"' .. os.computerLabel() .. '\"' end\n\
    path = fs.combine(path)\n\
    if path:match '^rom/modules' then return true end\n\
    local status\n\
    if path == 'rom/programs/shell.lua' or path == 'rom/programs/advanced/multishell.lua' or path == 'rom/programs/cash.lua' then status = 'In the shell'\n\
    elseif path == 'rom/programs/lua.lua' then status = 'In the Lua REPL'\n\
    elseif path == 'rom/programs/edit.lua' then status = 'Editing ' .. (meta or 'a file')\n\
    elseif path == 'rom/programs/fun/advanced/paint.lua' then status = 'Painting ' .. (meta or 'an image')\n\
    elseif path == 'rom/programs/help.lua' then status = 'Viewing ' .. (meta and 'help for ' .. meta or 'a help file')\n\
    elseif path == 'rom/programs/monitor.lua' then status = 'Running a program on ' .. (meta and 'monitor ' .. meta or 'a monitor')\n\
    elseif path == 'rom/programs/fun/adventure.lua' then status = 'Playing adventure'\n\
    elseif path == 'rom/programs/fun/worm.lua' then status = 'Playing worm'\n\
    elseif path == 'rom/programs/fun/advanced/redirection.lua' then status = 'Playing redirection'\n\
    elseif path == 'rom/programs/pocket/falling.lua' then status = 'Playing falling'\n\
    elseif path == 'rom/programs/rednet/chat.lua' then status = meta and meta == 'host' and 'Hosting a chat server' or 'Chatting on a server'\n\
    else status = 'Running ' .. path end\n\
    lastStatus = {name, status, lastStatus}\n\
    discord(name, status)\n\
end\n\
local function revert() lastStatus = lastStatus[3] or {'Unknown State', ''} discord(lastStatus[1], lastStatus[2]) end\n\
\n\
_G.load = function(chunk, name, mode, env)\n\
    if name and name:sub(1, 1) == '@' and not (type(mode) == 'string' and mode:match '_donotwrapfunction$') then\n\
        if type(mode) == 'string' then mode = mode:gsub('_donotwrapfunction$', '') end\n\
        local fn, err = nativeload(chunk, name, mode, env)\n\
        if not fn then return fn, err end\n\
        return function(...)\n\
            if status(name:sub(2)) then return fn(...) end\n\
            local res = table.pack(pcall(fn, ...))\n\
            revert()\n\
            if not res[1] then error(res[2], 0) end\n\
            return table.unpack(res, 2, res.n)\n\
        end\n\
    else return nativeload(chunk, name, mode, env) end\n\
end\n\
\n\
_G.loadstring = function(chunk, name)\n\
    if name and name:sub(1, 1) == '@' then\n\
        local fn, err = nativeloadstring(chunk, name)\n\
        if not fn then return fn, err end\n\
        return function(...)\n\
            if status(name:sub(2)) then return fn(...) end\n\
            local res = table.pack(pcall(fn, ...))\n\
            revert()\n\
            if not res[1] then error(res[2], 0) end\n\
            return table.unpack(res, 2, res.n)\n\
        end\n\
    else return nativeloadstring(chunk, name) end\n\
end\n\
\n\
_G.dofile = function(filename)\n\
    if type(filename) ~= 'string' then error('bad argument #1 (expected string, got ' .. type(filename) .. ')', 2) end\n\
    local file = fs.open(filename, 'r')\n\
    if not file then error('File not found', 2) end\n\
    local fn, err = load(file.readAll(), '@' .. filename, 'bt_donotwrapfunction', _G)\n\
    file.close()\n\
    if not fn then error(err, 2) end\n\
    if status(filename) then return fn() end\n\
    local res = table.pack(pcall(fn))\n\
    revert()\n\
    if not res[1] then error(res[2], 0) end\n\
    return table.unpack(res, 2, res.n)\n\
end\n\
\n\
os.run = function(env, path, ...)\n\
    if type(env) ~= 'table' then error('bad argument #1 (expected table, got ' .. type(env) .. ')', 2) end\n\
    if type(path) ~= 'string' then error('bad argument #2 (expected string, got ' .. type(path) .. ')', 2) end\n\
    setmetatable(env, {__index = _G})\n\
    if settings.get('bios.strict_globals', false) then\n\
        env._ENV = env\n\
        getmetatable(env).__newindex = function(_, name) error('Attempt to create global ' .. tostring(name), 2) end\n\
    end\n\
    local file = fs.open(path, 'r')\n\
    if not file then printError('File not found') return false end\n\
    local fn, err = load(file.readAll(), '@' .. path, 'bt_donotwrapfunction', env)\n\
    file.close()\n\
    if fn then\n\
        local s = status(path, ...)\n\
        local ok, err = pcall(fn, ...)\n\
        if not s then revert() end\n\
        if not ok then\n\
            if err and err ~= '' then printError(err) end\n\
            return false\n\
        end\n\
        return true\n\
    elseif err and err ~= '' then printError(err) end\n\
    return false\n\
end"}
};

static int discord_setPresence(lua_State *L) {
    if (!connected) return 0;
    fprintf(stderr, "Setting status to %s / %s\n", lua_tostring(L, 1), lua_tostring(L, 2));
    activity.SetState(lua_tostring(L, 1));
    activity.SetDetails(lua_tostring(L, 2));
    bool ready = false;
    core->ActivityManager().UpdateActivity(activity, [&ready](discord::Result res){
        if (res == discord::Result::NotRunning) {
            fprintf(stderr, "Discord disconnected. Restart CraftOS-PC to reconnect.\n");
            connected = false;
        }
        ready = true;
    });
    while (!ready) core->RunCallbacks();
    return 0;
}

extern "C" {
#ifdef _WIN32
_declspec(dllexport)
#endif
PluginInfo * plugin_init(PluginFunctions * func, const path_t& path) {
    craftospc = func;
    if (discord::Core::Create(client_id, DiscordCreateFlags_Default, &core) != discord::Result::Ok) {
        connected = false;
        info.failureReason = "Could not connect to Discord";
        return &info;
    }
    activity.SetApplicationId(client_id);
    activity.SetName("CraftOS-PC");
    activity.SetType(discord::ActivityType::Playing);
    activity.SetState("Starting Up");
    activity.GetAssets().SetLargeImage("craftos-pc");
    bool ready = false;
    core->ActivityManager().UpdateActivity(activity, [&ready](discord::Result res){ready = true;});
    while (!ready) core->RunCallbacks();
    return &info;
}

#ifdef _WIN32
_declspec(dllexport)
#endif
int luaopen_discord(lua_State *L) {
    if (get_comp(L)->isDebugger) return 0;
    craftospc->addVirtualMount(get_comp(L), autorun_hook, "/rom/autorun");
    lua_pushcfunction(L, discord_setPresence);
    return 1;
}

#ifdef _WIN32
_declspec(dllexport)
#endif
void plugin_deinit(PluginInfo * info) {
    delete core;
}
}
