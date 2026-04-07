-- Hello ThermoConsole
-- A simple demo game showing basic API usage

-- Player state
local player = {}

-- Animation
local frame = 0
local colors = {8, 9, 10, 11, 12, 14}  -- rainbow colors

function _init()
    -- Initialize player in center of screen
    player = {
        x = 240,
        y = 320,
        size = 20,
        speed = 4,
        trail = {}
    }
end

function _update()
    frame = frame + 1
    
    -- Store trail positions
    if frame % 3 == 0 then
        table.insert(player.trail, 1, {x = player.x, y = player.y})
        if #player.trail > 10 then
            table.remove(player.trail)
        end
    end
    
    -- Movement with d-pad
    if btn(0) then player.y = player.y - player.speed end  -- up
    if btn(1) then player.y = player.y + player.speed end  -- down
    if btn(2) then player.x = player.x - player.speed end  -- left
    if btn(3) then player.x = player.x + player.speed end  -- right
    
    -- Boost with A button
    if btn(4) then
        player.speed = 8
    else
        player.speed = 4
    end
    
    -- Grow/shrink with X/Y
    if btnp(6) then player.size = min(player.size + 4, 60) end
    if btnp(7) then player.size = max(player.size - 4, 8) end
    
    -- Keep player on screen
    player.x = mid(player.size, player.x, 480 - player.size)
    player.y = mid(player.size + 80, player.y, 640 - player.size - 40)
end

function _draw()
    -- Background gradient (simple version)
    for i = 0, 15 do
        local shade = flr(i / 4)
        rectfill(0, i * 40, 480, 40, shade)
    end
    
    -- Draw trail
    for i, pos in ipairs(player.trail) do
        local alpha = 1 - (i / #player.trail)
        local size = player.size * alpha
        local color_idx = ((flr(frame / 5) + i) % #colors) + 1
        circfill(pos.x, pos.y, size, colors[color_idx])
    end
    
    -- Draw player (pulsing circle)
    local pulse = sin(time() * 3) * 4
    local main_color = colors[(flr(frame / 10) % #colors) + 1]
    circfill(player.x, player.y, player.size + pulse, main_color)
    circfill(player.x, player.y, player.size * 0.6, 7)  -- white center
    
    -- Title
    local title = "THERMOCONSOLE"
    local title_x = 240 - (#title * 4)
    for i = 1, #title do
        local c = title:sub(i, i)
        local char_color = colors[((i + flr(frame / 8)) % #colors) + 1]
        local y_offset = sin(time() * 4 + i * 0.3) * 5
        print(c, title_x + (i - 1) * 8, 30 + y_offset, char_color)
    end
    
    -- Subtitle
    print("Hello World Demo", 168, 55, 6)
    
    -- Instructions box
    rectfill(20, 560, 440, 70, 1)
    rect(20, 560, 440, 70, 6)
    
    print("CONTROLS:", 35, 572, 7)
    print("D-Pad: Move    A: Boost", 35, 590, 6)
    print("X: Grow        Y: Shrink", 35, 605, 6)
    
    -- Stats
    print("Size: " .. player.size, 320, 572, 10)
    print("FPS: " .. fps(), 320, 590, 11)
    print("Time: " .. flr(time()), 320, 605, 12)
end
