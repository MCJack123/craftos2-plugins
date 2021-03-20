/*
 * sound.cpp plugin for CraftOS-PC
 * Adds a number of programmable sound channels (default 4) that play sound waves with the specified frequency, wave type, volume, and pan position.
 * Windows: cl /EHsc /Fesound.dll /LD /Icraftos2\api /Icraftos2\craftos2-lua\include plugin.cpp /link craftos2\craftos2-lua\src\lua51.lib SDL2.lib SDL2_mixer.lib
 * Linux: g++ -fPIC -shared -Icraftos2/api -Icraftos2/craftos2-lua/include -o sound.so sound.cpp craftos2/craftos2-lua/src/liblua.a -lSDL2 -lSDL2_mixer
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
#include <SDL2/SDL_mixer.h>
#include <cmath>
#include <chrono>
#include <random>
#include <mutex>
#define NUM_CHANNELS ((int)get_comp(L)->userdata[ChannelInfo::identifier+1])
#define channelGroup(id) ((id) | 0x74A800)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum class WaveType {
    None,
    Sine,
    Triangle,
    Sawtooth,
    RSawtooth,
    Square,
    Noise
};

struct ChannelInfo {
    static constexpr int identifier = 0x1d4c1cd0;
    int channelNumber;
    double position = 0.0;
    WaveType wavetype = WaveType::None;
    unsigned int frequency = 0;
    float amplitude = 1.0;
    float pan = 0.0;
    unsigned int fadeSamples = 0;
    unsigned int fadeSamplesMax = 0;
    float fadeSamplesInit = 0.0;
    bool halting = false;
    std::mutex lock;
    int channelCount = 4;
};

static Uint8 empty_audio[32];
static Mix_Chunk * empty_chunk;
static int targetFrequency = 0;
static Uint16 targetFormat = 0;
static int targetChannels = 0;
static std::default_random_engine rng;
static PluginFunctions * func;

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define MAKELE(size, x) (x)
#define MAKEBE(size, x) (SDL_Swap##size##(x))
#else
#define MAKELE(size, x) (SDL_Swap##size##(x))
#define MAKEBE(size, x) (x)
#endif

static void writeSample(float sample, void* data) {
    switch (targetFormat) {
        case AUDIO_S8: *(int8_t*)data = sample * INT8_MAX; return;
        case AUDIO_U8: *(uint8_t*)data = (sample + 1.0) * UINT8_MAX; return;
        case AUDIO_S16LSB: *(int16_t*)data = MAKELE(16, sample * INT16_MAX); return;
        case AUDIO_S16MSB: *(int16_t*)data = MAKEBE(16, sample * INT16_MAX); return;
        case AUDIO_U16LSB: *(uint16_t*)data = MAKELE(16, (sample + 1.0) * UINT16_MAX); return;
        case AUDIO_U16MSB: *(uint16_t*)data = MAKEBE(16, (sample + 1.0) * UINT16_MAX); return;
        case AUDIO_S32LSB: *(int32_t*)data = MAKELE(32, sample * INT32_MAX); return;
        case AUDIO_S32MSB: *(int32_t*)data = MAKEBE(32, sample * INT32_MAX); return;
        case AUDIO_F32LSB: *(float*)data = MAKELE(Float, sample); return;
        case AUDIO_F32MSB: *(float*)data = MAKEBE(Float, sample); return;
    }
}

static float getSample(WaveType type, double amplitude, double pos) {
    if (amplitude < 0.0001) return 0.0;
    switch (type) {
        case WaveType::Sine: return amplitude * sin(2.0 * pos * M_PI);
        case WaveType::Triangle: return 2.0 * abs(amplitude * fmod(2.0 * pos + 1.5, 2.0) - amplitude) - amplitude;
        case WaveType::Sawtooth: return amplitude * fmod(2.0 * pos + 1.0, 2.0) - amplitude;
        case WaveType::RSawtooth: return amplitude * fmod(2.0 * (1.0 - pos) + 1.0, 2.0) - amplitude;
        case WaveType::Square: return -2.0 * amplitude * floor(2 * fmod(pos, 1.0)) + amplitude;
        case WaveType::Noise: return amplitude * (((float)rng() / (float)rng.max()) * 2.0f - 1.0f);
        default: return 0.0;
    }
}

template<typename T> static T min(T a, T b) {return a < b ? a : b;}
template<typename T> static T max(T a, T b) {return a > b ? a : b;}

static void generateWaveform(int channel, void* stream, int length, void* udata) {
    ChannelInfo * info = (ChannelInfo*)udata;
    std::lock_guard<std::mutex> lock(info->lock);
    const int sampleSize = (SDL_AUDIO_BITSIZE(targetFormat) / 8) * targetChannels;
    int numSamples = length / sampleSize;
    for (int i = 0; i < numSamples; i++) {
        //if (info->wavetype == WaveType::Triangle) printf("%f %f\n", info->position, getSample(info->wavetype, info->amplitude, info->position));
        if (targetChannels == 1) {
            writeSample(info->frequency == 0 ? 0.0 : getSample(info->wavetype, info->amplitude, info->position), (uint8_t*)stream + i * sampleSize);
        } else {
            writeSample(info->frequency == 0 ? 0.0 : getSample(info->wavetype, info->amplitude * min(1.0 + info->pan, 1.0), info->position), (uint8_t*)stream + i * sampleSize);
            writeSample(info->frequency == 0 ? 0.0 : getSample(info->wavetype, info->amplitude * min(1.0 - info->pan, 1.0), info->position), (uint8_t*)stream + i * sampleSize + (SDL_AUDIO_BITSIZE(targetFormat) / 8));
            for (int j = 2; j < targetChannels; j++) writeSample(info->frequency == 0 ? 0.0 : getSample(info->wavetype, info->amplitude, info->position), (uint8_t*)stream + i * sampleSize + j * (SDL_AUDIO_BITSIZE(targetFormat) / 8));
        }
        info->position = fmod(info->position + (double)info->frequency / (double)targetFrequency, 1.0);
        if (info->fadeSamplesMax > 0) {
            info->amplitude -= info->fadeSamplesInit / info->fadeSamplesMax;
            if (--info->fadeSamples <= 0) {
                info->fadeSamples = info->fadeSamplesMax = 0;
                info->fadeSamplesInit = info->amplitude = 0.0f;
            }
        }
    }
}

static void channelFinished(int channel, void* udata) {
    if (!((ChannelInfo*)udata)->halting) Mix_PlayChannel(((ChannelInfo*)udata)->channelNumber, empty_chunk, -1);
}

static void ChannelInfo_destructor(Computer * comp, int id, void* data) {
    ChannelInfo * channels = (ChannelInfo*)data;
    for (int i = 0; i < channels[0].channelCount; i++) {
        channels[i].halting = true;
        Mix_HaltChannel(channels[i].channelNumber);
        Mix_UnregisterEffect(channels[i].channelNumber, generateWaveform);
        Mix_GroupChannel(channels[i].channelNumber, -1);
    }
    delete[] channels;
}

/*
 * Returns the type of wave assigned to the channel.
 * 1: The channel to check (1 - NUM_CHANNELS)
 * Returns: The current wave type
 */
static int sound_getWaveType(lua_State *L) {
    const int channel = luaL_checkinteger(L, 1);
    if (channel < 1 || channel > NUM_CHANNELS) luaL_error(L, "bad argument #1 (channel out of range)");
    ChannelInfo * info = (ChannelInfo*)get_comp(L)->userdata[ChannelInfo::identifier] + (channel - 1);
    switch (info->wavetype) {
        case WaveType::None: lua_pushstring(L, "none"); break;
        case WaveType::Sine: lua_pushstring(L, "sine"); break;
        case WaveType::Triangle: lua_pushstring(L, "triangle"); break;
        case WaveType::Sawtooth: lua_pushstring(L, "sawtooth"); break;
        case WaveType::RSawtooth: lua_pushstring(L, "rsawtooth"); break;
        case WaveType::Square: lua_pushstring(L, "square"); break;
        case WaveType::Noise: lua_pushstring(L, "noise"); break;
        default: lua_pushstring(L, "unknown"); break;
    }
    return 1;
}

/*
 * Sets the wave type for a channel.
 * 1: The channel to set (1 - NUM_CHANNELS)
 * 2: The type of wave as a string (from {"none", "sine", "triangle", "sawtooth", "square", and "noise"})
 */
static int sound_setWaveType(lua_State *L) {
    const int channel = luaL_checkinteger(L, 1);
    if (channel < 1 || channel > NUM_CHANNELS) luaL_error(L, "bad argument #1 (channel out of range)");
    ChannelInfo * info = (ChannelInfo*)get_comp(L)->userdata[ChannelInfo::identifier] + (channel - 1);
    std::string type = luaL_checkstring(L, 2);
    std::transform(type.begin(), type.end(), type.begin(), tolower);
    std::lock_guard<std::mutex> lock(info->lock);
    if (type == "none") info->wavetype = WaveType::None;
    else if (type == "sine") info->wavetype = WaveType::Sine;
    else if (type == "triangle") info->wavetype = WaveType::Triangle;
    else if (type == "sawtooth") info->wavetype = WaveType::Sawtooth;
    else if (type == "rsawtooth") info->wavetype = WaveType::RSawtooth;
    else if (type == "square") info->wavetype = WaveType::Square;
    else if (type == "noise") info->wavetype = WaveType::Noise;
    else luaL_error(L, "bad argument #2 (invalid option '%s')", type.c_str());
    return 0;
}

/*
 * Returns the frequency assigned to the channel.
 * 1: The channel to check (1 - NUM_CHANNELS)
 * Returns: The current frequency
 */
static int sound_getFrequency(lua_State *L) {
    const int channel = luaL_checkinteger(L, 1);
    if (channel < 1 || channel > NUM_CHANNELS) luaL_error(L, "bad argument #1 (channel out of range)");
    ChannelInfo * info = (ChannelInfo*)get_comp(L)->userdata[ChannelInfo::identifier] + (channel - 1);
    lua_pushinteger(L, info->frequency);
    return 1;
}

/*
 * Sets the frequency of the wave on a channel.
 * 1: The channel to set (1 - NUM_CHANNELS)
 * 2: The frequency in Hz
 */
static int sound_setFrequency(lua_State *L) {
    const int channel = luaL_checkinteger(L, 1);
    if (channel < 1 || channel > NUM_CHANNELS) luaL_error(L, "bad argument #1 (channel out of range)");
    ChannelInfo * info = (ChannelInfo*)get_comp(L)->userdata[ChannelInfo::identifier] + (channel - 1);
    lua_Integer frequency = luaL_checkinteger(L, 2);
    if (frequency < 0 || frequency > targetFrequency / 2) luaL_error(L, "bad argument #2 (frequency out of range)");
    std::lock_guard<std::mutex> lock(info->lock);
    info->frequency = frequency;
    return 0;
}

/*
 * Returns the volume of the channel.
 * 1: The channel to check (1 - NUM_CHANNELS)
 * Returns: The current volume
 */
static int sound_getVolume(lua_State *L) {
    const int channel = luaL_checkinteger(L, 1);
    if (channel < 1 || channel > NUM_CHANNELS) luaL_error(L, "bad argument #1 (channel out of range)");
    ChannelInfo * info = (ChannelInfo*)get_comp(L)->userdata[ChannelInfo::identifier] + (channel - 1);
    lua_pushnumber(L, info->amplitude);
    return 1;
}

/*
 * Sets the volume of a channel.
 * 1: The channel to set (1 - NUM_CHANNELS)
 * 2: The volume, from 0.0 to 1.0
 */
static int sound_setVolume(lua_State *L) {
    const int channel = luaL_checkinteger(L, 1);
    if (channel < 1 || channel > NUM_CHANNELS) luaL_error(L, "bad argument #1 (channel out of range)");
    ChannelInfo * info = (ChannelInfo*)get_comp(L)->userdata[ChannelInfo::identifier] + (channel - 1);
    float amplitude = luaL_checknumber(L, 2);
    if (amplitude < 0.0 || amplitude > 1.0) luaL_error(L, "bad argument #2 (volume out of range)");
    std::lock_guard<std::mutex> lock(info->lock);
    info->amplitude = amplitude;
    return 0;
}

/*
 * Returns the panning of the channel.
 * 1: The channel to check (1 - NUM_CHANNELS)
 * Returns: The current panning
 */
static int sound_getPan(lua_State *L) {
    const int channel = luaL_checkinteger(L, 1);
    if (channel < 1 || channel > NUM_CHANNELS) luaL_error(L, "bad argument #1 (channel out of range)");
    ChannelInfo * info = (ChannelInfo*)get_comp(L)->userdata[ChannelInfo::identifier] + (channel - 1);
    lua_pushnumber(L, info->pan);
    return 1;
}

/*
 * Sets the panning for a channel.
 * 1: The channel to set (1 - NUM_CHANNELS)
 * 2: The panning, from -1.0 (right) to 1.0 (left)
 */
static int sound_setPan(lua_State *L) {
    const int channel = luaL_checkinteger(L, 1);
    if (channel < 1 || channel > NUM_CHANNELS) luaL_error(L, "bad argument #1 (channel out of range)");
    ChannelInfo * info = (ChannelInfo*)get_comp(L)->userdata[ChannelInfo::identifier] + (channel - 1);
    float pan = luaL_checknumber(L, 2);
    if (pan < -1.0 || pan > 1.0) luaL_error(L, "bad argument #2 (pan out of range)");
    std::lock_guard<std::mutex> lock(info->lock);
    info->pan = pan;
    return 0;
}

/*
 * Starts or stops a fade out operation on a channel.
 * 1: The channel to fade out (1 - NUM_CHANNELS)
 * 2: The time for the fade out in seconds (0 to stop any fade out in progress)
 */
static int sound_fadeOut(lua_State *L) {
    const int channel = luaL_checkinteger(L, 1);
    if (channel < 1 || channel > NUM_CHANNELS) luaL_error(L, "bad argument #1 (channel out of range)");
    ChannelInfo * info = (ChannelInfo*)get_comp(L)->userdata[ChannelInfo::identifier] + (channel - 1);
    double time = luaL_checknumber(L, 2);
    if (time < 0.0) luaL_error(L, "bad argument #2 (time out of range)");
    std::lock_guard<std::mutex> lock(info->lock);
    if (time < 0.0001) {
        info->fadeSamplesInit = 0.0;
        info->fadeSamples = info->fadeSamplesMax = 0;
    } else {
        info->fadeSamplesInit = info->amplitude;
        info->fadeSamples = info->fadeSamplesMax = time * targetFrequency;
    }
    return 0;
}

static PluginInfo info("sound");
static luaL_Reg sound_lib[] = {
    {"getWaveType", sound_getWaveType},
    {"setWaveType", sound_setWaveType},
    {"getFrequency", sound_getFrequency},
    {"setFrequency", sound_setFrequency},
    {"getVolume", sound_getVolume},
    {"setVolume", sound_setVolume},
    {"getPan", sound_getPan},
    {"setPan", sound_setPan},
    {"fadeOut", sound_fadeOut},
    {NULL, NULL}
};

extern "C" {
#ifdef _WIN32
_declspec(dllexport)
#endif
PluginInfo * plugin_init(PluginFunctions * func, const path_t& path) {
    if (func->abi_version != PLUGIN_VERSION) return &info;
    memset(empty_audio, 0, 32);
    empty_chunk = Mix_QuickLoad_RAW(empty_audio, 32);
    rng.seed(std::chrono::system_clock::now().time_since_epoch().count());
    ::func = func;
    if (func->structure_version >= 2) func->registerConfigSetting("sound.numChannels", CONFIG_TYPE_INTEGER, [](const std::string&, void*)->int{return CONFIG_EFFECT_REOPEN;}, NULL);
    return &info;
}

#ifdef _WIN32
_declspec(dllexport)
#endif
int luaopen_sound(lua_State *L) {
    Computer * comp = get_comp(L);
    int num_channels = 4;
    if (func->structure_version >= 2) { // Plugin config is broken on v2.5-v2.5.2
        try {num_channels = func->getConfigSettingInt("sound.numChannels");}
        catch (...) {func->setConfigSettingInt("sound.numChannels", num_channels);}
    }
    if (comp->userdata.find(ChannelInfo::identifier) == comp->userdata.end()) {
        ChannelInfo * channels = new ChannelInfo[num_channels];
        Mix_QuerySpec(&targetFrequency, &targetFormat, &targetChannels);
        Mix_AllocateChannels(Mix_AllocateChannels(-1) + num_channels);
        for (int i = 0; i < num_channels; i++) {
            channels[i].channelCount = num_channels;
            channels[i].channelNumber = Mix_GroupAvailable(-1);
            while (channels[i].channelNumber == -1) {
                Mix_AllocateChannels(Mix_AllocateChannels(-1) + 1);
                channels[i].channelNumber = Mix_GroupAvailable(-1);
            }
            Mix_GroupChannel(channels[i].channelNumber, channelGroup(comp->id));
            Mix_RegisterEffect(channels[i].channelNumber, generateWaveform, channelFinished, &channels[i]);
            Mix_PlayChannel(channels[i].channelNumber, empty_chunk, -1);
        }
        comp->userdata[ChannelInfo::identifier] = channels;
        comp->userdata[ChannelInfo::identifier+1] = (void*)num_channels;
        comp->userdata_destructors[ChannelInfo::identifier] = ChannelInfo_destructor;
    }
    luaL_register(L, "sound", sound_lib);
    return 1;
}

#ifdef _WIN32
_declspec(dllexport)
#endif
void plugin_deinit(PluginInfo * info) {
    Mix_FreeChunk(empty_chunk);
}
}
