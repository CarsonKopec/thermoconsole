--[[
    ThermoConsole Boot Splash
    
    Animated boot screen shown during startup.
    Can be skipped by pressing any button.
]]

local SCREEN_W, SCREEN_H = 640, 480
local frame = 0
local boot_complete = false
local skip_requested = false

-- Animation phases
local PHASE_FADE_IN = 1
local PHASE_LOGO = 2
local PHASE_TEXT = 3
local PHASE_FADE_OUT = 4
local phase = PHASE_FADE_IN

-- Timing (in frames at 60fps)
local FADE_IN_DURATION = 30
local LOGO_DURATION = 90
local TEXT_DURATION = 60
local FADE_OUT_DURATION = 20

-- Colors
local C = {
    BLACK = 0,
    DARK_BLUE = 1,
    DARK_PURPLE = 2,
    WHITE = 7,
    ORANGE = 9,
    YELLOW = 10,
}

-- Particles for background effect
local particles = {}

local function init_particles()
    for i = 1, 30 do
        table.insert(particles, {
            x = rnd(SCREEN_W),
            y = rnd(SCREEN_H),
            vx = rnd(2) - 1,
            vy = rnd(2) - 1,
            size = 1 + flr(rnd(3)),
            color = 1 + flr(rnd(3))
        })
    end
end

local function update_particles()
    for _, p in ipairs(particles) do
        p.x = p.x + p.vx
        p.y = p.y + p.vy
        
        if p.x < 0 then p.x = SCREEN_W end
        if p.x > SCREEN_W then p.x = 0 end
        if p.y < 0 then p.y = SCREEN_H end
        if p.y > SCREEN_H then p.y = 0 end
    end
end

local function draw_particles(alpha)
    for _, p in ipairs(particles) do
        if alpha > 0.5 then
            circfill(p.x, p.y, p.size, p.color)
        end
    end
end

-- Logo drawing
local function draw_logo(x, y, scale, alpha)
    if alpha < 0.1 then return end
    
    -- "THERMO" text
    local thermo_color = alpha > 0.7 and C.ORANGE or C.DARK_PURPLE
    print("THERMO", x - 48 * scale, y - 8 * scale, thermo_color)
    
    -- "CONSOLE" text
    local console_color = alpha > 0.7 and C.YELLOW or C.DARK_PURPLE
    print("CONSOLE", x - 8 * scale, y - 8 * scale, console_color)
    
    -- Underline with animation
    if alpha > 0.5 then
        local line_w = 100 * scale * alpha
        local line_x = x - line_w / 2
        line(line_x, y + 4 * scale, line_x + line_w, y + 4 * scale, C.ORANGE)
    end
    
    -- Glow effect
    if alpha > 0.8 then
        local glow = flr(sin(frame / 10) * 2 + 2)
        for i = 1, glow do
            rect(x - 55 * scale - i, y - 12 * scale - i, 
                 110 * scale + i * 2, 24 * scale + i * 2, C.DARK_PURPLE)
        end
    end
end

local function draw_loading_bar(x, y, w, h, progress)
    -- Background
    rectfill(x, y, w, h, C.DARK_BLUE)
    rect(x, y, w, h, C.WHITE)
    
    -- Fill
    local fill_w = flr(progress * (w - 4))
    if fill_w > 0 then
        rectfill(x + 2, y + 2, fill_w, h - 4, C.ORANGE)
    end
    
    -- Shine effect
    if fill_w > 4 then
        local shine_x = x + 2 + (frame % fill_w)
        line(shine_x, y + 2, shine_x, y + h - 3, C.YELLOW)
    end
end

function _init()
    init_particles()
end

function _update()
    frame = frame + 1
    update_particles()
    
    -- Check for skip
    for i = 0, 9 do
        if btnp(i) then
            skip_requested = true
        end
    end
    
    -- Phase transitions
    if phase == PHASE_FADE_IN then
        if frame >= FADE_IN_DURATION then
            phase = PHASE_LOGO
            frame = 0
        end
    elseif phase == PHASE_LOGO then
        if frame >= LOGO_DURATION or skip_requested then
            phase = PHASE_TEXT
            frame = 0
        end
    elseif phase == PHASE_TEXT then
        if frame >= TEXT_DURATION or skip_requested then
            phase = PHASE_FADE_OUT
            frame = 0
        end
    elseif phase == PHASE_FADE_OUT then
        if frame >= FADE_OUT_DURATION then
            boot_complete = true
        end
    end
end

function _draw()
    -- Calculate alpha for current phase
    local alpha = 1
    
    if phase == PHASE_FADE_IN then
        alpha = frame / FADE_IN_DURATION
    elseif phase == PHASE_FADE_OUT then
        alpha = 1 - (frame / FADE_OUT_DURATION)
    end
    
    -- Background
    cls(C.BLACK)
    draw_particles(alpha)
    
    -- Center coordinates
    local cx, cy = SCREEN_W / 2, SCREEN_H / 2
    
    -- Logo
    if phase >= PHASE_LOGO then
        local logo_alpha = 1
        if phase == PHASE_FADE_IN then
            logo_alpha = alpha
        elseif phase == PHASE_FADE_OUT then
            logo_alpha = alpha
        end
        
        -- Logo position with bounce
        local bounce = 0
        if phase == PHASE_LOGO and frame < 30 then
            bounce = (30 - frame) * sin(frame / 5) * 0.5
        end
        
        draw_logo(cx, cy - 60 + bounce, 1, logo_alpha)
    end
    
    -- Loading bar
    if phase >= PHASE_TEXT then
        local progress = 0
        if phase == PHASE_TEXT then
            progress = frame / TEXT_DURATION
        else
            progress = 1
        end
        
        draw_loading_bar(cx - 100, cy + 40, 200, 16, progress)
        
        -- Loading text
        local dots = string.rep(".", (frame / 15) % 4)
        print("Loading" .. dots, cx - 28, cy + 70, C.WHITE)
    end
    
    -- Version
    if alpha > 0.5 then
        print("v1.0.0", SCREEN_W - 50, SCREEN_H - 20, C.DARK_PURPLE)
    end
    
    -- Skip hint
    if phase < PHASE_FADE_OUT and alpha > 0.7 then
        local hint_alpha = (frame % 60) < 30
        if hint_alpha then
            print("Press any button to skip", cx - 80, SCREEN_H - 40, C.DARK_PURPLE)
        end
    end
    
    -- Fade overlay
    if phase == PHASE_FADE_IN or phase == PHASE_FADE_OUT then
        local fade = flr((1 - alpha) * 16)
        if fade > 0 then
            for y = 0, SCREEN_H, 4 do
                for x = 0, SCREEN_W, 4 do
                    if rnd(16) < fade then
                        rectfill(x, y, 4, 4, C.BLACK)
                    end
                end
            end
        end
    end
end

-- Called by runtime to check if boot is done
function is_complete()
    return boot_complete
end
