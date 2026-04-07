-- Space Dodge
-- A complete example game for ThermoConsole
-- Demonstrates: states, spawning, collision, scoring, save/load, menus

-- ═══════════════════════════════════════════════════════════════════════════
-- GAME STATE
-- ═══════════════════════════════════════════════════════════════════════════

local state = "title"  -- "title", "playing", "gameover", "paused"

local player = {}
local asteroids = {}
local particles = {}
local stars = {}

local score = 0
local high_score = 0
local difficulty = 1
local spawn_timer = 0
local game_time = 0
local flash_timer = 0

-- ═══════════════════════════════════════════════════════════════════════════
-- INITIALIZATION
-- ═══════════════════════════════════════════════════════════════════════════

function _init()
    -- Load high score
    local save = load(0)
    if save and save.high_score then
        high_score = save.high_score
    end
    
    -- Create starfield
    stars = {}
    for i = 1, 100 do
        table.insert(stars, {
            x = rnd(480),
            y = rnd(640),
            speed = 0.5 + rnd(2),
            brightness = irnd(3) + 5
        })
    end
    
    reset_game()
end

function reset_game()
    player = {
        x = 240,
        y = 550,
        w = 24,
        h = 32,
        speed = 5,
        invincible = 0,
        lives = 3
    }
    
    asteroids = {}
    particles = {}
    score = 0
    difficulty = 1
    spawn_timer = 0
    game_time = 0
end

-- ═══════════════════════════════════════════════════════════════════════════
-- UPDATE
-- ═══════════════════════════════════════════════════════════════════════════

function _update()
    -- Update stars (always)
    for _, star in ipairs(stars) do
        star.y = star.y + star.speed
        if star.y > 640 then
            star.y = 0
            star.x = rnd(480)
        end
    end
    
    -- State machine
    if state == "title" then
        update_title()
    elseif state == "playing" then
        update_playing()
    elseif state == "gameover" then
        update_gameover()
    elseif state == "paused" then
        update_paused()
    end
    
    -- Update particles (always)
    update_particles()
end

function update_title()
    flash_timer = flash_timer + 1
    
    if btnp(4) or btnp(8) then  -- A or Start
        state = "playing"
        reset_game()
    end
end

function update_playing()
    game_time = game_time + dt()
    
    -- Increase difficulty over time
    difficulty = 1 + game_time / 30
    
    -- Pause
    if btnp(8) then  -- Start
        state = "paused"
        return
    end
    
    -- Player movement
    if btn(2) then player.x = player.x - player.speed end  -- left
    if btn(3) then player.x = player.x + player.speed end  -- right
    if btn(0) then player.y = player.y - player.speed * 0.5 end  -- up (slower)
    if btn(1) then player.y = player.y + player.speed * 0.5 end  -- down
    
    -- Clamp player position
    player.x = mid(player.w/2, player.x, 480 - player.w/2)
    player.y = mid(100, player.y, 620 - player.h/2)
    
    -- Update invincibility
    if player.invincible > 0 then
        player.invincible = player.invincible - dt()
    end
    
    -- Spawn asteroids
    spawn_timer = spawn_timer + dt()
    local spawn_rate = 0.8 / difficulty
    
    if spawn_timer > spawn_rate then
        spawn_timer = 0
        spawn_asteroid()
    end
    
    -- Update asteroids
    for i = #asteroids, 1, -1 do
        local a = asteroids[i]
        a.y = a.y + a.vy
        a.x = a.x + a.vx
        a.rot = a.rot + a.rot_speed
        
        -- Remove if off screen
        if a.y > 700 then
            table.remove(asteroids, i)
            score = score + 10
        -- Check collision with player
        elseif player.invincible <= 0 then
            if overlap(
                player.x - player.w/2, player.y - player.h/2, player.w, player.h,
                a.x - a.size/2, a.y - a.size/2, a.size, a.size
            ) then
                hit_player()
                table.remove(asteroids, i)
            end
        end
    end
end

function update_paused()
    if btnp(8) then  -- Start
        state = "playing"
    end
end

function update_gameover()
    flash_timer = flash_timer + 1
    
    if btnp(4) or btnp(8) then  -- A or Start
        state = "title"
    end
end

function update_particles()
    for i = #particles, 1, -1 do
        local p = particles[i]
        p.x = p.x + p.vx
        p.y = p.y + p.vy
        p.life = p.life - dt()
        p.vy = p.vy + 50 * dt()  -- gravity
        
        if p.life <= 0 then
            table.remove(particles, i)
        end
    end
end

-- ═══════════════════════════════════════════════════════════════════════════
-- GAME LOGIC
-- ═══════════════════════════════════════════════════════════════════════════

function spawn_asteroid()
    local size = 20 + rnd(30)
    table.insert(asteroids, {
        x = rnd(440) + 20,
        y = -size,
        size = size,
        vy = 2 + rnd(3) * difficulty,
        vx = rnd(2) - 1,
        rot = rnd(1),
        rot_speed = rnd(0.02) - 0.01,
        color = 4 + irnd(2)
    })
end

function hit_player()
    -- Spawn explosion particles
    for i = 1, 20 do
        local angle = rnd(1)
        local speed = 50 + rnd(100)
        table.insert(particles, {
            x = player.x,
            y = player.y,
            vx = cos(angle) * speed,
            vy = sin(angle) * speed,
            life = 0.5 + rnd(0.5),
            color = 8 + irnd(3)
        })
    end
    
    player.lives = player.lives - 1
    player.invincible = 2  -- 2 seconds of invincibility
    
    if player.lives <= 0 then
        game_over()
    end
end

function game_over()
    state = "gameover"
    flash_timer = 0
    
    -- Update high score
    if score > high_score then
        high_score = score
        save(0, { high_score = high_score })
    end
end

-- ═══════════════════════════════════════════════════════════════════════════
-- DRAWING
-- ═══════════════════════════════════════════════════════════════════════════

function _draw()
    cls(0)
    
    -- Draw starfield
    for _, star in ipairs(stars) do
        pset(flr(star.x), flr(star.y), star.brightness)
    end
    
    -- Draw particles
    for _, p in ipairs(particles) do
        local alpha = p.life * 2
        if alpha > 1 then alpha = 1 end
        circfill(flr(p.x), flr(p.y), flr(2 * alpha), p.color)
    end
    
    -- State-specific drawing
    if state == "title" then
        draw_title()
    elseif state == "playing" then
        draw_playing()
    elseif state == "gameover" then
        draw_playing()  -- Draw game state behind
        draw_gameover()
    elseif state == "paused" then
        draw_playing()
        draw_paused()
    end
end

function draw_title()
    -- Title
    local title = "SPACE DODGE"
    local title_y = 200
    
    for i = 1, #title do
        local char = title:sub(i, i)
        local x = 120 + (i - 1) * 24
        local y = title_y + sin(time() * 2 + i * 0.1) * 10
        local color = 8 + (i % 8)
        
        -- Shadow
        print(char, x + 2, y + 2, 1)
        -- Main
        print(char, x, y, color)
    end
    
    -- High score
    print("HIGH SCORE: " .. high_score, 160, 300, 10)
    
    -- Prompt
    if flr(flash_timer / 30) % 2 == 0 then
        print("PRESS A TO START", 152, 450, 7)
    end
    
    -- Controls
    print("CONTROLS:", 180, 540, 6)
    print("ARROWS - MOVE", 168, 560, 5)
    print("START  - PAUSE", 164, 580, 5)
    
    -- Version
    print("v1.0", 430, 620, 5)
end

function draw_playing()
    -- Draw asteroids
    for _, a in ipairs(asteroids) do
        draw_asteroid(a)
    end
    
    -- Draw player
    draw_player()
    
    -- Draw UI
    draw_ui()
end

function draw_player()
    local px = player.x
    local py = player.y
    
    -- Skip drawing every other frame when invincible (blink effect)
    if player.invincible > 0 and flr(time() * 10) % 2 == 0 then
        return
    end
    
    -- Ship body (triangle)
    local x1 = px
    local y1 = py - 16
    local x2 = px - 12
    local y2 = py + 12
    local x3 = px + 12
    local y3 = py + 12
    
    -- Fill with lines (simple triangle fill)
    for yy = y1, y3 do
        local t = (yy - y1) / (y3 - y1)
        local left = x1 + (x2 - x1) * t
        local right = x1 + (x3 - x1) * t
        line(left, yy, right, yy, 12)
    end
    
    -- Cockpit
    circfill(px, py - 4, 4, 7)
    circfill(px, py - 4, 2, 12)
    
    -- Engine flame
    if flr(time() * 20) % 2 == 0 then
        local flame_colors = {9, 10, 8}
        for i, c in ipairs(flame_colors) do
            local fy = py + 12 + i * 4
            local fw = 8 - i * 2
            rectfill(px - fw/2, fy, fw, 4, c)
        end
    end
end

function draw_asteroid(a)
    -- Simple rotating asteroid (octagon-ish)
    local cx, cy = a.x, a.y
    local r = a.size / 2
    
    -- Draw as filled circle with color
    circfill(cx, cy, r, a.color)
    
    -- Add some crater details
    local detail_r = r * 0.3
    local angle = a.rot
    circfill(
        cx + cos(angle) * r * 0.4,
        cy + sin(angle) * r * 0.4,
        detail_r,
        a.color - 1
    )
end

function draw_ui()
    -- Score
    rectfill(0, 0, 480, 30, 1)
    print("SCORE: " .. score, 10, 10, 7)
    
    -- Lives
    for i = 1, player.lives do
        local lx = 440 - (i - 1) * 20
        -- Mini ship icon
        line(lx, 8, lx - 6, 20, 12)
        line(lx, 8, lx + 6, 20, 12)
        line(lx - 6, 20, lx + 6, 20, 12)
    end
    
    -- Difficulty indicator
    local diff_text = "LEVEL " .. flr(difficulty)
    print(diff_text, 200, 10, 11)
end

function draw_gameover()
    -- Darken background
    rectfill(100, 250, 280, 180, 1)
    rect(100, 250, 280, 180, 7)
    
    -- Game Over text
    print("GAME OVER", 180, 280, 8)
    
    -- Final score
    print("SCORE: " .. score, 180, 320, 7)
    
    if score >= high_score and score > 0 then
        if flr(flash_timer / 15) % 2 == 0 then
            print("NEW HIGH SCORE!", 160, 350, 10)
        end
    else
        print("HIGH: " .. high_score, 185, 350, 6)
    end
    
    -- Prompt
    if flr(flash_timer / 30) % 2 == 0 then
        print("PRESS A TO CONTINUE", 140, 400, 7)
    end
end

function draw_paused()
    rectfill(150, 290, 180, 60, 1)
    rect(150, 290, 180, 60, 7)
    print("PAUSED", 200, 310, 7)
    print("PRESS START", 180, 330, 6)
end
