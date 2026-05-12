-- SPDX-License-Identifier: MIT
--
-- REAPER host-visible performance scenario driver for nilamp.
--
-- Usage:
--   1. Open a REAPER project with nilamp CLAP and VST3 instances.
--   2. Start one of the tools/reaper_perf/measure_reaper_*.py samplers.
--   3. Run this script from REAPER's Action List.
--
-- The sampler timestamps marker arrival and produces the final
-- report. This script only controls repeatable host state.

local function default_temp_path(name)
  local temp = os.getenv("TMPDIR") or os.getenv("TEMP") or os.getenv("TMP") or "."
  local last = string.sub(temp, -1)
  local sep = (last == "/" or last == "\\") and "" or "/"
  return temp .. sep .. name
end

local config = {}
local config_path = os.getenv("NILAMP_REAPER_PERF_CONFIG")
if config_path == nil or config_path == "" then
  config_path = default_temp_path("nilamp_reaper_perf_config.lua")
end
local config_chunk = loadfile(config_path)
if config_chunk then
  local ok, loaded = pcall(config_chunk)
  if ok and type(loaded) == "table" then
    config = loaded
  end
end

local marker_path = config.marker_path or os.getenv("NILAMP_REAPER_PERF_MARKERS")
if marker_path == nil or marker_path == "" then
  marker_path = default_temp_path("nilamp_reaper_perf_markers.jsonl")
end

local scenario_seconds = tonumber(config.scenario_seconds or os.getenv("NILAMP_REAPER_PERF_SECONDS") or "") or 30.0
local settle_seconds = tonumber(config.settle_seconds or os.getenv("NILAMP_REAPER_PERF_SETTLE_SECONDS") or "") or 3.0
local repeats = tonumber(config.repeats or os.getenv("NILAMP_REAPER_PERF_REPEATS") or "") or 3
local selected_surfaces = config.surfaces or os.getenv("NILAMP_REAPER_PERF_SURFACES") or
                          "nilamp_clap,nilamp_vst3,keller_ysfx"

local all_surfaces = {
  {
    key = "nilamp_clap",
    track_hints = {"nilamp clap", "clap", "nilamp"},
    fx_hints = {"clap: nilamp", "nilamp twd mkii"},
    fx_requires = {"clap"},
    fx_excludes = {"vst3"},
  },
  {
    key = "nilamp_vst3",
    track_hints = {"nilamp vst3", "vst3", "nilamp"},
    fx_hints = {"vst3: nilamp", "nilamp twd mkii"},
    fx_requires = {"vst3"},
    fx_excludes = {"clap:"},
  },
  {
    key = "keller_ysfx",
    track_hints = {"keller", "ysfx", "hk", "twd"},
    fx_hints = {"keller", "ysfx", "hk/twd", "hk twd", "twd dlx", "twd"},
    fx_excludes = {"nilamp"},
  },
}

local surfaces = {}
for key in string.gmatch(selected_surfaces, "([^,]+)") do
  key = string.gsub(key, "^%s+", "")
  key = string.gsub(key, "%s+$", "")
  for _, spec in ipairs(all_surfaces) do
    if spec.key == key then
      surfaces[#surfaces + 1] = spec
    end
  end
end
if #surfaces == 0 then
  reaper.ShowMessageBox(
    "No valid NILAMP_REAPER_PERF_SURFACES selected: " .. selected_surfaces,
    "nilamp REAPER perf",
    0
  )
  return
end

local candidates = {}

local function lower(s)
  return string.lower(s or "")
end

local function contains_any(text, hints)
  local haystack = lower(text)
  for _, hint in ipairs(hints) do
    if string.find(haystack, lower(hint), 1, true) then
      return true
    end
  end
  return false
end

local function contains_none(text, hints)
  return not contains_any(text, hints)
end

local function contains_all(text, hints)
  if hints == nil then
    return true
  end
  local haystack = lower(text)
  for _, hint in ipairs(hints) do
    if not string.find(haystack, lower(hint), 1, true) then
      return false
    end
  end
  return true
end

local function json_escape(value)
  value = tostring(value or "")
  value = string.gsub(value, "\\", "\\\\")
  value = string.gsub(value, "\"", "\\\"")
  value = string.gsub(value, "\n", "\\n")
  value = string.gsub(value, "\r", "\\r")
  value = string.gsub(value, "\t", "\\t")
  return value
end

local function write_marker(fields)
  local file = io.open(marker_path, "a")
  if not file then
    reaper.ShowMessageBox("Could not open marker file:\n" .. marker_path, "nilamp REAPER perf", 0)
    return false
  end

  local parts = {}
  for key, value in pairs(fields) do
    if type(value) == "number" then
      parts[#parts + 1] = string.format("\"%s\":%.9f", json_escape(key), value)
    else
      parts[#parts + 1] = string.format("\"%s\":\"%s\"", json_escape(key), json_escape(value))
    end
  end
  file:write("{" .. table.concat(parts, ",") .. "}\n")
  file:close()
  return true
end

local function track_name(track)
  local ok, name = reaper.GetTrackName(track)
  if ok then
    return name
  end
  return ""
end

local function fx_name(track, fx_index)
  local ok, name = reaper.TrackFX_GetFXName(track, fx_index)
  if ok then
    return name
  end
  return ""
end

local function find_surface(spec)
  local track_count = reaper.CountTracks(0)

  for i = 0, track_count - 1 do
    local track = reaper.GetTrack(0, i)
    if contains_any(track_name(track), spec.track_hints) then
      local fx_count = reaper.TrackFX_GetCount(track)
      for fx = 0, fx_count - 1 do
        local name = fx_name(track, fx)
        if contains_any(name, spec.fx_hints) and
            contains_all(name, spec.fx_requires) and
            contains_none(name, spec.fx_excludes) then
          return track, fx, name
        end
      end
    end
  end

  for i = 0, track_count - 1 do
    local track = reaper.GetTrack(0, i)
    local fx_count = reaper.TrackFX_GetCount(track)
    for fx = 0, fx_count - 1 do
      local name = fx_name(track, fx)
      if contains_any(name, spec.fx_hints) and
          contains_all(name, spec.fx_requires) and
          contains_none(name, spec.fx_excludes) then
        return track, fx, name
      end
    end
  end

  return nil, -1, ""
end

local function describe_project_fx()
  local lines = {}
  local track_count = reaper.CountTracks(0)
  lines[#lines + 1] = "Scanned tracks/FX:"
  for i = 0, track_count - 1 do
    local track = reaper.GetTrack(0, i)
    lines[#lines + 1] = string.format("Track %d name=\"%s\"", i + 1, track_name(track))
    local fx_count = reaper.TrackFX_GetCount(track)
    for fx = 0, fx_count - 1 do
      lines[#lines + 1] = string.format("  FX %d name=\"%s\"", fx + 1, fx_name(track, fx))
    end
  end
  return table.concat(lines, "\n")
end

local found = {}
for _, spec in ipairs(surfaces) do
  local track, fx, name = find_surface(spec)
  if track == nil then
    reaper.ShowMessageBox(
      "Could not find track/FX for " .. spec.key ..
      ". Rename the comparison tracks or adjust hints at the top of the script.\n\n" ..
      describe_project_fx(),
      "nilamp REAPER perf",
      0
    )
    return
  end
  found[spec.key] = {
    track = track,
    fx = fx,
    fx_name = name,
  }
  candidates[#candidates + 1] = found[spec.key]
end

local scenarios = {}
for run = 1, repeats do
  for _, spec in ipairs(surfaces) do
    scenarios[#scenarios + 1] = {
      scenario = spec.key .. "_editor_closed",
      surface = spec.key,
      editor = "closed",
      run = run,
    }
    scenarios[#scenarios + 1] = {
      scenario = spec.key .. "_editor_open",
      surface = spec.key,
      editor = "open",
      run = run,
    }
  end
end

local original_state = {}
for _, item in ipairs(candidates) do
  original_state[#original_state + 1] = {
    track = item.track,
    fx = item.fx,
    enabled = reaper.TrackFX_GetEnabled(item.track, item.fx),
  }
end

local original_cursor = reaper.GetCursorPosition()
local original_play_state = reaper.GetPlayState()
local current = 0
local phase = "prepare"
local phase_started = reaper.time_precise()

local function close_all_candidate_fx()
  for _, item in ipairs(candidates) do
    reaper.TrackFX_Show(item.track, item.fx, 2)
  end
end

local function set_active_surface(surface)
  for _, spec in ipairs(surfaces) do
    local item = found[spec.key]
    reaper.TrackFX_SetEnabled(item.track, item.fx, spec.key == surface)
  end
end

local function start_transport()
  reaper.OnStopButton()
  reaper.SetEditCurPos(0.0, false, false)
  reaper.OnPlayButton()
end

local function finish()
  write_marker({
    event = "done",
    scenario = "all",
    run = 0,
    time_precise = reaper.time_precise(),
  })

  reaper.OnStopButton()
  reaper.SetEditCurPos(original_cursor, false, false)
  close_all_candidate_fx()
  for _, item in ipairs(original_state) do
    reaper.TrackFX_SetEnabled(item.track, item.fx, item.enabled)
  end
  if (original_play_state % 2) == 1 then
    reaper.OnPlayButton()
  end
  reaper.ShowConsoleMsg("nilamp REAPER perf scenarios complete. Markers: " .. marker_path .. "\n")
end

local function step()
  local now = reaper.time_precise()

  if current >= #scenarios and phase == "prepare" then
    finish()
    return
  end

  local scenario = scenarios[current + 1]

  if phase == "prepare" then
    close_all_candidate_fx()
    set_active_surface(scenario.surface)
    if scenario.editor == "open" then
      local item = found[scenario.surface]
      reaper.TrackFX_Show(item.track, item.fx, 3)
    end
    start_transport()
    phase = "settle"
    phase_started = now
    reaper.defer(step)
    return
  end

  if phase == "settle" and now - phase_started >= settle_seconds then
    write_marker({
      event = "start",
      scenario = scenario.scenario,
      surface = scenario.surface,
      editor = scenario.editor,
      run = scenario.run,
      fx_name = found[scenario.surface].fx_name,
      time_precise = now,
    })
    phase = "measure"
    phase_started = now
    reaper.defer(step)
    return
  end

  if phase == "measure" and now - phase_started >= scenario_seconds then
    write_marker({
      event = "end",
      scenario = scenario.scenario,
      surface = scenario.surface,
      editor = scenario.editor,
      run = scenario.run,
      fx_name = found[scenario.surface].fx_name,
      time_precise = now,
    })
    reaper.OnStopButton()
    close_all_candidate_fx()
    current = current + 1
    phase = "prepare"
    phase_started = now
    reaper.defer(step)
    return
  end

  reaper.defer(step)
end

write_marker({
  event = "session_start",
  scenario = "all",
  run = 0,
  scenario_seconds = scenario_seconds,
  settle_seconds = settle_seconds,
  repeats = repeats,
  time_precise = reaper.time_precise(),
})
reaper.ShowConsoleMsg("nilamp REAPER perf scenario markers: " .. marker_path .. "\n")
step()
