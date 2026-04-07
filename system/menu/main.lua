--[[
    ThermoConsole System Menu
    
    The main menu system for ThermoConsole, handling:
    - Game selection and launching
    - System settings
    - USB mode toggle
    - Credits and about
    
    This runs as a special system app that can launch other games.
]]

-- ═══════════════════════════════════════════════════════════════════════════════
-- Configuration
-- ═══════════════════════════════════════════════════════════════════════════════

local VERSION = "1.0.0"
local SCREEN_W, SCREEN_H = 640, 480

-- Menu states
local STATE_MAIN = "main"
local STATE_GAMES = "games"
local STATE_SETTINGS = "settings"
local STATE_CREDITS = "credits"
local STATE_CONFIRM = "confirm"
local STATE_USB = "usb"

-- Colors (PICO-8 palette)
local C = {
    BLACK = 0,
    DARK_BLUE = 1,
    DARK_PURPLE = 2,
    DARK_GREEN = 3,
    BROWN = 4,
    DARK_GRAY = 5,
    LIGHT_GRAY = 6,
    WHITE = 7,
    RED = 8,
    ORANGE = 9,
    YELLOW = 10,
    GREEN = 11,
    BLUE = 12,
    INDIGO = 13,
    PINK = 14,
    PEACH = 15
}

-- ═══════════════════════════════════════════════════════════════════════════════
-- State
-- ═══════════════════════════════════════════════════════════════════════════════

local state = STATE_MAIN
local prev_state = nil

-- Animation
local frame = 0
local transition = 0
local transition_dir = 0

-- Main menu
local main_menu = {
    { id = "games", label = "GAMES", icon = "gamepad" },
    { id = "settings", label = "SETTINGS", icon = "gear" },
    { id = "usb", label = "USB MODE", icon = "usb" },
    { id = "credits", label = "CREDITS", icon = "info" },
}
local main_selected = 1

-- Games list
local games = {}
local games_selected = 1
local games_scroll = 0

-- Settings
local settings = {
    { id = "brightness", label = "Brightness", type = "slider", value = 100, min = 10, max = 100 },
    { id = "volume", label = "Volume", type = "slider", value = 80, min = 0, max = 100 },
    { id = "screen_filter", label = "Screen Filter", type = "toggle", value = false, options = {"Off", "Scanlines"} },
    { id = "show_fps", label = "Show FPS", type = "toggle", value = false },
    { id = "auto_save", label = "Auto Save", type = "toggle", value = true },
    { id = "sleep_timer", label = "Sleep Timer", type = "select", value = 1, options = {"Off", "5 min", "10 min", "30 min"} },
}
local settings_selected = 1

-- Credits
local credits = {
    { type = "title", text = "THERMOCONSOLE" },
    { type = "version", text = "Version " .. VERSION },
    { type = "spacer" },
    { type = "heading", text = "Hardware" },
    { type = "item", text = "Raspberry Pi Zero" },
    { type = "item", text = "Waveshare 2.8\" DPI LCD" },
    { type = "item", text = "Pi Pico Controller" },
    { type = "spacer" },
    { type = "heading", text = "Software" },
    { type = "item", text = "SDL2 - Graphics & Audio" },
    { type = "item", text = "Lua 5.4 - Scripting" },
    { type = "item", text = "Raspberry Pi OS" },
    { type = "spacer" },
    { type = "heading", text = "Inspired By" },
    { type = "item", text = "PICO-8" },
    { type = "item", text = "TIC-80" },
    { type = "item", text = "Playdate" },
    { type = "spacer" },
    { type = "heading", text = "Special Thanks" },
    { type = "item", text = "The open source community" },
    { type = "spacer" },
    { type = "footer", text = "Made with <3" },
}
local credits_scroll = 0

-- Confirmation dialog
local confirm = {
    title = "",
    message = "",
    options = {"Yes", "No"},
    selected = 2,
    callback = nil
}

-- USB mode
local usb_active = false

-- ═══════════════════════════════════════════════════════════════════════════════
-- Utility Functions
-- ═══════════════════════════════════════════════════════════════════════════════

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function clamp(v, min, max)
    return math.max(min, math.min(max, v))
end

local function ease_out(t)
    return 1 - (1 - t) ^ 3
end

local function ease_in_out(t)
    if t < 0.5 then
        return 4 * t * t * t
    else
        return 1 - ((-2 * t + 2) ^ 3) / 2
    end
end

-- Simple string split
local function split(str, sep)
    local result = {}
    for part in string.gmatch(str, "([^" .. sep .. "]+)") do
        table.insert(result, part)
    end
    return result
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Drawing Helpers
-- ═══════════════════════════════════════════════════════════════════════════════

local function draw_box(x, y, w, h, fill_color, border_color)
    if fill_color then
        rectfill(x, y, w, h, fill_color)
    end
    if border_color then
        rect(x, y, w, h, border_color)
    end
end

local function draw_rounded_box(x, y, w, h, fill_color, border_color)
    -- Simple rounded corners using rectangles
    if fill_color then
        rectfill(x + 2, y, w - 4, h, fill_color)
        rectfill(x, y + 2, w, h - 4, fill_color)
        rectfill(x + 1, y + 1, w - 2, h - 2, fill_color)
    end
    if border_color then
        line(x + 2, y, x + w - 3, y, border_color)
        line(x + 2, y + h - 1, x + w - 3, y + h - 1, border_color)
        line(x, y + 2, x, y + h - 3, border_color)
        line(x + w - 1, y + 2, x + w - 1, y + h - 3, border_color)
        pset(x + 1, y + 1, border_color)
        pset(x + w - 2, y + 1, border_color)
        pset(x + 1, y + h - 2, border_color)
        pset(x + w - 2, y + h - 2, border_color)
    end
end

local function draw_text_centered(text, y, color)
    local w = #text * 4  -- Approximate width
    print(text, flr((SCREEN_W - w) / 2), y, color)
end

local function draw_icon(icon, x, y, color)
    -- Simple pixel art icons
    if icon == "gamepad" then
        rectfill(x, y + 2, 12, 6, color)
        rectfill(x + 2, y, 2, 2, color)
        rectfill(x + 8, y, 2, 2, color)
        pset(x + 3, y + 4, C.BLACK)
        pset(x + 8, y + 4, C.BLACK)
    elseif icon == "gear" then
        circfill(x + 5, y + 5, 4, color)
        circfill(x + 5, y + 5, 2, C.BLACK)
        for i = 0, 7 do
            local a = i / 8
            local px = x + 5 + flr(cos(a) * 5)
            local py = y + 5 + flr(sin(a) * 5)
            pset(px, py, color)
        end
    elseif icon == "usb" then
        rectfill(x, y + 2, 8, 6, color)
        rectfill(x + 8, y + 3, 4, 4, color)
        line(x + 2, y, x + 2, y + 2, color)
        line(x + 5, y, x + 5, y + 2, color)
    elseif icon == "info" then
        circfill(x + 5, y + 5, 5, color)
        print("i", x + 3, y + 2, C.BLACK)
    elseif icon == "folder" then
        rectfill(x, y + 2, 12, 8, color)
        rectfill(x, y, 5, 3, color)
    elseif icon == "back" then
        line(x + 6, y + 2, x + 2, y + 5, color)
        line(x + 2, y + 5, x + 6, y + 8, color)
        line(x + 2, y + 5, x + 10, y + 5, color)
    elseif icon == "check" then
        line(x + 2, y + 5, x + 4, y + 8, color)
        line(x + 4, y + 8, x + 10, y + 2, color)
    end
end

local function draw_progress_bar(x, y, w, h, value, max_val, fg_color, bg_color)
    draw_box(x, y, w, h, bg_color, C.DARK_GRAY)
    local fill_w = flr((value / max_val) * (w - 2))
    if fill_w > 0 then
        rectfill(x + 1, y + 1, fill_w, h - 2, fg_color)
    end
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Background Animation
-- ═══════════════════════════════════════════════════════════════════════════════

local stars = {}
local function init_stars()
    for i = 1, 50 do
        table.insert(stars, {
            x = rnd(SCREEN_W),
            y = rnd(SCREEN_H),
            speed = 0.2 + rnd(0.8),
            brightness = 5 + flr(rnd(3))
        })
    end
end

local function update_stars()
    for _, star in ipairs(stars) do
        star.y = star.y + star.speed
        if star.y > SCREEN_H then
            star.y = 0
            star.x = rnd(SCREEN_W)
        end
    end
end

local function draw_stars()
    for _, star in ipairs(stars) do
        pset(flr(star.x), flr(star.y), star.brightness)
    end
end

local function draw_background()
    cls(C.DARK_BLUE)
    draw_stars()
    
    -- Subtle gradient at top
    for i = 0, 30 do
        local alpha = 1 - (i / 30)
        if alpha > 0.5 then
            line(0, i, SCREEN_W, i, C.BLACK)
        end
    end
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Header / Footer
-- ═══════════════════════════════════════════════════════════════════════════════

local function draw_header(title)
    -- Header background
    rectfill(0, 0, SCREEN_W, 32, C.DARK_PURPLE)
    line(0, 32, SCREEN_W, 32, C.INDIGO)
    
    -- Title
    print(title, 16, 12, C.WHITE)
    
    -- System icons (right side)
    local x = SCREEN_W - 40
    
    -- Battery indicator (placeholder)
    draw_box(x, 10, 16, 10, C.GREEN, C.WHITE)
    rectfill(x + 16, 13, 2, 4, C.WHITE)
    
    -- Time
    local time_str = os.date and os.date("%H:%M") or "12:00"
    print(time_str, SCREEN_W - 80, 12, C.LIGHT_GRAY)
end

local function draw_footer(left_text, right_text)
    -- Footer background
    rectfill(0, SCREEN_H - 24, SCREEN_W, 24, C.DARK_GRAY)
    line(0, SCREEN_H - 24, SCREEN_W, SCREEN_H - 24, C.LIGHT_GRAY)
    
    -- Button hints
    if left_text then
        print(left_text, 16, SCREEN_H - 16, C.WHITE)
    end
    if right_text then
        local w = #right_text * 4
        print(right_text, SCREEN_W - w - 16, SCREEN_H - 16, C.WHITE)
    end
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Game Scanner
-- ═══════════════════════════════════════════════════════════════════════════════

local function scan_games()
    games = {}
    
    -- In a real implementation, this would call a C function to scan the filesystem
    -- For now, we'll use placeholder data or the system call
    
    -- Try to read game list from system
    local game_list = _get_games and _get_games() or nil
    
    if game_list then
        for _, g in ipairs(game_list) do
            table.insert(games, {
                name = g.name or "Unknown",
                path = g.path or "",
                author = g.author or "",
                version = g.version or "1.0",
                description = g.description or "",
                icon = nil  -- Could load thumbnail
            })
        end
    else
        -- Placeholder games for testing
        table.insert(games, { name = "Space Dodge", path = "space_dodge", author = "ThermoConsole", version = "1.0" })
        table.insert(games, { name = "Hello World", path = "hello", author = "ThermoConsole", version = "1.0" })
        table.insert(games, { name = "Demo Platformer", path = "demo_platformer", author = "ThermoConsole", version = "1.0" })
    end
    
    -- Sort alphabetically
    table.sort(games, function(a, b) return a.name < b.name end)
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Menu Screens
-- ═══════════════════════════════════════════════════════════════════════════════

local function draw_main_menu()
    draw_header("THERMOCONSOLE")
    
    -- Logo area
    local logo_y = 60
    print("THERMO", SCREEN_W/2 - 48, logo_y, C.ORANGE)
    print("CONSOLE", SCREEN_W/2 - 8, logo_y, C.YELLOW)
    
    -- Animated underline
    local line_w = 120 + flr(sin(frame / 30) * 10)
    local line_x = (SCREEN_W - line_w) / 2
    line(line_x, logo_y + 12, line_x + line_w, logo_y + 12, C.ORANGE)
    
    -- Menu items
    local menu_y = 140
    local item_h = 50
    local item_w = 280
    local menu_x = (SCREEN_W - item_w) / 2
    
    for i, item in ipairs(main_menu) do
        local y = menu_y + (i - 1) * item_h
        local selected = (i == main_selected)
        
        -- Selection animation
        local offset = 0
        if selected then
            offset = flr(sin(frame / 10) * 3)
        end
        
        -- Background
        local bg_color = selected and C.DARK_PURPLE or C.DARK_GRAY
        local border_color = selected and C.PINK or C.LIGHT_GRAY
        draw_rounded_box(menu_x + offset, y, item_w, item_h - 8, bg_color, border_color)
        
        -- Icon
        draw_icon(item.icon, menu_x + offset + 16, y + 14, selected and C.YELLOW or C.WHITE)
        
        -- Label
        local text_color = selected and C.WHITE or C.LIGHT_GRAY
        print(item.label, menu_x + offset + 50, y + 16, text_color)
        
        -- Arrow indicator
        if selected then
            print(">", menu_x + item_w - 20 + offset, y + 16, C.YELLOW)
        end
    end
    
    draw_footer("[A] Select", "[B] ---")
end

local function draw_games_menu()
    draw_header("SELECT GAME")
    
    if #games == 0 then
        draw_text_centered("No games found!", SCREEN_H / 2 - 8, C.LIGHT_GRAY)
        draw_text_centered("Copy .tcr files via USB", SCREEN_H / 2 + 8, C.DARK_GRAY)
        draw_footer("[B] Back", "")
        return
    end
    
    -- Game list
    local list_y = 50
    local list_h = SCREEN_H - 100
    local item_h = 60
    local visible_items = flr(list_h / item_h)
    
    -- Adjust scroll
    if games_selected > games_scroll + visible_items then
        games_scroll = games_selected - visible_items
    elseif games_selected <= games_scroll then
        games_scroll = games_selected - 1
    end
    games_scroll = clamp(games_scroll, 0, math.max(0, #games - visible_items))
    
    -- Draw items
    for i = 1, visible_items do
        local game_idx = i + games_scroll
        if game_idx > #games then break end
        
        local game = games[game_idx]
        local y = list_y + (i - 1) * item_h
        local selected = (game_idx == games_selected)
        
        -- Background
        local bg_color = selected and C.DARK_PURPLE or nil
        if bg_color then
            draw_rounded_box(20, y, SCREEN_W - 40, item_h - 4, bg_color, C.INDIGO)
        end
        
        -- Icon placeholder
        draw_box(30, y + 8, 40, 40, C.DARK_GRAY, C.LIGHT_GRAY)
        draw_icon("gamepad", 38, y + 20, C.WHITE)
        
        -- Game info
        local text_color = selected and C.WHITE or C.LIGHT_GRAY
        print(game.name, 85, y + 12, text_color)
        print("by " .. (game.author or "Unknown"), 85, y + 28, C.DARK_GRAY)
        
        -- Version
        print("v" .. (game.version or "?"), SCREEN_W - 70, y + 12, C.DARK_GRAY)
    end
    
    -- Scrollbar
    if #games > visible_items then
        local sb_h = list_h
        local sb_y = list_y
        local thumb_h = flr((visible_items / #games) * sb_h)
        local thumb_y = sb_y + flr((games_scroll / (#games - visible_items)) * (sb_h - thumb_h))
        
        draw_box(SCREEN_W - 12, sb_y, 6, sb_h, C.DARK_GRAY, nil)
        draw_box(SCREEN_W - 12, thumb_y, 6, thumb_h, C.LIGHT_GRAY, nil)
    end
    
    draw_footer("[A] Play  [B] Back", "")
end

local function draw_settings_menu()
    draw_header("SETTINGS")
    
    local list_y = 60
    local item_h = 45
    
    for i, setting in ipairs(settings) do
        local y = list_y + (i - 1) * item_h
        local selected = (i == settings_selected)
        
        -- Background
        if selected then
            draw_rounded_box(20, y - 4, SCREEN_W - 40, item_h - 4, C.DARK_PURPLE, C.INDIGO)
        end
        
        -- Label
        local text_color = selected and C.WHITE or C.LIGHT_GRAY
        print(setting.label, 40, y + 8, text_color)
        
        -- Value display
        local value_x = SCREEN_W - 180
        
        if setting.type == "slider" then
            -- Progress bar
            draw_progress_bar(value_x, y + 6, 100, 12, setting.value, setting.max, C.GREEN, C.DARK_GRAY)
            print(setting.value .. "%", value_x + 110, y + 8, C.WHITE)
            
        elseif setting.type == "toggle" then
            -- Toggle switch
            local on = setting.value
            local switch_x = value_x + 60
            draw_box(switch_x, y + 4, 40, 16, on and C.GREEN or C.DARK_GRAY, C.LIGHT_GRAY)
            local knob_x = on and (switch_x + 22) or (switch_x + 2)
            draw_box(knob_x, y + 6, 16, 12, C.WHITE, nil)
            print(on and "ON" or "OFF", switch_x + 50, y + 8, on and C.GREEN or C.LIGHT_GRAY)
            
        elseif setting.type == "select" then
            -- Option selector
            local opt = setting.options[setting.value] or "?"
            print("< " .. opt .. " >", value_x + 40, y + 8, C.WHITE)
        end
    end
    
    draw_footer("[A] Change  [B] Back", "[<][>] Adjust")
end

local function draw_credits()
    draw_header("CREDITS")
    
    local y = 50 - credits_scroll
    
    for _, item in ipairs(credits) do
        if item.type == "title" then
            local pulse = 0.7 + sin(frame / 20) * 0.3
            local color = pulse > 0.5 and C.YELLOW or C.ORANGE
            draw_text_centered(item.text, y, color)
            y = y + 24
        elseif item.type == "version" then
            draw_text_centered(item.text, y, C.LIGHT_GRAY)
            y = y + 16
        elseif item.type == "heading" then
            draw_text_centered("[ " .. item.text .. " ]", y, C.BLUE)
            y = y + 20
        elseif item.type == "item" then
            draw_text_centered(item.text, y, C.WHITE)
            y = y + 14
        elseif item.type == "spacer" then
            y = y + 16
        elseif item.type == "footer" then
            draw_text_centered(item.text, y, C.PINK)
            y = y + 16
        end
    end
    
    draw_footer("[B] Back", "[^][v] Scroll")
end

local function draw_usb_mode()
    draw_header("USB MODE")
    
    -- Big USB icon
    local icon_x = SCREEN_W / 2 - 40
    local icon_y = 100
    
    -- Animated USB icon
    local bounce = flr(sin(frame / 15) * 5)
    
    -- USB plug shape
    rectfill(icon_x, icon_y + bounce, 80, 40, C.WHITE)
    rectfill(icon_x + 80, icon_y + 10 + bounce, 20, 20, C.LIGHT_GRAY)
    rectfill(icon_x + 15, icon_y - 10 + bounce, 10, 15, C.WHITE)
    rectfill(icon_x + 55, icon_y - 10 + bounce, 10, 15, C.WHITE)
    
    -- Status
    if usb_active then
        draw_text_centered("USB STORAGE ACTIVE", 200, C.GREEN)
        draw_text_centered("Console appears as TCGAMES drive", 230, C.WHITE)
        draw_text_centered("Copy .tcr games, then eject safely", 250, C.LIGHT_GRAY)
        
        -- Pulsing indicator
        local pulse = (frame % 60) < 30
        circfill(SCREEN_W / 2, 290, 8, pulse and C.GREEN or C.DARK_GREEN)
        print("Connected", SCREEN_W / 2 - 30, 310, C.GREEN)
    else
        draw_text_centered("Press [A] to enable USB mode", 200, C.WHITE)
        draw_text_centered("Games will pause while USB is active", 230, C.LIGHT_GRAY)
        draw_text_centered("Hold START+SELECT on hardware", 260, C.DARK_GRAY)
    end
    
    draw_footer(usb_active and "[A] Disable" or "[A] Enable", "[B] Back")
end

local function draw_confirm_dialog()
    -- Darken background
    rectfill(0, 0, SCREEN_W, SCREEN_H, C.BLACK)
    
    -- Dialog box
    local w, h = 300, 140
    local x, y = (SCREEN_W - w) / 2, (SCREEN_H - h) / 2
    
    draw_rounded_box(x, y, w, h, C.DARK_GRAY, C.WHITE)
    
    -- Title
    print(confirm.title, x + 20, y + 16, C.YELLOW)
    line(x + 10, y + 30, x + w - 10, y + 30, C.LIGHT_GRAY)
    
    -- Message
    print(confirm.message, x + 20, y + 50, C.WHITE)
    
    -- Options
    local btn_w = 80
    local btn_y = y + h - 40
    
    for i, opt in ipairs(confirm.options) do
        local btn_x = x + (w / (#confirm.options + 1)) * i - btn_w / 2
        local selected = (i == confirm.selected)
        
        draw_rounded_box(btn_x, btn_y, btn_w, 25, selected and C.DARK_PURPLE or C.DARK_GRAY, selected and C.PINK or C.LIGHT_GRAY)
        print(opt, btn_x + btn_w / 2 - #opt * 2, btn_y + 8, selected and C.WHITE or C.LIGHT_GRAY)
    end
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Input Handling
-- ═══════════════════════════════════════════════════════════════════════════════

local function handle_main_input()
    if btnp(0) then  -- Up
        main_selected = main_selected - 1
        if main_selected < 1 then main_selected = #main_menu end
        sfx("menu_move")
    elseif btnp(1) then  -- Down
        main_selected = main_selected + 1
        if main_selected > #main_menu then main_selected = 1 end
        sfx("menu_move")
    elseif btnp(4) then  -- A - Select
        local item = main_menu[main_selected]
        sfx("menu_select")
        if item.id == "games" then
            scan_games()
            state = STATE_GAMES
        elseif item.id == "settings" then
            state = STATE_SETTINGS
        elseif item.id == "usb" then
            state = STATE_USB
        elseif item.id == "credits" then
            credits_scroll = 0
            state = STATE_CREDITS
        end
    end
end

local function handle_games_input()
    if btnp(0) then  -- Up
        games_selected = games_selected - 1
        if games_selected < 1 then games_selected = #games end
        sfx("menu_move")
    elseif btnp(1) then  -- Down
        games_selected = games_selected + 1
        if games_selected > #games then games_selected = 1 end
        sfx("menu_move")
    elseif btnp(4) then  -- A - Launch game
        if #games > 0 then
            local game = games[games_selected]
            sfx("menu_select")
            -- Launch the game
            if _launch_game then
                _launch_game(game.path)
            end
        end
    elseif btnp(5) then  -- B - Back
        sfx("menu_back")
        state = STATE_MAIN
    end
end

local function handle_settings_input()
    local setting = settings[settings_selected]
    
    if btnp(0) then  -- Up
        settings_selected = settings_selected - 1
        if settings_selected < 1 then settings_selected = #settings end
        sfx("menu_move")
    elseif btnp(1) then  -- Down
        settings_selected = settings_selected + 1
        if settings_selected > #settings then settings_selected = 1 end
        sfx("menu_move")
    elseif btnp(2) or btnp(3) then  -- Left/Right - Adjust
        local dir = btnp(3) and 1 or -1
        
        if setting.type == "slider" then
            setting.value = clamp(setting.value + dir * 10, setting.min, setting.max)
            sfx("menu_move")
        elseif setting.type == "toggle" then
            setting.value = not setting.value
            sfx("menu_select")
        elseif setting.type == "select" then
            setting.value = setting.value + dir
            if setting.value < 1 then setting.value = #setting.options end
            if setting.value > #setting.options then setting.value = 1 end
            sfx("menu_move")
        end
        
        -- Apply setting
        if _apply_setting then
            _apply_setting(setting.id, setting.value)
        end
        
    elseif btnp(4) then  -- A - Toggle
        if setting.type == "toggle" then
            setting.value = not setting.value
            sfx("menu_select")
            if _apply_setting then
                _apply_setting(setting.id, setting.value)
            end
        end
    elseif btnp(5) then  -- B - Back
        sfx("menu_back")
        state = STATE_MAIN
    end
end

local function handle_credits_input()
    if btn(0) then  -- Up - Scroll
        credits_scroll = credits_scroll - 2
        if credits_scroll < 0 then credits_scroll = 0 end
    elseif btn(1) then  -- Down - Scroll
        credits_scroll = credits_scroll + 2
    elseif btnp(5) then  -- B - Back
        sfx("menu_back")
        state = STATE_MAIN
    end
end

local function handle_usb_input()
    if btnp(4) then  -- A - Toggle USB
        usb_active = not usb_active
        sfx("menu_select")
        if _toggle_usb then
            _toggle_usb(usb_active)
        end
    elseif btnp(5) then  -- B - Back
        if not usb_active then
            sfx("menu_back")
            state = STATE_MAIN
        end
    end
end

local function handle_confirm_input()
    if btnp(2) then  -- Left
        confirm.selected = confirm.selected - 1
        if confirm.selected < 1 then confirm.selected = #confirm.options end
        sfx("menu_move")
    elseif btnp(3) then  -- Right
        confirm.selected = confirm.selected + 1
        if confirm.selected > #confirm.options then confirm.selected = 1 end
        sfx("menu_move")
    elseif btnp(4) then  -- A - Confirm
        sfx("menu_select")
        if confirm.callback then
            confirm.callback(confirm.selected)
        end
        state = prev_state or STATE_MAIN
    elseif btnp(5) then  -- B - Cancel
        sfx("menu_back")
        state = prev_state or STATE_MAIN
    end
end

-- ═══════════════════════════════════════════════════════════════════════════════
-- Main Callbacks
-- ═══════════════════════════════════════════════════════════════════════════════

function _init()
    init_stars()
    scan_games()
    
    -- Load saved settings
    if _load_settings then
        local saved = _load_settings()
        if saved then
            for _, setting in ipairs(settings) do
                if saved[setting.id] ~= nil then
                    setting.value = saved[setting.id]
                end
            end
        end
    end
end

function _update()
    frame = frame + 1
    update_stars()
    
    -- Handle transitions
    if transition > 0 then
        transition = transition - 0.1
        if transition < 0 then transition = 0 end
        return
    end
    
    -- Input based on state
    if state == STATE_MAIN then
        handle_main_input()
    elseif state == STATE_GAMES then
        handle_games_input()
    elseif state == STATE_SETTINGS then
        handle_settings_input()
    elseif state == STATE_CREDITS then
        handle_credits_input()
    elseif state == STATE_USB then
        handle_usb_input()
    elseif state == STATE_CONFIRM then
        handle_confirm_input()
    end
end

function _draw()
    draw_background()
    
    -- Draw current screen
    if state == STATE_MAIN then
        draw_main_menu()
    elseif state == STATE_GAMES then
        draw_games_menu()
    elseif state == STATE_SETTINGS then
        draw_settings_menu()
    elseif state == STATE_CREDITS then
        draw_credits()
    elseif state == STATE_USB then
        draw_usb_mode()
    end
    
    -- Overlay confirm dialog
    if state == STATE_CONFIRM then
        draw_confirm_dialog()
    end
    
    -- Transition overlay
    if transition > 0 then
        local alpha = flr(transition * 16)
        for y = 0, SCREEN_H, 8 do
            for x = 0, SCREEN_W, 8 do
                if (x + y) % 16 < alpha then
                    rectfill(x, y, 8, 8, C.BLACK)
                end
            end
        end
    end
end
