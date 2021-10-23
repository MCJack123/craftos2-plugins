/*
 * computronics-tape.cpp plugin for CraftOS-PC
 * Emulates the Computronics tape drive, including DFPWM audio playback.
 * Windows: cl /EHsc /Fecomputronics-tape.dll /LD /Icraftos2\api /Icraftos2\craftos2-lua\include computronics-tape.cpp /link craftos2\craftos2-lua\src\lua51.lib SDL2.lib SDL2_mixer.lib
 * Linux: g++ -fPIC -shared -Icraftos2/api -Icraftos2/craftos2-lua/include -o computronics-tape.so computronics-tape.cpp craftos2/craftos2-lua/src/liblua.a -lSDL2 -lSDL2_mixer
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

#include <fstream>
#include <CraftOS-PC.hpp>
#include <SDL2/SDL_mixer.h>

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define MAKELE(size, x) (x)
#define MAKEBE(size, x) (SDL_Swap##size(x))
#else
#define MAKELE(size, x) (SDL_Swap##size(x))
#define MAKEBE(size, x) (x)
#endif

static void au_decompress(int *fq, int *q, int *s, int *lt, int fs, int ri, int rd, int len, int8_t *outbuf, uint8_t *inbuf)
{
	int i,j;
	uint8_t d;
	for(i = 0; i < len; i++)
	{
		// get bits
		d = *(inbuf++);
		
		for(j = 0; j < 8; j++)
		{
			// set target
			int t = ((d&1) ? 127 : -128);
			d >>= 1;
			
			// adjust charge
			int nq = *q + ((*s * (t-*q) + 0x80)>>8);
			if(nq == *q && nq != t)
				*q += (t == 127 ? 1 : -1);
			int lq = *q;
			*q = nq;
			
			// adjust strength
			int st = (t != *lt ? 0 : 255);
			int sr = (t != *lt ? rd : ri);
			int ns = *s + ((sr*(st-*s) + 0x80)>>8);
			if(ns == *s && ns != st)
				ns += (st == 255 ? 1 : -1);
			*s = ns;
			
			// FILTER: perform antijerk
			int ov = (t != *lt ? (nq+lq)>>1 : nq);
			
			// FILTER: perform LPF
			*fq += ((fs*(ov-*fq) + 0x80)>>8);
			ov = *fq;
			
			// output sample
			*(outbuf++) = ov;
			
			*lt = t;
		}
	}
}

static void volumeEffect(int channel, void *stream, int len, void *udata);
static void volumeDone(int channel, void *udata);

class tape_drive: public peripheral {
    friend void volumeEffect(int channel, void *stream, int len, void *udata);
    friend void volumeDone(int channel, void *udata);
    std::string filename;
    uint8_t * data;
    uint8_t * end;
    uint8_t * pos;
    char label[27] = {0};
    float speed = 1.0;
    float volume = 1.0;
    Mix_Chunk * chunk = NULL;
    int channel = -1;
    Uint16 format = 0;
    int channels = 0;
    int isReady(lua_State *L) {
        lua_pushboolean(L, data != NULL);
        return 1;
    }
    int isEnd(lua_State *L) {
        lua_pushboolean(L, pos > end);
        return 1;
    }
    int getSize(lua_State *L) {
        lua_pushinteger(L, end - data);
        return 1;
    }
    int getLabel(lua_State *L) {
        lua_pushlstring(L, label, strnlen(label, 27));
        return 1;
    }
    int getState(lua_State *L) {
        if (chunk) lua_pushliteral(L, "PLAYING");
        else lua_pushliteral(L, "STOPPED");
        return 1;
    }
    int setLabel(lua_State *L) {
        strncpy(label, luaL_checkstring(L, 1), 27);
        return 0;
    }
    int setSpeed(lua_State *L) {
        float s = luaL_checknumber(L, 1);
        if (s < 0.25 || s > 2.0) luaL_error(L, "bad argument #1 (value out of range)");
        speed = s;
        return 0;
    }
    int setVolume(lua_State *L) {
        float v = luaL_checknumber(L, 1);
        if (v < 0.0 || v > 1.0) luaL_error(L, "bad argument #1 (value out of range)");
        volume = v;
        return 0;
    }
    int seek(lua_State *L) {
        ptrdiff_t offset = luaL_optinteger(L, 1, 0);
        uint8_t * old = pos;
        if (pos + offset > end) pos = end;
        else if (pos + offset < data) pos = data;
        else pos += offset;
        lua_pushinteger(L, pos - old);
        return 1;
    }
    int read(lua_State *L) {
        if (pos > end) return 0;
        if (lua_isnoneornil(L, 1)) lua_pushinteger(L, *pos++);
        else {
            ptrdiff_t sz = luaL_checkinteger(L, 1);
            if (sz < 0) luaL_error(L, "bad argument #1 (value out of range)");
            if (sz > end - pos) sz = end - pos;
            lua_pushlstring(L, (char*)pos, sz);
            pos += sz;
        }
        return 1;
    }
    int write(lua_State *L) {
        if (pos > end) return 0;
        if (lua_isnumber(L, 1)) *pos++ = lua_tointeger(L, 1);
        else if (lua_isstring(L, 1)) {
            size_t sz = 0;
            const char * str = lua_tolstring(L, 1, &sz);
            if (sz > end - pos) sz = end - pos;
            memcpy(pos, str, sz);
            pos += sz;
        } else luaL_typerror(L, 1, "number or string");
        return 0;
    }
    int play(lua_State *L) {
        int q = 0;
        int s = 0;
        int lt = -128;
        int ri = 7;
        int rd = 20;
        int fq = 0;
        int fs = 100;
        int frequency = 32768 * speed;

        if (chunk) Mix_HaltChannel(channel);
        int8_t * wav = new int8_t[(end - pos) * 8 + 44];
        memcpy(wav, "RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\0\0\0\0\0\0\0\0\x01\0\x08\0data", 40);
        *(uint32_t*)(wav + 4) = (end - pos) * 8 + 36;
        *(uint32_t*)(wav + 24) = frequency;
        *(uint32_t*)(wav + 28) = frequency;
        *(uint32_t*)(wav + 40) = (end - pos) * 8;
        au_decompress(&fq, &q, &s, &lt, fs, ri, rd, end - pos, wav + 44, pos);

        SDL_RWops * rw = SDL_RWFromMem(wav, end - pos + 44);
        chunk = Mix_LoadWAV_RW(rw, true);
        delete[] wav;
        int freq = 0;
        Mix_QuerySpec(&freq, &format, &channels);
        channel = Mix_PlayChannel(-1, chunk, 0);
        Mix_RegisterEffect(channel, volumeEffect, volumeDone, this);
        // chunk is automatically freed by CraftOS-PC
        return 0;
    }
    int stop(lua_State *L) {
        if (chunk) Mix_HaltChannel(channel);
        return 0;
    }
public:
    static library_t methods;
    tape_drive(lua_State *L, const char * side) {
        const char * file = luaL_optstring(L, 3, NULL);
        double tapeSizeF = luaL_optnumber(L, 4, 1.0);
        if (tapeSizeF < 0.0625 || tapeSizeF >= 16.0) throw std::invalid_argument("Tape size must be >= 64k and < 16M.");
        int tapeSize = tapeSizeF * 1048576;
        if (file) {
            filename = file;
            std::ifstream in(filename);
            if (in.is_open()) {
                char magic[5] = {0, 0, 0, 0, 0};
                in.read(magic, 4);
                if (strcmp(magic, "CTDT") != 0) {
                    in.close();
                    throw std::invalid_argument("Specified file is not a valid tape image.");
                }
                int size = in.get() << 16;
                in.read(label, 27);
                data = new uint8_t[size];
                pos = data;
                end = data + size;
                in.read((char*)data, size);
                in.close();
            } else {
                std::ofstream out(filename);
                if (!out.is_open()) throw std::invalid_argument("Specified file could not be written to.");
                data = new uint8_t[tapeSize];
                pos = data;
                end = data + tapeSize;
                memset(data, 0, tapeSize);
                out.write("CTDT", 4);
                out.put((end - data) >> 16);
                out.write(label, 27);
                out.write((char*)data, end - data);
                out.close();
            }
        } else {
            data = new uint8_t[tapeSize];
            pos = data;
            end = data + tapeSize;
            memset(data, 0, tapeSize);
        }
    }
    ~tape_drive() {
        if (chunk) Mix_HaltChannel(channel);
        if (!filename.empty()) {
            std::ofstream out(filename);
            if (out.is_open()) {
                out.write("CTDT", 4);
                out.put((end - data) >> 16);
                out.write(label, 27);
                out.write((char*)data, end - data);
                out.close();
            }
        }
        delete[] data;
    }
    static peripheral * init(lua_State *L, const char * side) {return new tape_drive(L, side);}
    static void deinit(peripheral * p) {delete (tape_drive*)p;}
    destructor getDestructor() const override {return deinit;}
    int call(lua_State *L, const char * method) override {
        const std::string m(method);
        if (m == "isReady") return isReady(L);
        else if (m == "isEnd") return isEnd(L);
        else if (m == "getSize") return getSize(L);
        else if (m == "getLabel") return getLabel(L);
        else if (m == "getState") return getState(L);
        else if (m == "setLabel") return setLabel(L);
        else if (m == "setSpeed") return setSpeed(L);
        else if (m == "setVolume") return setVolume(L);
        else if (m == "seek") return seek(L);
        else if (m == "read") return read(L);
        else if (m == "write") return write(L);
        else if (m == "play") return play(L);
        else if (m == "stop") return stop(L);
        else return 0;
    }
    void update() override {}
    library_t getMethods() const override {return methods;}
};

static luaL_Reg methods_reg[] = {
    {"isReady", NULL},
    {"isEnd", NULL},
    {"getSize", NULL},
    {"getLabel", NULL},
    {"getState", NULL},
    {"setLabel", NULL},
    {"setSpeed", NULL},
    {"setVolume", NULL},
    {"seek", NULL},
    {"read", NULL},
    {"write", NULL},
    {"play", NULL},
    {"stop", NULL},
    {NULL, NULL}
};
static PluginInfo info("tape");
library_t tape_drive::methods = {"tape_drive", methods_reg, nullptr, nullptr};

static void volumeEffect(int channel, void *stream, int len, void *udata) {
    tape_drive * drive = (tape_drive*)udata;
    switch (drive->format) {
        case AUDIO_U8: for (int i = 0; i < len; i++) ((uint8_t*)stream)[i] = ((uint8_t*)stream)[i] * drive->volume; break;
        case AUDIO_S8: for (int i = 0; i < len; i++) ((int8_t*)stream)[i] = ((int8_t*)stream)[i] * drive->volume; break;
        case AUDIO_U16LSB: for (int i = 0; i < len / 2; i++) ((uint16_t*)stream)[i] = MAKELE(16, MAKELE(16, ((uint16_t*)stream)[i]) * drive->volume); break;
        case AUDIO_U16MSB: for (int i = 0; i < len / 2; i++) ((uint16_t*)stream)[i] = MAKEBE(16, MAKEBE(16, ((uint16_t*)stream)[i]) * drive->volume); break;
        case AUDIO_S16LSB: for (int i = 0; i < len / 2; i++) ((int16_t*)stream)[i] = MAKELE(16, MAKELE(16, ((int16_t*)stream)[i]) * drive->volume); break;
        case AUDIO_S16MSB: for (int i = 0; i < len / 2; i++) ((int16_t*)stream)[i] = MAKEBE(16, MAKEBE(16, ((int16_t*)stream)[i]) * drive->volume); break;
        case AUDIO_S32LSB: for (int i = 0; i < len / 4; i++) ((int32_t*)stream)[i] = MAKELE(32, MAKELE(32, ((int32_t*)stream)[i]) * drive->volume); break;
        case AUDIO_S32MSB: for (int i = 0; i < len / 4; i++) ((int32_t*)stream)[i] = MAKEBE(32, MAKEBE(32, ((int32_t*)stream)[i]) * drive->volume); break;
        case AUDIO_F32LSB: for (int i = 0; i < len / 4; i++) ((float*)stream)[i] = MAKELE(Float, MAKELE(Float, ((float*)stream)[i]) * drive->volume); break;
        case AUDIO_F32MSB: for (int i = 0; i < len / 4; i++) ((float*)stream)[i] = MAKEBE(Float, MAKEBE(Float, ((float*)stream)[i]) * drive->volume); break;
    }
}

static void volumeDone(int channel, void *udata) {
    tape_drive * drive = (tape_drive*)udata;
    drive->chunk = NULL;
    drive->channel = -1;
}

extern "C" {
#ifdef _WIN32
_declspec(dllexport)
#endif
PluginInfo * plugin_init(PluginFunctions * func, const path_t& path) {
    func->registerPeripheral("tape_drive", &tape_drive::init);
    return &info;
}

#ifdef _WIN32
_declspec(dllexport)
#endif
int luaopen_tape(lua_State *L) {return 0;}
}