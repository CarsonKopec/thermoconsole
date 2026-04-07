--[[
    ThermoConsole Pause Menu
    
    Overlay shown when Start is pressed during gameplay.
    Provides options for:
    - Resume
    - Restart game
    - Return to menu
    - Quick settings
    
    This is loaded as an overlay and drawn on top of the game.
]]

local SCREEN_W, SCREEN_H = 640, 480

-- State
local visible = false
local selected = 1
local frame = 0
local fade_in = 0

-- Menu options
local options = {
    { id = "resume", label = "Resume" },
    { id = "restart", label = "Restart Game" },
    { id = "menu", label = "Return to Menu" },
    { id = "settings", label = "Quick Settings" },
}

-- Quick settings submenu
local in_settings = false
local settings_selected = 1
local quick_settings = {
    { id = "volume", label = "Volume", value = 80, min = 0, max = 100 },
    { id = "brightness", label = "Brightness", value = 100, min = 10, max = 100 },
}

-- Colors
local C = {
    BLACK = 0,
    DARK_BLUE = 1,
    DARK_PURPLE = 2,
    DARK_GRAY = 5,
    LIGHT_GRAY = 6,
    WHITE = 7,
    YELLOW = 10,
    PINK = 14,
}

-- ═══════════════════════════════════════════════════════════════════════════════
-- Drawing Helpers
-- ═══════════════════════════════════════════════════════════════════════════════

local function draw_box(x, y, w, h, fill, border)
    if fill then
        rectfill(x, y, w, h, fill)
    end
    if border then
        rect(x, y, w, h, border)
    end
end

local function draw_text_centered(text, y, color)
    local w = #text * 4
    print(text, flr((SCREEN_W - w) / 2), y, color)
end

local function draw_slider(x, y, w, value, max_val)
    -- Background track
    draw_box(x, y + 3, w, 6, C.DARK_GRAY, nil)
    
    -- Fill
    local fill_w = flr((value / max_val) * w)
    if fill_w > 0 then
        rectfill(x, y + 3, fill_w, 6, C.YELLOW)
    end
    
    -- Knob
    local knob_x = x + fill_w - 4
    draw_box(knob_x, y, 8, 12, C.WHITE, C.LIGHT_GRAY)
    
    -- Value text
    print(value .. "%", x + w + 10, y + 2, C.WHITE)
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Main Drawing
-- ═══════════════════════════════════════════════════════════════════════════════

local function draw_overlay()
    -- Darken background with animated pattern
    for y = 0, SCREEN_H - 1, 2 do
        for x = 0, SCREEN_W - 1, 2 do
            if (x + y + frame) % 4 < 2 then
                pset(x, y, C.BLACK)
            end
        end
    end
end

local function draw_main_menu()
    -- Dialog box
    local box_w, box_h = 280, 220
    local box_x = (SCREEN_W - box_w) / 2
    local box_y = (SCREEN_H - box_h) / 2
    
    -- Animated entrance
    local scale = fade_in < 10 and (fade_in / 10) or 1
    box_w = box_w * scale
    box_h = box_h * scale
    box_x = (SCREEN_W - box_w) / 2
    box_y = (SCREEN_H - box_h) / 2
    
    if scale < 0.5 then return end
    
    -- Background
    draw_box(box_x, box_y, box_w, box_h, C.DARK_BLUE, C.WHITE)
    
    -- Header
    rectfill(box_x, box_y, box_w, 30, C.DARK_PURPLE)
    print("PAUSED", box_x + box_w / 2 - 20, box_y + 10, C.WHITE)
    
    -- Menu items
    local item_h = 35
    local start_y = box_y + 50
    
    for i, opt in ipairs(options) do
        local y = start_y + (i - 1) * item_h
        local is_selected = (i == selected)
        
        -- Selection highlight
        if is_selected then
            local pulse = flr(sin(frame / 8) * 2)
            rectfill(box_x + 10, y - 2, box_w - 20, item_h - 6, C.DARK_PURPLE)
            
            -- Arrow
            print(">", box_x + 20 + pulse, y + 8, C.YELLOW)
        end
        
        -- Label
        local color = is_selected and C.WHITE or C.LIGHT_GRAY
        print(opt.label, box_x + 40, y + 8, color)
    end
    
    -- Footer hints
    print("[A] Select  [START] Resume", box_x + 30, box_y + box_h - 25, C.DARK_GRAY)
end

local function draw_settings_menu()
    -- Dialog box
    local box_w, box_h = 320, 180
    local box_x = (SCREEN_W - box_w) / 2
    local box_y = (SCREEN_H - box_h) / 2
    
    -- Background
    draw_box(box_x, box_y, box_w, box_h, C.DARK_BLUE, C.WHITE)
    
    -- Header
    rectfill(box_x, box_y, box_w, 30, C.DARK_PURPLE)
    print("QUICK SETTINGS", box_x + box_w / 2 - 45, box_y + 10, C.WHITE)
    
    -- Settings
    local start_y = box_y + 50
    local item_h = 40
    
    for i, setting in ipairs(quick_settings) do
        local y = start_y + (i - 1) * item_h
        local is_selected = (i == settings_selected)
        
        -- Selection highlight
        if is_selected then
            rectfill(box_x + 10, y - 4, box_w - 20, item_h - 8, C.DARK_PURPLE)
        end
        
        -- Label
        local color = is_selected and C.WHITE or C.LIGHT_GRAY
        print(setting.label, box_x + 20, y + 4, color)
        
        -- Slider
        draw_slider(box_x + 120, y, 120, setting.value, setting.max)
    end
    
    -- Footer hints
    print("[<][>] Adjust  [B] Back", box_x + 60, box_y + box_h - 25, C.DARK_GRAY)
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Input Handling
-- ═══════════════════════════════════════════════════════════════════════════════

local function handle_main_input()
    if btnp(0) then  -- Up
        selected = selected - 1
        if selected < 1 then selected = #options end
        sfx("menu_move")
    elseif btnp(1) then  -- Down
        selected = selected + 1
        if selected > #options then selected = 1 end
        sfx("menu_move")
    elseif btnp(4) then  -- A - Select
        local opt = options[selected]
        sfx("menu_select")
        
        if opt.id == "resume" then
            return "resume"
        elseif opt.id == "restart" then
            return "restart"
        elseif opt.id == "menu" then
            return "menu"
        elseif opt.id == "settings" then
            in_settings = true
        end
    elseif btnp(8) then  -- Start - Resume
        return "resume"
    end
    
    return nil
end

local function handle_settings_input()
    local setting = quick_settings[settings_selected]
    
    if btnp(0) then  -- Up
        settings_selected = settings_selected - 1
        if settings_selected < 1 then settings_selected = #quick_settings end
        sfx("menu_move")
    elseif btnp(1) then  -- Down
        settings_selected = settings_selected + 1
        if settings_selected > #quick_settings then settings_selected = 1 end
        sfx("menu_move")
    elseif btn(2) then  -- Left - Decrease
        setting.value = math.max(setting.min, setting.value - 2)
        if _apply_setting then
            _apply_setting(setting.id, setting.value)
        end
    elseif btn(3) then  -- Right - Increase
        setting.value = math.min(setting.max, setting.value + 2)
        if _apply_setting then
            _apply_setting(setting.id, setting.value)
        end
    elseif btnp(5) then  -- B - Back
        sfx("menu_back")
        in_settings = false
    end
    
    return nil
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Public Interface
-- ═══════════════════════════════════════════════════════════════════════════════

-- Called to show the pause menu
function pause_show()
    visible = true
    selected = 1
    fade_in = 0
    in_settings = false
end

-- Called to hide the pause menu
function pause_hide()
    visible = false
end

-- Returns true if pause menu is visible
function pause_visible()
    return visible
end

-- Update pause menu (call from main game loop)
function pause_update()
    if not visible then return nil end
    
    frame = frame + 1
    fade_in = math.min(fade_in + 1, 15)
    
    if in_settings then
        return handle_settings_input()
    else
        return handle_main_input()
    end
end

-- Draw pause menu overlay (call after drawing game)
function pause_draw()
    if not visible then return end
    
    draw_overlay()
    
    if in_settings then
        draw_settings_menu()
    else
        draw_main_menu()
    end
end
