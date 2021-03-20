local octave = 440
local keymap = {
    [keys.a] = 3,
    [keys.w] = 4,
    [keys.s] = 5,
    [keys.e] = 6,
    [keys.d] = 7,
    [keys.f] = 8,
    [keys.t] = 9,
    [keys.g] = 10,
    [keys.y] = 11,
    [keys.h] = 12,
    [keys.u] = 13,
    [keys.j] = 14,
    [keys.k] = 15,
}
local topRow = {[keys.w] = 2, [keys.e] = 4, [keys.t] = 8, [keys.y] = 10, [keys.u] = 12}
local bottomRow = {[keys.a] = 1, [keys.s] = 3, [keys.d] = 5, [keys.f] = 7, [keys.g] = 9, [keys.h] = 11, [keys.j] = 13, [keys.k] = 15}

local channels = {
    nil,
    nil,
    nil
}

sound.setVolume(1, 0)
sound.setVolume(2, 0)
sound.setVolume(3, 0)
sound.setVolume(4, 0)
sound.setWaveType(1, "sine")
sound.setWaveType(2, "sine")
sound.setWaveType(3, "sine")
sound.setWaveType(4, "noise")
sound.setFrequency(4, 1)

local x, y = term.getCursorPos()
local w, h = term.getSize()
if y == h then
    y = y - 1
    term.scroll(1)
end
term.setCursorPos(1, y)
term.blit(" W E   T Y U", "777777777777", "ffffffffffff")
term.setCursorPos(1, y + 1)
term.blit("A S D F G H J K", "777777777777777", "fffffffffffffff")
term.setCursorBlink(false)

while true do
    local ev, key, held = os.pullEvent()
    if ev == "key" and not held then
        if keymap[key] and #channels < 3 then
            local c = #channels+1
            channels[c] = key
            sound.setFrequency(c, octave * 2^(keymap[key]/12))
            sound.setVolume(c, 0.25)
            if topRow[key] then term.setCursorPos(topRow[key], y)
            else term.setCursorPos(bottomRow[key], y + 1) end
            term.blit(keys.getName(key):upper(), "0", "f")
        elseif key == keys.n then
            sound.setVolume(4, 0.1)
        elseif key == keys.m then
            sound.setVolume(4, 0.1)
            sound.fadeOut(4, 0.1)
        elseif key == keys.comma then
            sound.setVolume(4, 0.1)
            sound.fadeOut(4, 0.2)
        elseif key == keys.q then
            break
        end
    elseif ev == "key_up" then
        if keymap[key] then
            for i = 1, 3 do
                if key == channels[i] then
                    channels[i] = nil
                    sound.setVolume(i, 0)
                    if topRow[key] then term.setCursorPos(topRow[key], y)
                    else term.setCursorPos(bottomRow[key], y + 1) end
                    term.blit(keys.getName(key):upper(), "7", "f")
                    break
                end
            end
        elseif key == keys.n then
            sound.setVolume(4, 0)
        end
    end
end
