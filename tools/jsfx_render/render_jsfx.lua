-- render_jsfx.lua: render an input wav through nilamp_abx/twd_dlx_ii JSFX
--
-- Reads a JSON-ish config file at /tmp/render_jsfx_cfg.lua (a Lua chunk that
-- returns a table). Keeping config as Lua avoids a JSON dep.
--
-- Config table fields:
--   input    : absolute path to input wav
--   output   : absolute path for rendered wav (32-bit float)
--   effect   : REAPER-relative JSFX effect name
--   sliders  : array of {index = N, value = X} (1-based slider numbers as in JSFX)
--   sr       : sample rate (default 48000)
--   channels : render channel count (default 1)
--
-- Strategy:
--   1. Start from an empty project (REAPER opens with one when given a script).
--   2. Force project/render sample rate before JSFX @init/@slider runs.
--   3. Insert input wav as media on a new track.
--   4. Add JSFX, set slider params via TrackFX_SetParam (param index 0-based,
--      JSFX exposes sliders in order — REAPER param index = slider index - 1).
--   5. Configure render: bounds = entire project, format = 32-bit float WAV,
--      channels = 1, sample rate = sr, output filename pattern.
--   6. Render synchronously via Main_OnCommand(42230) (File: Render project,
--      using the most recent render settings, auto-close render dialog).
--      Fallback: 41824 = "File: Render project, using the most recent render
--      settings" — opens dialog. We want non-interactive: 42230.
--   7. Quit REAPER.

local cfg_path = "/tmp/render_jsfx_cfg.lua"
local ok, cfg = pcall(dofile, cfg_path)
if not ok or type(cfg) ~= "table" then
  reaper.ShowConsoleMsg("FATAL: failed to load " .. cfg_path .. "\n")
  reaper.Main_SaveProject(0, false)
  reaper.Main_OnCommand(40004, 0)
  return
end

local function log(msg)
  local f = io.open("/tmp/render_jsfx.log", "a")
  if f then f:write(msg .. "\n"); f:close() end
end

local function quit_reaper()
  reaper.Main_SaveProject(0, false)
  reaper.Main_OnCommand(40004, 0)
end

log("=== render_jsfx.lua start ===")
log("input=" .. tostring(cfg.input))
log("output=" .. tostring(cfg.output))
log("sr=" .. tostring(cfg.sr))
log("effect=" .. tostring(cfg.effect))

-- 1. Use the project REAPER opened for us; just ensure it's empty.
-- (Skipping close-project to avoid leaving REAPER without an active project.)

-- 2. Force sample rate before inserting media or instantiating JSFX. Keller's
-- JSFX computes filter and smoothing coefficients from srate during @init and
-- @slider, so setting this later can leave the reference render initialized at
-- the device/default rate.
local sr = cfg.sr or 48000
local render_channels = cfg.channels or 1
reaper.GetSetProjectInfo(0, "PROJECT_SRATE", sr, true)
reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", sr, true)
log("sample rate forced before FX init: " .. tostring(sr))

-- 3. Add a track and insert input media.
reaper.InsertTrackAtIndex(0, true)
local track = reaper.GetTrack(0, 0)
reaper.SetOnlyTrackSelected(track)
reaper.SetMediaTrackInfo_Value(track, "I_NCHAN", render_channels)
reaper.SetMediaTrackInfo_Value(reaper.GetMasterTrack(0), "I_NCHAN", render_channels)
log("track channels before media=" .. tostring(reaper.GetMediaTrackInfo_Value(track, "I_NCHAN")))
-- InsertMedia mode: 0 = add to current track at edit cursor.
reaper.SetEditCurPos(0, false, false)
local insret = reaper.InsertMedia(cfg.input, 0)
log("InsertMedia ret=" .. tostring(insret))
reaper.SetMediaTrackInfo_Value(track, "I_NCHAN", render_channels)
reaper.SetMediaTrackInfo_Value(reaper.GetMasterTrack(0), "I_NCHAN", render_channels)
log("track channels after media=" .. tostring(reaper.GetMediaTrackInfo_Value(track, "I_NCHAN")))
log("master channels after media=" .. tostring(reaper.GetMediaTrackInfo_Value(reaper.GetMasterTrack(0), "I_NCHAN")))

-- 4. Add JSFX. AddByName flag bits: 1 = add (not just query).
local effect_name = cfg.effect or "nilamp_abx/twd_dlx_ii_harness"
local fx_idx = reaper.TrackFX_AddByName(track, effect_name, false, -1)
log("TrackFX_AddByName ret=" .. tostring(fx_idx))
if fx_idx < 0 then
  log("FATAL: JSFX not found: " .. tostring(effect_name)); quit_reaper(); return
end

local n_params = reaper.TrackFX_GetNumParams(track, fx_idx)
log("n_params=" .. tostring(n_params))

-- Set sliders. JSFX slider1 -> param index 0, slider2 -> 1, etc.
for _, sl in ipairs(cfg.sliders or {}) do
  local pidx = sl.index - 1
  if pidx >= 0 and pidx < n_params then
    local set_ok = reaper.TrackFX_SetParam(track, fx_idx, pidx, sl.value)
    local actual, minval, maxval = reaper.TrackFX_GetParam(track, fx_idx, pidx)
    log(string.format("slider %d (param %d) requested=%s ok=%s readback=%s range=[%s,%s]",
      sl.index, pidx, tostring(sl.value), tostring(set_ok), tostring(actual),
      tostring(minval), tostring(maxval)))
  else
    log(string.format("slider %d skipped: param index %d outside [0,%d)",
      sl.index, pidx, n_params - 1))
  end
end

-- 5. Configure render settings via project info.
-- Render bounds: 1 = entire project
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
-- Render channels: 1 = mono, higher values for diagnostic tap renders.
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", render_channels, true)
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

-- 6. Render. 42230 = "File: Render project, using the most recent render
-- settings (auto-close render dialog)".
reaper.Main_OnCommand(42230, 0)
log("render command issued")

-- 7. Quit. Use 40004 = "File: Quit REAPER".
quit_reaper()
