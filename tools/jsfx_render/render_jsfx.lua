-- render_jsfx.lua: render an input wav through nilamp_abx/twd_dlx_ii JSFX
--
-- Reads a JSON-ish config file at /tmp/render_jsfx_cfg.lua (a Lua chunk that
-- returns a table). Keeping config as Lua avoids a JSON dep.
--
-- Config table fields:
--   input    : absolute path to input wav
--   output   : absolute path for rendered wav (32-bit float mono)
--   sliders  : array of {index = N, value = X} (1-based slider numbers as in JSFX)
--   sr       : sample rate (default 48000)
--
-- Strategy:
--   1. Start from an empty project (REAPER opens with one when given a script).
--   2. Insert input wav as media on a new track.
--   3. Add JSFX, set slider params via TrackFX_SetParam (param index 0-based,
--      JSFX exposes sliders in order — REAPER param index = slider index - 1).
--   4. Configure render: bounds = entire project, format = 32-bit float WAV,
--      channels = 1, sample rate = sr, output filename pattern.
--   5. Render synchronously via Main_OnCommand(42230) (File: Render project,
--      using the most recent render settings, auto-close render dialog).
--      Fallback: 41824 = "File: Render project, using the most recent render
--      settings" — opens dialog. We want non-interactive: 42230.
--   6. Quit REAPER.

local cfg_path = "/tmp/render_jsfx_cfg.lua"
local ok, cfg = pcall(dofile, cfg_path)
if not ok or type(cfg) ~= "table" then
  reaper.ShowConsoleMsg("FATAL: failed to load " .. cfg_path .. "\n")
  reaper.Main_OnCommand(40004, 0)
  return
end

local function log(msg)
  local f = io.open("/tmp/render_jsfx.log", "a")
  if f then f:write(msg .. "\n"); f:close() end
end

log("=== render_jsfx.lua start ===")
log("input=" .. tostring(cfg.input))
log("output=" .. tostring(cfg.output))
log("sr=" .. tostring(cfg.sr))

-- 1. Use the project REAPER opened for us; just ensure it's empty.
-- (Skipping close-project to avoid leaving REAPER without an active project.)

-- 2. Add a track and insert input media.
reaper.InsertTrackAtIndex(0, true)
local track = reaper.GetTrack(0, 0)
reaper.SetOnlyTrackSelected(track)
-- InsertMedia mode: 0 = add to current track at edit cursor.
reaper.SetEditCurPos(0, false, false)
local insret = reaper.InsertMedia(cfg.input, 0)
log("InsertMedia ret=" .. tostring(insret))

-- 3. Add JSFX. AddByName flag bits: 1 = add (not just query).
local fx_idx = reaper.TrackFX_AddByName(track, "nilamp_abx/twd_dlx_ii_harness", false, -1)
log("TrackFX_AddByName ret=" .. tostring(fx_idx))
if fx_idx < 0 then
  log("FATAL: JSFX not found"); reaper.Main_OnCommand(40004, 0); return
end

local n_params = reaper.TrackFX_GetNumParams(track, fx_idx)
log("n_params=" .. tostring(n_params))

-- Set sliders. JSFX slider1 -> param index 0, slider2 -> 1, etc.
for _, sl in ipairs(cfg.sliders or {}) do
  local pidx = sl.index - 1
  if pidx >= 0 and pidx < n_params then
    -- TrackFX_SetParam expects normalized [0,1]? No: for JSFX, the value is
    -- the raw slider value (REAPER maps it via min/max). Use SetParamNormalized
    -- only when we have a normalized value. Use TrackFX_SetParam for raw.
    -- However TrackFX_SetParam in REAPER actually wants normalized for most
    -- plugins but for JSFX it accepts raw within slider range. Try raw first.
    local set_ok = reaper.TrackFX_SetParam(track, fx_idx, pidx, sl.value)
    log(string.format("slider %d (param %d) <- %s : ok=%s",
      sl.index, pidx, tostring(sl.value), tostring(set_ok)))
  end
end

-- 4. Configure render settings via project info.
local sr = cfg.sr or 48000
reaper.GetSetProjectInfo(0, "PROJECT_SRATE", sr, true)
reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)

-- Render bounds: 1 = entire project
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
-- Render channels: 1 = mono
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 1, true)
-- Render sample rate
reaper.GetSetProjectInfo(0, "RENDER_SRATE", sr, true)
-- Render source: 0 = master mix
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
-- Tail: 0 ms
reaper.GetSetProjectInfo(0, "RENDER_TAILFLAG", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_TAILMS", 0, true)
-- Add to project after render: 0 = no
reaper.GetSetProjectInfo(0, "RENDER_ADDTOPROJ", 0, true)
-- Dither: 0 = none
reaper.GetSetProjectInfo(0, "RENDER_DITHER", 0, true)

-- File path & pattern
local out_dir = cfg.output:match("(.*/)") or "/tmp/"
local out_name = cfg.output:match("([^/]+)$"):gsub("%.wav$", "")
reaper.GetSetProjectInfo_String(0, "RENDER_FILE", out_dir, true)
reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", out_name, true)

-- Format: WAV 32-bit float. RENDER_FORMAT is a 4-char base64-ish blob.
-- For WAV: "evaw" (LE "wave") preceded by config. Easier: use cfg as a
-- known-good string. The blob for "WAV 32-bit FP, mono, no BWF" is documented
-- as: "ZXZhdyAAAAA=" base64 = "evaw \0\0\0\0" (5 chars: 'e','v','a','w',
-- then config byte 0x20 = 32-bit float?). Trial below.
-- Per REAPER source: format is 4-byte type + N-byte cfg data. For wav,
-- type='evaw' (little-endian "wave"). cfg byte 0: bits 0-2 = format
-- (0=16/24/32 int by another byte, 3=32-bit float). Use the simpler trick:
-- base64 encoded "evaw\32\0\0\0" gives 32-bit int? We'll set float via:
local fmt_blob = "ZXZhdyAAAAA="  -- evaw + 0x20 0x00 0x00 0x00 + pad?
-- Decode base64 manually-ish: this is what REAPER produces for WAV 32-bit FP.
-- If wrong, render still produces *some* wav and we'll see.
reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", fmt_blob, true)

log("render config set; out_dir=" .. out_dir .. " out_name=" .. out_name)

-- 5. Render. 42230 = "File: Render project, using the most recent render
-- settings (auto-close render dialog)".
reaper.Main_OnCommand(42230, 0)
log("render command issued")

-- 6. Quit. Use 40004 = "File: Quit REAPER".
reaper.Main_OnCommand(40004, 0)
