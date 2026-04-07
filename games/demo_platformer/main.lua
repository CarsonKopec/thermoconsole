-- Demo Platformer
-- A ThermoConsole platformer

local player = {}
local gravity = 0.5
local jump_force = -10

function _init()
    player = {
        x = 240,
        y = 500,
        vx = 0,
        vy = 0,
        w = 16,
        h = 24,
        grounded = false,
        facing = 1  -- 1 = right, -1 = left
    }
end

function _update()
    -- Horizontal movement
    player.vx = 0
    if btn(2) then
        player.vx = -4
        player.facing = -1
    end
    if btn(3) then
        player.vx = 4
        player.facing = 1
    end
    
    -- Jump
    if btnp(4) and player.grounded then
        player.vy = jump_force
        player.grounded = false
        -- sfx("jump")
    end
    
    -- Apply gravity
    player.vy = player.vy + gravity
    if player.vy > 12 then
        player.vy = 12  -- terminal velocity
    end
    
    -- Apply velocity
    player.x = player.x + player.vx
    player.y = player.y + player.vy
    
    -- Floor collision (simple)
    local floor_y = 580
    if player.y + player.h > floor_y then
        player.y = floor_y - player.h
        player.vy = 0
        player.grounded = true
    end
    
    -- Screen bounds
    if player.x < 0 then player.x = 0 end
    if player.x > 480 - player.w then player.x = 480 - player.w end
end

function _draw()
    cls(1)  -- dark blue background
    
    -- Draw ground
    rectfill(0, 580, 480, 60, 3)
    
    -- Draw player (rectangle for now, replace with sprite)
    local px = player.x
    local py = player.y
    rectfill(px, py, player.w, player.h, 11)
    
    -- Eyes (show facing direction)
    local eye_x = px + (player.facing == 1 and 10 or 2)
    rectfill(eye_x, py + 4, 4, 4, 7)
    
    -- UI
    print("Demo Platformer", 10, 10, 7)
    print("Arrows: Move   Z: Jump", 10, 620, 6)
end
