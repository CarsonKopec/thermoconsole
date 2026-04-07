--[[
    ThermoConsole System Sounds
    
    Defines the system sound effects used by menus.
    These are simple synthesized sounds that don't require audio files.
]]

-- Sound definitions using frequency/duration pairs
-- In a real implementation, these would be passed to a synth

local sounds = {
    -- Menu navigation
    menu_move = {
        type = "square",
        notes = {{freq = 440, dur = 0.05}},
        volume = 0.3
    },
    
    -- Menu selection
    menu_select = {
        type = "square",
        notes = {
            {freq = 523, dur = 0.05},
            {freq = 659, dur = 0.05},
            {freq = 784, dur = 0.1}
        },
        volume = 0.4
    },
    
    -- Menu back/cancel
    menu_back = {
        type = "square",
        notes = {
            {freq = 392, dur = 0.05},
            {freq = 330, dur = 0.1}
        },
        volume = 0.3
    },
    
    -- Error/invalid
    menu_error = {
        type = "noise",
        notes = {{freq = 100, dur = 0.2}},
        volume = 0.2
    },
    
    -- Boot jingle
    boot_jingle = {
        type = "square",
        notes = {
            {freq = 262, dur = 0.1},  -- C4
            {freq = 330, dur = 0.1},  -- E4
            {freq = 392, dur = 0.1},  -- G4
            {freq = 523, dur = 0.2},  -- C5
        },
        volume = 0.5
    },
    
    -- Game launch
    game_launch = {
        type = "square",
        notes = {
            {freq = 392, dur = 0.05},
            {freq = 523, dur = 0.05},
            {freq = 659, dur = 0.05},
            {freq = 784, dur = 0.15},
        },
        volume = 0.4
    },
    
    -- Toggle on
    toggle_on = {
        type = "square",
        notes = {
            {freq = 440, dur = 0.03},
            {freq = 880, dur = 0.08}
        },
        volume = 0.3
    },
    
    -- Toggle off
    toggle_off = {
        type = "square",
        notes = {
            {freq = 880, dur = 0.03},
            {freq = 440, dur = 0.08}
        },
        volume = 0.3
    },
}

-- Simple beep fallback if synth not available
local function beep(freq, duration)
    -- Would call native beep function
    if _beep then
        _beep(freq, duration)
    end
end

-- Play a system sound
function play_system_sound(name)
    local sound = sounds[name]
    if not sound then return end
    
    -- Try native sound first
    if _play_synth then
        _play_synth(sound)
        return
    end
    
    -- Fallback to simple beeps
    for _, note in ipairs(sound.notes) do
        beep(note.freq, note.dur)
    end
end

-- Register as the sfx handler for system sounds
_G.sfx = function(name)
    -- Check if it's a system sound
    if sounds[name] then
        play_system_sound(name)
    else
        -- Pass through to game sfx system
        if _game_sfx then
            _game_sfx(name)
        end
    end
end

return sounds
