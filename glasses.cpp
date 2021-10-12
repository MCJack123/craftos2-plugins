/*
 * glasses.cpp plugin for CraftOS-PC
 * Adds the Plethora and Advanced Peripherals AR Glasses/Goggles, allowing vector graphics in a separate window.
 * Requires SDL 2.0.18+, SDL2_gfx, SDL2_ttf.
 * Windows: cl /EHsc /Feglasses.dll /LD /Icraftos2\api /Icraftos2\craftos2-lua\include glasses.cpp /link craftos2\craftos2-lua\src\lua51.lib SDL2.lib SDL2_gfx.lib SDL2_ttf.lib
 * Linux: g++ -fPIC -shared -Icraftos2/api -Icraftos2/craftos2-lua/include -o glasses.so glasses.cpp craftos2/craftos2-lua/src/liblua.a -lSDL2 -lSDL2_gfx -lSDL2_ttf
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
#include <thread>
#include <CraftOS-PC.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <SDL2/SDL_ttf.h>
#include "font.h"
#include "polypartition.h"
#include "polypartition.cpp"

#define rgba(color) (Uint8)((color >> 24) & 0xFF), (Uint8)((color >> 16) & 0xFF), (Uint8)((color >> 8) & 0xFF), (Uint8)(color & 0xFF)
#define addLuaMethod(name) lua_pushlightuserdata(L, this); \
    lua_pushcclosure(L, _lua_##name, 1); \
    lua_setfield(L, -2, #name);
#define LuaGetMethod(type, name, vartype) static int _lua_##name(lua_State *L) { \
        lua_push##vartype(L, ((type*)lua_touserdata(L, lua_upvalueindex(1)))->name()); \
        return 1; \
    }
#define LuaSetMethod(type, name, vartype) static int _lua_##name(lua_State *L) { \
        ((type*)lua_touserdata(L, lua_upvalueindex(1)))->name(luaL_check##vartype(L, 1)); \
        return 0; \
    }
#define LuaVoidMethod(type, name) static int _lua_##name(lua_State *L) { \
        ((type*)lua_touserdata(L, lua_upvalueindex(1)))->name(); \
        return 0; \
    }

namespace objects {namespace object2d {class Frame2D;}}

struct Item {
    std::string name;
    int damage;
};

struct GlassesRenderer {
    SDL_Window * win;
    SDL_Renderer * ren;
    SDL_GLContext glCtx;
    objects::object2d::Frame2D * canvas2d;
    std::mutex renderlock;
    Computer * computer;
    std::string side;

    GlassesRenderer();
    ~GlassesRenderer();
    bool render();
};

constexpr int WIDTH = 512;
constexpr int HEIGHT = 512 / 16 * 9;
static std::list<GlassesRenderer*> renderTargets;
static std::mutex renderTargetsLock;
static PluginFunctions * functions;
static PluginInfo info("glasses", 4);
static bool renderRunning = true;
static std::thread renderThread;

static std::vector<std::string> split(const std::string& strToSplit, const char * delims = "\n") {
    std::vector<std::string> retval;
    size_t pos = strToSplit.find_first_not_of(delims);
    while (pos != std::string::npos) {
        const size_t end = strToSplit.find_first_of(delims, pos);
        retval.push_back(strToSplit.substr(pos, end - pos));
        pos = strToSplit.find_first_not_of(delims, end);
    }
    return retval;
}

static SDL_Point luaL_checkpoint(lua_State *L, int arg) {
    SDL_Point retval;
    luaL_checktype(L, arg, LUA_TTABLE);
    lua_rawgeti(L, arg, 1);
    int tt = lua_type(L, -1);
    if (tt != LUA_TNUMBER) {
        lua_pop(L, 1);
        luaL_error(L, "bad X coordinate for argument #%d (expected number, got %s)", arg, lua_typename(L, tt));
    }
    retval.x = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_rawgeti(L, arg, 2);
    tt = lua_type(L, -1);
    if (tt != LUA_TNUMBER) {
        lua_pop(L, 1);
        luaL_error(L, "bad Y coordinate for argument #%d (expected number, got %s)", arg, lua_typename(L, tt));
    }
    retval.y = lua_tointeger(L, -1);
    lua_pop(L, 1);
    return retval;
}

// Code borrowed from SDL2_gfx
static int thickLineColor_ren(SDL_Renderer *renderer, Sint16 x1, Sint16 y1, Sint16 x2, Sint16 y2, Uint8 width, Uint32 color)
{
    int wh;
    double dx, dy, dx1, dy1, dx2, dy2;
    double l, wl2, nx, ny, ang, adj;
    SDL_Vertex vertices[6];

    if (renderer == NULL) {
        return -1;
    }

    if (width < 1) {
        return -1;
    }

    /* Special case: thick "point" */
    if ((x1 == x2) && (y1 == y2)) {
        wh = width / 2;
        return boxRGBA(renderer, x1 - wh, y1 - wh, x2 + width, y2 + width, rgba(color));		
    }

    /* Special case: width == 1 */
    if (width == 1) {
        return lineRGBA(renderer, x1, y1, x2, y2, rgba(color));		
    }

    /* Calculate offsets for sides */
    dx = (double)(x2 - x1);
    dy = (double)(y2 - y1);
    l = SDL_sqrt(dx*dx + dy*dy);
    ang = SDL_atan2(dx, dy);
    adj = 0.1 + 0.9 * SDL_fabs(SDL_cos(2.0 * ang));
    wl2 = ((double)width - adj)/(2.0 * l);
    nx = dx * wl2;
    ny = dy * wl2;

    /* Build polygon */
    dx1 = (double)x1;
    dy1 = (double)y1;
    dx2 = (double)x2;
    dy2 = (double)y2;
    vertices[0].position.x = (dx1 + ny);
    vertices[1].position.x = (dx1 - ny);
    vertices[2].position.x = (dx2 - ny);
    vertices[3].position.x = (dx2 + ny);
    vertices[4].position.x = (dx1 + ny);
    vertices[5].position.x = (dx2 - ny);
    vertices[0].position.y = (dy1 - nx);
    vertices[1].position.y = (dy1 + nx);
    vertices[2].position.y = (dy2 + nx);
    vertices[3].position.y = (dy2 - nx);
    vertices[4].position.y = (dy1 - nx);
    vertices[5].position.y = (dy2 + nx);
    for (wh = 0; wh < 6; wh++) {
        vertices[wh].color = {rgba(color)};
        vertices[wh].tex_coord = {0, 0};
    }

    /* Draw polygon */
    return SDL_RenderGeometry(renderer, NULL, vertices, 6, NULL, 0);
}

// This imitates the structure of Plethora's objects.
namespace objects {

    // Interfaces

    struct ObjectGroup;

    struct LuaObject {
        virtual void toLua(lua_State *L) = 0;
    };

    class BaseObject: public LuaObject {
        LuaVoidMethod(BaseObject, remove)
    protected:
        ObjectGroup * parent;
    public:
        BaseObject(ObjectGroup * p): parent(p) {}
        virtual void remove();
        virtual void draw(GlassesRenderer * ren, SDL_Point transform) = 0;
        virtual void setDirty();
        virtual void toLua(lua_State *L) override {
            lua_newtable(L);
            addLuaMethod(remove);
        }
    };

    struct Colorable: public LuaObject {
        static const unsigned int DEFAULT_COLOR = 0xFFFFFFFF;
        virtual int getAlpha() const = 0;
        virtual unsigned int getColor() const = 0;
        virtual void setAlpha(int alpha) = 0;
        virtual void setColor(unsigned int rgb) = 0;
        virtual void setColor(int r, int g, int b, int a = 255) = 0;
        // MARK: LuaObject
        virtual void toLua(lua_State *L) override {
            addLuaMethod(getAlpha)
            addLuaMethod(getColor)
            addLuaMethod(setAlpha)
            addLuaMethod(setColor)
        }
    private:
        LuaGetMethod(Colorable, getAlpha, integer)
        LuaGetMethod(Colorable, getColor, integer)
        static int _lua_setAlpha(lua_State *L) {
            ((Colorable*)lua_touserdata(L, lua_upvalueindex(1)))->setAlpha(luaL_checkinteger(L, 1) & 0xFF);
            return 0;
        }
        static int _lua_setColor(lua_State *L) {
            Colorable * obj = (Colorable*)lua_touserdata(L, lua_upvalueindex(1));
            if (!lua_isnoneornil(L, 2)) obj->setColor(luaL_checkinteger(L, 1) & 0xFF, luaL_checkinteger(L, 2) & 0xFF, luaL_checkinteger(L, 3) & 0xFF, luaL_optinteger(L, 4, 0xFF) & 0xFF);
            else obj->setColor(luaL_checkinteger(L, 1) & 0xFFFFFFFF);
            return 0;
        }
    };

    struct ItemObject: public LuaObject {
        virtual Item getItem() const = 0;
        virtual void setItem(Item item) = 0;
        // MARK: LuaObject
        virtual void toLua(lua_State *L) override {
            addLuaMethod(getItem)
            addLuaMethod(setItem)
        }
    private:
        static int _lua_getItem(lua_State *L) {
            Item it = ((ItemObject*)lua_touserdata(L, lua_upvalueindex(1)))->getItem();
            lua_pushstring(L, it.name.c_str());
            lua_pushinteger(L, it.damage);
            return 2;
        }
        static int _lua_setItem(lua_State *L) {
            ((ItemObject*)lua_touserdata(L, lua_upvalueindex(1)))->setItem({luaL_checkstring(L, 1), (int)luaL_optinteger(L, 2, 0)});
            return 0;
        }
    };

    struct ObjectGroup: public LuaObject {
        std::vector<BaseObject*> children;
        ~ObjectGroup() {
            for (BaseObject * o : children) delete o;
        }
        virtual void clear() = 0;
        virtual void setDirty() = 0;
        // MARK: LuaObject
        virtual void toLua(lua_State *L) override {
            addLuaMethod(clear)
        }
    private:
        LuaVoidMethod(ObjectGroup, clear)
    };

    struct Scalable: public LuaObject {
        virtual double getScale() const = 0;
        virtual void setScale(double scale) = 0;
        // MARK: LuaObject
        virtual void toLua(lua_State *L) override {
            addLuaMethod(getScale)
            addLuaMethod(setScale)
        }
    private:
        LuaGetMethod(Scalable, getScale, number)
        LuaSetMethod(Scalable, setScale, number)
    };

    struct TextObject: public LuaObject {
        virtual int getLineHeight() const = 0;
        virtual const char * getText() const = 0;
        virtual bool hasShadow() const = 0;
        virtual void setLineHeight(int height) = 0;
        virtual void setShadow(bool shadow) = 0;
        virtual void setText(const char * text) = 0;
        // MARK: LuaObject
        virtual void toLua(lua_State *L) override {
            addLuaMethod(getLineHeight)
            addLuaMethod(getText)
            addLuaMethod(hasShadow)
            addLuaMethod(setLineHeight)
            addLuaMethod(setShadow)
            addLuaMethod(setText)
        }
    private:
        LuaGetMethod(TextObject, getLineHeight, integer)
        LuaGetMethod(TextObject, getText, string)
        LuaGetMethod(TextObject, hasShadow, boolean)
        LuaSetMethod(TextObject, setLineHeight, integer)
        LuaSetMethod(TextObject, setText, string)
        static int _lua_setShadow(lua_State *L) {
            ((TextObject*)lua_touserdata(L, lua_upvalueindex(1)))->setShadow(lua_toboolean(L, 1));
            return 0;
        }
    };

    // Classes

    class ColorableObject: public BaseObject, Colorable {
    protected:
        unsigned int color = DEFAULT_COLOR;

    public:
        ColorableObject(ObjectGroup * p): BaseObject(p) {}

        // MARK: LuaObject

        virtual void toLua(lua_State *L) override {
            BaseObject::toLua(L);
            Colorable::toLua(L);
        }

        // MARK: Colorable

        virtual int getAlpha() const override {
            return color & 0xFF;
        }

        virtual unsigned int getColor() const override {
            return color;
        }

        virtual void setAlpha(int alpha) override {
            color = (color & 0xFFFFFF00) | alpha;
            setDirty();
        }

        virtual void setColor(unsigned int rgb) override {
            color = rgb;
        }

        virtual void setColor(int r, int g, int b, int a = 255) override {
            color = (r << 24) | (g << 16) | (b << 8) | a;
            setDirty();
        }
    };

    namespace object2d {

        // Interfaces

        struct Positionable2D: public LuaObject {
            virtual SDL_Point getPosition() const = 0;
            virtual void setPosition(SDL_Point pos) = 0;
            // MARK: LuaObject
            virtual void toLua(lua_State *L) override {
                addLuaMethod(getPosition)
                addLuaMethod(setPosition)
            }
        private:
            static int _lua_getPosition(lua_State *L) {
                SDL_Point p = ((Positionable2D*)lua_touserdata(L, lua_upvalueindex(1)))->getPosition();
                lua_pushinteger(L, p.x);
                lua_pushinteger(L, p.y);
                return 2;
            }
            static int _lua_setPosition(lua_State *L) {
                ((Positionable2D*)lua_touserdata(L, lua_upvalueindex(1)))->setPosition({(int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2)});
                return 0;
            }
        };

        struct MultiPoint2D: public LuaObject {
            virtual int getPointCount() const = 0;
            virtual SDL_Point getPoint(int idx) const = 0;
            virtual void setPoint(int idx, SDL_Point pt) = 0;
            // MARK: LuaObject
            virtual void toLua(lua_State *L) override {
                addLuaMethod(getPoint)
                addLuaMethod(setPoint)
            }
        private:
            static int _lua_getPoint(lua_State *L) {
                MultiPoint2D * obj = (MultiPoint2D*)lua_touserdata(L, lua_upvalueindex(1));
                int idx = luaL_checkinteger(L, 1);
                if (idx < 1 || idx > obj->getPointCount()) luaL_error(L, "bad argument #1 (index out of range)");
                SDL_Point p = obj->getPoint(idx - 1);
                lua_pushinteger(L, p.x);
                lua_pushinteger(L, p.y);
                return 2;
            }
            static int _lua_setPoint(lua_State *L) {
                MultiPoint2D * obj = (MultiPoint2D*)lua_touserdata(L, lua_upvalueindex(1));
                int idx = luaL_checkinteger(L, 1);
                if (idx < 1 || idx > obj->getPointCount()) luaL_error(L, "bad argument #1 (index out of range)");
                obj->setPoint(idx - 1, {(int)luaL_checkinteger(L, 2), (int)luaL_checkinteger(L, 3)});
                return 0;
            }
        };

        struct MultiPointResizable2D: public MultiPoint2D {
            virtual void insertPoint(int x, int y, int idx = INT_MAX) = 0;
            virtual void removePoint(int idx) = 0;
            // MARK: LuaObject
            virtual void toLua(lua_State *L) override {
                MultiPoint2D::toLua(L);
                addLuaMethod(getPointCount)
                addLuaMethod(insertPoint)
                addLuaMethod(removePoint)
            }
        private:
            LuaGetMethod(MultiPoint2D, getPointCount, integer)
            static int _lua_insertPoint(lua_State *L) {
                MultiPointResizable2D * obj = (MultiPointResizable2D*)lua_touserdata(L, lua_upvalueindex(1));
                if (!lua_isnoneornil(L, 3)) {
                    int idx = luaL_checkinteger(L, 1);
                    if (idx < 1) luaL_error(L, "bad argument #1 (index out of range)");
                    obj->insertPoint(luaL_checkinteger(L, 2), luaL_checkinteger(L, 3), idx - 1);
                } else obj->insertPoint(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2));
                return 0;
            }
            static int _lua_removePoint(lua_State *L) {
                MultiPointResizable2D * obj = (MultiPointResizable2D*)lua_touserdata(L, lua_upvalueindex(1));
                int idx = luaL_checkinteger(L, 1);
                if (idx < 1 || idx > obj->getPointCount()) luaL_error(L, "bad argument #1 (index out of range)");
                obj->removePoint(idx - 1);
                return 0;
            }
        };

        // Classes

        class Dot: public ColorableObject, Positionable2D, Scalable {
            SDL_Point position = {0, 0};
            double scale = 1.0;
        public:
            Dot(ObjectGroup * p): ColorableObject(p) {}

            // MARK: BaseObject

            virtual void draw(GlassesRenderer * ren, SDL_Point transform) override {
                boxColor(ren->ren, transform.x + position.x - scale, transform.y + position.y - scale, transform.x + position.x + scale, transform.y + position.y + scale, SDL_SwapBE32(color));
            }

            // MARK: LuaObject

            virtual void toLua(lua_State *L) override {
                ColorableObject::toLua(L);
                Positionable2D::toLua(L);
                Scalable::toLua(L);
            }

            // MARK: Positionable2D

            virtual SDL_Point getPosition() const override {
                return position;
            }

            virtual void setPosition(SDL_Point pos) override {
                position = pos;
                setDirty();
            }

            // MARK: Scalable

            virtual double getScale() const override {
                return scale;
            }

            virtual void setScale(double s) override {
                scale = s;
                setDirty();
            }
        };

        class Line: public ColorableObject, Scalable, MultiPoint2D {
            SDL_Point start = {0, 0}, end = {0, 0};
            double scale = 1.0;
        public:
            Line(ObjectGroup * p): ColorableObject(p) {}

            // MARK: BaseObject

            virtual void draw(GlassesRenderer * ren, SDL_Point transform) override {
                thickLineColor_ren(ren->ren, transform.x + start.x, transform.y + start.y, transform.x + end.x, transform.y + end.y, scale, color);
            }

            // MARK: LuaObject

            virtual void toLua(lua_State *L) override {
                ColorableObject::toLua(L);
                Scalable::toLua(L);
                MultiPoint2D::toLua(L);
            }

            // MARK: Scalable

            virtual double getScale() const override {
                return scale;
            }

            virtual void setScale(double s) override {
                scale = s;
                setDirty();
            }

            // MARK: MultiPoint2D

            virtual int getPointCount() const override {
                return 2;
            }

            virtual SDL_Point getPoint(int idx) const override {
                return idx ? end : start;
            }

            virtual void setPoint(int idx, SDL_Point pt) override {
                if (idx) end = pt;
                else start = pt;
                setDirty();
            }

        };

        class Rectangle: public ColorableObject, Positionable2D {
            SDL_Rect rect = {0, 0, 0, 0};
            static int _lua_getSize(lua_State *L) {
                SDL_Point p = ((Rectangle*)lua_touserdata(L, lua_upvalueindex(1)))->getSize();
                lua_pushinteger(L, p.x);
                lua_pushinteger(L, p.y);
                return 2;
            }
            static int _lua_setSize(lua_State *L) {
                ((Rectangle*)lua_touserdata(L, lua_upvalueindex(1)))->setSize({(int)luaL_checkinteger(L, 1), (int)luaL_checkinteger(L, 2)});
                return 0;
            }
        public:
            Rectangle(ObjectGroup * p): ColorableObject(p) {}

            SDL_Point getSize() const {
                return {rect.w, rect.h};
            }

            void setSize(SDL_Point size) {
                rect.w = size.x;
                rect.h = size.y;
                setDirty();
            }

            // MARK: BaseObject

            virtual void draw(GlassesRenderer * ren, SDL_Point transform) override {
                SDL_Rect nr = {transform.x + rect.x, transform.y + rect.y, rect.w, rect.h};
                SDL_SetRenderDrawColor(ren->ren, rgba(color));
                SDL_RenderDrawRect(ren->ren, &nr);
            }

            // MARK: LuaObject

            virtual void toLua(lua_State *L) override {
                ColorableObject::toLua(L);
                Positionable2D::toLua(L);
                addLuaMethod(getSize)
                addLuaMethod(setSize)
            }

            // MARK: Positionable2D

            virtual SDL_Point getPosition() const override {
                return {rect.x, rect.y};
            }

            virtual void setPosition(SDL_Point pos) override {
                rect.x = pos.x;
                rect.y = pos.y;
                setDirty();
            }

        };

        class Triangle: public ColorableObject, MultiPoint2D {
            SDL_Point points[3];
        public:
            Triangle(ObjectGroup * p): ColorableObject(p) {}

            // MARK: BaseObject

            virtual void draw(GlassesRenderer * ren, SDL_Point transform) override {
                SDL_Vertex vertices[3];
                for (int i = 0; i < 3; i++) vertices[i] = {{(float)points[i].x, (float)points[i].y}, {rgba(color)}, {0, 0}};
                SDL_RenderGeometry(ren->ren, NULL, vertices, 3, NULL, 0);
            }

            // MARK: LuaObject

            virtual void toLua(lua_State *L) override {
                ColorableObject::toLua(L);
                MultiPoint2D::toLua(L);
            }

            // MARK: MultiPoint2D

            virtual int getPointCount() const override {
                return 3;
            }

            virtual SDL_Point getPoint(int idx) const override {
                return points[idx];
            }

            virtual void setPoint(int idx, SDL_Point pt) override {
                points[idx] = pt;
                setDirty();
            }
        };

        class Polygon: public ColorableObject, MultiPointResizable2D {
        protected:
            std::vector<SDL_Point> points;
            TPPLPartition part;
            TPPLPolyList tris;
            bool pointsDirty = true;
        public:
            Polygon(ObjectGroup * p): ColorableObject(p) {}

            // MARK: BaseObject

            virtual void draw(GlassesRenderer * ren, SDL_Point transform) override {
                if (points.empty()) return;
                if (pointsDirty) {
                    TPPLPoly poly;
                    tris.clear();
                    poly.Init(points.size());
                    for (int i = 0; i < points.size(); i++) poly[i] = {(double)points[i].x, (double)points[i].y};
                    part.Triangulate_EC(&poly, &tris);
                    pointsDirty = false;
                }
                SDL_Vertex * vertices = new SDL_Vertex[tris.size()*3];
                int i = 0;
                for (auto it = tris.begin(); it != tris.end(); i++, it++) {
                    vertices[i*3] = {{(float)(*it)[0].x, (float)(*it)[0].y}, {rgba(color)}, {0, 0}};
                    vertices[i*3+1] = {{(float)(*it)[1].x, (float)(*it)[1].y}, {rgba(color)}, {0, 0}};
                    vertices[i*3+2] = {{(float)(*it)[2].x, (float)(*it)[2].y}, {rgba(color)}, {0, 0}};
                }
                SDL_RenderGeometry(ren->ren, NULL, vertices, tris.size()*3, NULL, 0);
                delete[] vertices;
            }

            // MARK: LuaObject

            virtual void toLua(lua_State *L) override {
                ColorableObject::toLua(L);
                MultiPointResizable2D::toLua(L);
            }

            // MARK: MultiPoint2D

            virtual int getPointCount() const override {
                return points.size();
            }

            virtual SDL_Point getPoint(int idx) const override {
                return points[idx];
            }

            virtual void setPoint(int idx, SDL_Point pt) override {
                points[idx] = pt;
                setDirty();
                pointsDirty = true;
            }

            // MARK: MultiPointResizable2D

            virtual void insertPoint(int x, int y, int idx = INT_MAX) override {
                if (idx >= points.size()) points.push_back({x, y});
                else points.insert(points.begin() + idx, {x, y});
                setDirty();
                pointsDirty = true;
            }

            virtual void removePoint(int idx) override {
                points.erase(points.begin() + idx);
                setDirty();
                pointsDirty = true;
            }

        };

        // TODO: fix thickness
        class LineLoop: public Polygon, Scalable {
            double scale = 1.0;
        public:
            LineLoop(ObjectGroup * p): Polygon(p) {}

            // MARK: BaseObject

            virtual void draw(GlassesRenderer * ren, SDL_Point transform) override {
                if (points.size() > 2) {
                    for (int i = 0; i < points.size() - 1; i++) {
                        thickLineColor_ren(ren->ren, transform.x + points[i].x, transform.y + points[i].y, transform.x + points[i+1].x, transform.y + points[i+1].y, scale, color);
                    }
                }
                if (points.size() > 1) thickLineColor_ren(ren->ren, transform.x + points.back().x, transform.y + points.back().y, transform.x + points.front().x, transform.y + points.front().y, scale, color);
            }

            // MARK: LuaObject

            virtual void toLua(lua_State *L) override {
                Polygon::toLua(L);
                Scalable::toLua(L);
            }

            // MARK: Scalable

            virtual double getScale() const override {
                return scale;
            }

            virtual void setScale(double s) override {
                scale = s;
            }

        };

        class Text: public ColorableObject, Positionable2D, Scalable, TextObject {
            SDL_Point position = {0, 0};
            double scale = 1.0;
            std::string text;
            bool shadow = false;
            int lineHeight = 0;
            SDL_Texture * texture = NULL;
        public:
            Text(ObjectGroup * p): ColorableObject(p) {}
            ~Text() {if (texture) SDL_DestroyTexture(texture);}

            // MARK: BaseObject

            virtual void draw(GlassesRenderer * ren, SDL_Point transform) override {
                SDL_RWops * rw = SDL_RWFromConstMem(font_ttf, font_ttf_len);
                TTF_Font * font = TTF_OpenFontRW(rw, true, scale);
                std::vector<std::string> lines = split(text);
                int sw = 0, sh = 0;
                for (int i = 0; i < lines.size(); i++) {
                    int w = 0;
                    TTF_SizeText(font, lines[i].c_str(), &w, &sh);
                    if (w > sw) sw = w;
                }
                sh += lineHeight * (lines.size() - 1);
                SDL_Surface * surf = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_RGBA32);
                for (int i = 0; i < lines.size(); i++) {
                    SDL_Surface * s = TTF_RenderText_Solid(font, lines[i].c_str(), {rgba(color)});
                    SDL_Rect rect = {0, i * lineHeight, s->w, s->h};
                    SDL_BlitSurface(s, NULL, surf, &rect);
                    SDL_FreeSurface(s);
                }
                TTF_CloseFont(font);
                if (texture) SDL_DestroyTexture(texture);
                texture = SDL_CreateTextureFromSurface(ren->ren, surf);
                SDL_Rect rect = {transform.x + position.x, transform.y + position.y, surf->w, surf->h};
                SDL_FreeSurface(surf);
                SDL_RenderCopy(ren->ren, texture, NULL, &rect);
            }

            // MARK: LuaObject

            virtual void toLua(lua_State *L) override {
                ColorableObject::toLua(L);
                Positionable2D::toLua(L);
                Scalable::toLua(L);
                TextObject::toLua(L);
            }

            // MARK: Positionable2D

            virtual SDL_Point getPosition() const override {
                return position;
            }

            virtual void setPosition(SDL_Point pos) override {
                position = pos;
                setDirty();
            }

            // MARK: Scalable

            virtual double getScale() const override {
                return scale;
            }

            virtual void setScale(double s) override {
                scale = s;
                setDirty();
            }
        
            // MARK: TextObject

            virtual int getLineHeight() const override {
                return lineHeight;
            }

            virtual const char * getText() const override {
                return text.c_str();
            }

            virtual bool hasShadow() const override {
                return shadow;
            }

            virtual void setLineHeight(int height) override {
                lineHeight = height;
                setDirty();
            }

            virtual void setShadow(bool s) override {
                shadow = s;
                setDirty();
            }

            virtual void setText(const char * t) override {
                text = t;
                setDirty();
            }

        };

        class ObjectGroup2D;

        // Interface (but this needs to be below the rest)
        struct Group2D: public ObjectGroup {
            virtual Dot * addDot(SDL_Point pos, unsigned int color = Colorable::DEFAULT_COLOR, int size = 1) = 0;
            virtual ObjectGroup2D * addGroup(SDL_Point pos) = 0;
            virtual Line * addLine(SDL_Point start, SDL_Point end, unsigned int color = Colorable::DEFAULT_COLOR, double thickness = 1.0) = 0;
            virtual LineLoop * addLines(const std::vector<SDL_Point>& points, unsigned int color = Colorable::DEFAULT_COLOR, double thickness = 1.0) = 0;
            virtual Polygon * addPolygon(const std::vector<SDL_Point>& points, unsigned int color = Colorable::DEFAULT_COLOR) = 0;
            virtual Rectangle * addRectangle(int x, int y, int width, int height, unsigned int color = Colorable::DEFAULT_COLOR) = 0;
            virtual Text * addText(SDL_Point pos, const std::string& contents, unsigned int color = Colorable::DEFAULT_COLOR, double size = 1.0) = 0;
            virtual Triangle * addTriangle(SDL_Point p1, SDL_Point p2, SDL_Point p3, unsigned int color = Colorable::DEFAULT_COLOR) = 0;
            // MARK: LuaObject
            virtual void toLua(lua_State *L) override {
                ObjectGroup::toLua(L);
                addLuaMethod(addDot)
                addLuaMethod(addGroup)
                addLuaMethod(addLine)
                addLuaMethod(addLines)
                addLuaMethod(addPolygon)
                addLuaMethod(addRectangle)
                addLuaMethod(addText)
                addLuaMethod(addTriangle)
            }
        private:
            static int _lua_addDot(lua_State *L) {
                Dot * obj = ((Group2D*)lua_touserdata(L, lua_upvalueindex(1)))->addDot(luaL_checkpoint(L, 1), luaL_optinteger(L, 2, 0xFFFFFFFF), luaL_optinteger(L, 3, 1));
                obj->toLua(L);
                return 1;
            }
            static int _lua_addGroup(lua_State *L);
            static int _lua_addLine(lua_State *L) {
                Line * obj = ((Group2D*)lua_touserdata(L, lua_upvalueindex(1)))->addLine(luaL_checkpoint(L, 1), luaL_checkpoint(L, 2), luaL_optinteger(L, 3, 0xFFFFFFFF), luaL_optnumber(L, 4, 1.0));
                obj->toLua(L);
                return 1;
            }
            static int _lua_addLines(lua_State *L) {
                std::vector<SDL_Point> points;
                luaL_checktype(L, 1, LUA_TTABLE);
                lua_rawgeti(L, 1, 1);
                for (int i = 1; !lua_isnil(L, -1); i++) {
                    points.push_back(luaL_checkpoint(L, -1));
                    lua_pop(L, 1);
                    lua_rawgeti(L, 1, i+1);
                }
                lua_pop(L, 1);
                LineLoop * obj = ((Group2D*)lua_touserdata(L, lua_upvalueindex(1)))->addLines(points, luaL_optinteger(L, 2, 0xFFFFFFFF), luaL_optnumber(L, 3, 1.0));
                obj->toLua(L);
                return 1;
            }
            static int _lua_addPolygon(lua_State *L) {
                std::vector<SDL_Point> points;
                luaL_checktype(L, 1, LUA_TTABLE);
                lua_rawgeti(L, 1, 1);
                for (int i = 1; !lua_isnil(L, -1); i++) {
                    points.push_back(luaL_checkpoint(L, -1));
                    lua_pop(L, 1);
                    lua_rawgeti(L, 1, i+1);
                }
                lua_pop(L, 1);
                Polygon * obj = ((Group2D*)lua_touserdata(L, lua_upvalueindex(1)))->addPolygon(points, luaL_optinteger(L, 2, 0xFFFFFFFF));
                obj->toLua(L);
                return 1;
            }
            static int _lua_addRectangle(lua_State *L) {
                Rectangle * obj = ((Group2D*)lua_touserdata(L, lua_upvalueindex(1)))->addRectangle(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2), luaL_checkinteger(L, 3), luaL_checkinteger(L, 4), luaL_optinteger(L, 5, 0xFFFFFFFF));
                obj->toLua(L);
                return 1;
            }
            static int _lua_addText(lua_State *L) {
                Text * obj = ((Group2D*)lua_touserdata(L, lua_upvalueindex(1)))->addText(luaL_checkpoint(L, 1), luaL_checkstring(L, 2), luaL_optinteger(L, 3, 0xFFFFFFFF), luaL_optnumber(L, 4, 1.0));
                obj->toLua(L);
                return 1;
            }
            static int _lua_addTriangle(lua_State *L) {
                Triangle * obj = ((Group2D*)lua_touserdata(L, lua_upvalueindex(1)))->addTriangle(luaL_checkpoint(L, 1), luaL_checkpoint(L, 2), luaL_checkpoint(L, 3), luaL_optinteger(L, 4, 0xFFFFFFFF));
                obj->toLua(L);
                return 1;
            }
        };

        class ObjectGroup2D: public BaseObject, Group2D, Positionable2D {
            SDL_Point position = {0, 0};
        public:
            ObjectGroup2D(ObjectGroup * p): BaseObject(p) {}

            // MARK: BaseObject

            virtual void draw(GlassesRenderer * ren, SDL_Point transform) override {
                transform.x += position.x;
                transform.y += position.y;
                for (BaseObject * obj : children) obj->draw(ren, transform);
            }

            // MARK: LuaObject

            virtual void toLua(lua_State *L) override {
                BaseObject::toLua(L);
                Group2D::toLua(L);
                Positionable2D::toLua(L);
            }

            // MARK: ObjectGroup

            virtual void clear() override {
                while (!children.empty()) children.front()->remove();
                setDirty();
            }

            virtual void setDirty() override {
                BaseObject::setDirty();
            }

            // MARK: Group2D

            virtual Dot * addDot(SDL_Point pos, unsigned int color = Colorable::DEFAULT_COLOR, int size = 1) override {
                Dot * retval = new Dot(this);
                retval->setPosition(pos);
                retval->setColor(color);
                retval->setScale(size);
                children.push_back(retval);
                setDirty();
                return retval;
            }

            virtual ObjectGroup2D * addGroup(SDL_Point pos) override {
                ObjectGroup2D * retval = new ObjectGroup2D(this);
                retval->setPosition(pos);
                children.push_back(retval);
                setDirty();
                return retval;
            }

            virtual Line * addLine(SDL_Point start, SDL_Point end, unsigned int color = Colorable::DEFAULT_COLOR, double thickness = 1.0) override {
                Line * retval = new Line(this);
                retval->setPoint(0, start);
                retval->setPoint(1, end);
                retval->setColor(color);
                retval->setScale(thickness);
                children.push_back(retval);
                setDirty();
                return retval;
            }

            virtual LineLoop * addLines(const std::vector<SDL_Point>& points, unsigned int color = Colorable::DEFAULT_COLOR, double thickness = 1.0) override {
                LineLoop * retval = new LineLoop(this);
                for (SDL_Point p : points) retval->insertPoint(p.x, p.y);
                retval->setScale(thickness);
                retval->setColor(color);
                children.push_back(retval);
                setDirty();
                return retval;
            }

            virtual Polygon * addPolygon(const std::vector<SDL_Point>& points, unsigned int color = Colorable::DEFAULT_COLOR) override {
                Polygon * retval = new Polygon(this);
                for (SDL_Point p : points) retval->insertPoint(p.x, p.y);
                retval->setColor(color);
                children.push_back(retval);
                setDirty();
                return retval;
            }

            virtual Rectangle * addRectangle(int x, int y, int width, int height, unsigned int color = Colorable::DEFAULT_COLOR) override {
                Rectangle * retval = new Rectangle(this);
                retval->setPosition({x, y});
                retval->setSize({width, height});
                retval->setColor(color);
                children.push_back(retval);
                setDirty();
                return retval;
            }

            virtual Text * addText(SDL_Point pos, const std::string& contents, unsigned int color = Colorable::DEFAULT_COLOR, double size = 1.0) override {
                Text * retval = new Text(this);
                retval->setPosition(pos);
                retval->setText(contents.c_str());
                retval->setColor(color);
                retval->setScale(size);
                children.push_back(retval);
                setDirty();
                return retval;
            }

            virtual Triangle * addTriangle(SDL_Point p1, SDL_Point p2, SDL_Point p3, unsigned int color = Colorable::DEFAULT_COLOR) override {
                Triangle * retval = new Triangle(this);
                retval->setPoint(0, p1);
                retval->setPoint(1, p2);
                retval->setPoint(2, p3);
                retval->setColor(color);
                children.push_back(retval);
                setDirty();
                return retval;
            }

            // MARK: Positionable2D

            virtual SDL_Point getPosition() const override {
                return position;
            }

            virtual void setPosition(SDL_Point pos) override {
                position = pos;
                setDirty();
            }

        };

        class Frame2D: public ObjectGroup2D {
            SDL_Point size;
            static int _lua_getSize(lua_State *L) {
                SDL_Point p = ((Frame2D*)lua_touserdata(L, lua_upvalueindex(1)))->getSize();
                lua_pushinteger(L, p.x);
                lua_pushinteger(L, p.y);
                return 2;
            }
        public:
            bool isDirty = true;
            Frame2D(SDL_Point sz): ObjectGroup2D(nullptr), size(sz) {setPosition({0, 0});}
            SDL_Point getSize() const {return size;}

            // MARK: BaseObject

            virtual void remove() {}
            virtual void setDirty() {isDirty = true;}

            // MARK: LuaObject

            virtual void toLua(lua_State *L) override {
                ObjectGroup2D::toLua(L);
                addLuaMethod(getSize)
            }
        };

    };

};

void objects::BaseObject::setDirty() {parent->setDirty();}
void objects::BaseObject::remove() {
    for (auto it = parent->children.begin(); it != parent->children.end(); it++) {
        if (*it == this) {
            it = parent->children.erase(it);
            if (it == parent->children.end()) break;
        }
    }
    parent->setDirty();
    delete this;
}
int objects::object2d::Group2D::_lua_addGroup(lua_State *L) {
    ObjectGroup2D * obj = ((Group2D*)lua_touserdata(L, lua_upvalueindex(1)))->addGroup(luaL_checkpoint(L, 1));
    obj->toLua(L);
    return 1;
}

GlassesRenderer::GlassesRenderer() {
    canvas2d = new objects::object2d::Frame2D({WIDTH, HEIGHT});
    functions->queueTask([this](void*)->void* {
        win = SDL_CreateWindow("CraftOS Terminal: Glasses", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT, SDL_WINDOW_OPENGL & 0);
        ren = SDL_GetRenderer(win);
        if (!ren) {
            ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
            if (!ren) {
                SDL_DestroyWindow(win);
                throw window_exception("Could not create renderer: " + std::string(SDL_GetError()));
            }
        }
        return NULL;
    }, NULL, false);
    std::lock_guard<std::mutex> lock(renderTargetsLock);
    renderTargets.push_back(this);
}

GlassesRenderer::~GlassesRenderer() {
    std::lock_guard<std::mutex> lock2(renderlock);
    {
        std::lock_guard<std::mutex> lock(renderTargetsLock);
        for (auto it = renderTargets.begin(); it != renderTargets.end(); ++it) {
            if (*it == this)
                it = renderTargets.erase(it);
            if (it == renderTargets.end()) break;
        }
    }
    // Delete allocated resources
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    delete canvas2d;
}

bool GlassesRenderer::render() {
    std::lock_guard<std::mutex> lock2(renderlock);
    if (!canvas2d->isDirty) return false;
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    canvas2d->draw(this, {0, 0});
    canvas2d->isDirty = false;
    return true;
}

class plethora_glasses: public peripheral {
    GlassesRenderer renderer;
public:
    static library_t methods;
    plethora_glasses(lua_State *L, const char * side) {
        renderer.computer = get_comp(L);
        renderer.side = side;
    }
    ~plethora_glasses(){}
    static peripheral * init(lua_State *L, const char * side) {return new plethora_glasses(L, side);}
    static void deinit(peripheral * p) {delete (plethora_glasses*)p;}
    destructor getDestructor() const override {return deinit;}
    int call(lua_State *L, const char * method) override {
        const std::string m(method);
        if (m == "canvas") {
            renderer.canvas2d->toLua(L);
            return 1;
        } else if (m == "canvas3d") {
            lua_pushnil(L); // todo
            return 1;
        } else if (m == "forceRender") {
            renderer.canvas2d->isDirty = true;
            return 0;
        } else return luaL_error(L, "No such method");
    }
    void update() override {}
    library_t getMethods() const override {return methods;}
};

static luaL_Reg plethora_methods_reg[] = {
    {"canvas", NULL},
    {"canvas3d", NULL},
    {NULL, NULL}
};

library_t plethora_glasses::methods = {"glasses", plethora_methods_reg, nullptr, nullptr};

static void glassesRenderLoop() {
    std::list<GlassesRenderer*> * renderList = new std::list<GlassesRenderer*>;
    while (renderRunning) {
        std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
        {
            std::lock_guard<std::mutex> lock(renderTargetsLock);
            for (GlassesRenderer* term : renderTargets) if (term->render()) {
                term->renderlock.lock();
                renderList->push_back(term);
            }
        }
        if (!renderList->empty()) {
            functions->queueTask([](void* p)->void* {
                auto renderList = (std::list<GlassesRenderer*>*)p;
                for (GlassesRenderer* term : *renderList) {
                    SDL_RenderPresent(term->ren);
                    term->renderlock.unlock();
                }
                delete renderList;
                return NULL;
            }, renderList, true);
            renderList = new std::list<GlassesRenderer*>;
        }
        const long long count = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start).count();
        //printf("Render thread took %lld us (%lld fps)\n", count, count == 0 ? 1000000 : 1000000 / count);
        long long t = (1000000/functions->config->clockSpeed) - count;
        if (t > 0) std::this_thread::sleep_for(std::chrono::microseconds(t));
    }
    delete renderList;
}

static bool sdlHook(SDL_Event * e, Computer * comp, Terminal * term, void* ud) {
    if (e->window.event == SDL_WINDOWEVENT_CLOSE) {
        for (GlassesRenderer * ren : renderTargets) {
            if (e->window.windowID == SDL_GetWindowID(ren->win)) {
                functions->detachPeripheral(ren->computer, ren->side);
                return true;
            }
        }
    }
    return false;
}

extern "C" {
#ifdef _WIN32
_declspec(dllexport)
#endif
PluginInfo * plugin_init(PluginFunctions * func, const path_t& path) {
    functions = func;
    SDL_version v;
    SDL_GetVersion(&v);
    if (v.patch < 18) {
        info.failureReason = "SDL version too old; please replace SDL in the executable";
        return &info;
    }
    TTF_Init();
    renderThread = std::thread(glassesRenderLoop);
    func->registerPeripheral("glasses", &plethora_glasses::init);
    func->registerSDLEvent(SDL_WINDOWEVENT, sdlHook, NULL);
    return &info;
}

#ifdef _WIN32
_declspec(dllexport)
#endif
int luaopen_glasses(lua_State *L) {
    return 0;
}

#ifdef _WIN32
_declspec(dllexport)
#endif
void plugin_deinit(PluginInfo * info) {
    if (!info->failureReason.empty()) return;
    renderRunning = false;
    renderThread.join();
    TTF_Quit();
}
}