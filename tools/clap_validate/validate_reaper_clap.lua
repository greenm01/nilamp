-- Validate nilamp's native CLAP shell through REAPER.
--
-- Config path: /tmp/nilamp_clap_validate_cfg.lua

local cfg_path = "/tmp/nilamp_clap_validate_cfg.lua"
local ok, cfg = pcall(dofile, cfg_path)
if not ok or type(cfg) ~= "table" then
  reaper.ShowConsoleMsg("FATAL: failed to load " .. cfg_path .. "\n")
  reaper.Main_SaveProject(0, false)
  reaper.Main_OnCommand(40004, 0)
  return
end

local log_path = cfg.log or "/tmp/nilamp_clap_validate.log"

local function log(msg)
  local f = io.open(log_path, "a")
  if f then f:write(msg .. "\n"); f:close() end
end

local function quit_reaper()
  reaper.Main_SaveProject(0, false)
  reaper.Main_OnCommand(40004, 0)
end

local function fatal(msg)
  log("FATAL: " .. msg)
  quit_reaper()
end

log("=== validate_reaper_clap.lua start ===")
log("input=" .. tostring(cfg.input))
log("output=" .. tostring(cfg.output))
log("sr=" .. tostring(cfg.sr))

local sr = cfg.sr or 48000
reaper.GetSetProjectInfo(0, "PROJECT_SRATE", sr, true)
reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", sr, true)
log("sample rate forced before FX init: " .. tostring(sr))

reaper.InsertTrackAtIndex(0, true)
local track = reaper.GetTrack(0, 0)
if not track then
  fatal("failed to create track")
  return
end

reaper.SetOnlyTrackSelected(track)
reaper.SetEditCurPos(0, false, false)
local insret = reaper.InsertMedia(cfg.input, 0)
log("InsertMedia ret=" .. tostring(insret))

local fx_idx = -1
local fx_name = nil
for _, candidate in ipairs(cfg.plugin_candidates or {}) do
  local idx = reaper.TrackFX_AddByName(track, candidate, false, -1)
  log("TrackFX_AddByName candidate=" .. tostring(candidate) .. " ret=" .. tostring(idx))
  if idx >= 0 then
    fx_idx = idx
    fx_name = candidate
    break
  end
end

if fx_idx < 0 then
  fatal("nilamp CLAP plugin not found")
  return
end
log("selected_fx=" .. tostring(fx_name))

local _, resolved_name = reaper.TrackFX_GetFXName(track, fx_idx, "")
log("resolved_fx_name=" .. tostring(resolved_name))

local n_params = reaper.TrackFX_GetNumParams(track, fx_idx)
log("n_params=" .. tostring(n_params))

local expected = cfg.expected_params or {}
if n_params < #expected then
  fatal("expected at least " .. tostring(#expected) .. " params, saw " .. tostring(n_params))
  return
end

for i, name in ipairs(expected) do
  local pidx = i - 1
  local _, actual = reaper.TrackFX_GetParamName(track, fx_idx, pidx, "")
  log(string.format("param %d name=%s expected=%s", pidx, tostring(actual), tostring(name)))
  if actual ~= name then
    fatal(string.format("param %d expected '%s', saw '%s'", pidx, tostring(name), tostring(actual)))
    return
  end
end

for _, param in ipairs(cfg.params or {}) do
  local pidx = param.index
  local value = param.value
  local ok_set = reaper.TrackFX_SetParam(track, fx_idx, pidx, value)
  local normalized = reaper.TrackFX_GetParamNormalized(track, fx_idx, pidx)
  local plain, minval, maxval = reaper.TrackFX_GetParam(track, fx_idx, pidx)
  local _, formatted = reaper.TrackFX_GetFormattedParamValue(track, fx_idx, pidx, "")
  log(string.format(
    "param_set index=%d requested=%s ok=%s readback_norm=%s plain=%s range=[%s,%s] formatted=%s",
    pidx, tostring(value), tostring(ok_set), tostring(normalized), tostring(plain),
    tostring(minval), tostring(maxval), tostring(formatted)))
  if not ok_set then
    fatal(string.format("param %d set call failed", pidx))
    return
  end
  if math.abs(plain - value) > 0.0001 then
    log(string.format("param %d immediate readback deferred", pidx))
  end
end

-- Render bounds: entire project; source: master mix; channels: stereo.
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", sr, true)
reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_TAILFLAG", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_TAILMS", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_ADDTOPROJ", 0, true)
reaper.GetSetProjectInfo(0, "RENDER_DITHER", 0, true)

local out_dir = cfg.output:match("(.*/)") or "/tmp/"
local out_name = cfg.output:match("([^/]+)$"):gsub("%.wav$", "")
reaper.GetSetProjectInfo_String(0, "RENDER_FILE", out_dir, true)
reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", out_name, true)
reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "ZXZhdyAAAAA=", true)
log("render config set; out_dir=" .. out_dir .. " out_name=" .. out_name)

reaper.Main_OnCommand(42230, 0)
log("render command issued")
quit_reaper()
