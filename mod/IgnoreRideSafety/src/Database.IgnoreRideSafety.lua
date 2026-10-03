-----------------------------------------------------------------------
-- Ignore Ride Safety: loads the native helper, defines the options,
-- adds them to Options > Game, and registers the park manager with ACSE.
-----------------------------------------------------------------------
local global = _G
local api = global.api
local package = global.package
local string = global.string
local type = global.type
local tostring = global.tostring
local pcall = global.pcall
local ipairs = global.ipairs
local IgnoreRideSafety = module(...)

IgnoreRideSafety.sVersion = "0.2.0-exp.3 EXPERIMENTAL"

-- Status codes returned by the native helper (see native/irs_patch.c)
IgnoreRideSafety.ST_ON = 1
IgnoreRideSafety.ST_OFF = 2
IgnoreRideSafety.ST_UNSUPPORTED = 3
IgnoreRideSafety.ST_PROTECT_FAIL = 4
IgnoreRideSafety.ST_RACE = 5

IgnoreRideSafety.tNative = nil
IgnoreRideSafety.sLoadError = nil

-- The two independent options and their native entry points.
IgnoreRideSafety.tOptions = {
  fear = {
    sEnable = "irs_enable", sDisable = "irs_disable", sStatus = "irs_status",
    sID = "game.ignoreridesafety",
    sLabel = "Ignore ride safety concerns",
    sToolTip = "When ticked, guests no longer refuse rides for being too intense or scary. Price, queues, needs, nausea and not-intense-enough still apply. Resets to off when a park is loaded."
  },
  nausea = {
    sEnable = "irs_nausea_enable", sDisable = "irs_nausea_disable", sStatus = "irs_nausea_status",
    sID = "game.ignoreridenausea",
    sLabel = "Ignore ride nausea",
    sToolTip = "When ticked, the nausea rating of a ride no longer puts guests off it. Guests who already feel sick still avoid rides and can still vomit afterwards. Resets to off when a park is loaded."
  }
}
IgnoreRideSafety.tOptionOrder = {"fear", "nausea"}

-- EXPERIMENTAL option (off by default, never part of tOptionOrder so the stable options never depend on it).
IgnoreRideSafety.tExperimental = {
  sEnable = "irs_exp_enable", sDisable = "irs_exp_disable", sStatus = "irs_exp_status",
  sID = "game.irsexperimentalopen",
  sLabel = "EXPERIMENTAL: allow opening untested or unfinished rides",
  sToolTip = "Experimental. Lets untested or unfinished rides be opened; while such a ride is open, guests treat it as Excitement 8, Fear 8, Nausea 4. The ride may still close after a crash. Untick to restore normal behaviour. Resets to off when a park is loaded."
}
IgnoreRideSafety.tExperimentalChannel = {"irs_exp_begin", "irs_exp_bit0", "irs_exp_bit1", "irs_exp_push", "irs_exp_commit", "irs_exp_report"}

local function GetDLLPath()
  -- package.cpath begins with "<game dir>\?.dll"
  local sFirst = string.match(package.cpath or "", "^([^;]+)")
  if sFirst == nil then
    return nil
  end
  local sGameDir = string.match(sFirst, "^(.*[\\/])%?%.dll$")
  if sGameDir == nil then
    return nil
  end
  return sGameDir .. "Win64\\ovldata\\IgnoreRideSafety\\IgnoreRideSafety.dll"
end

-- Calls a native function. Natives return k, which makes Lua return the top k arguments,
-- so the first returned value is the status code.
local function Call(fn)
  local bOk, nStatus = pcall(fn, 6, 5, 4, 3, 2, 1)
  if bOk and type(nStatus) == "number" then
    return nStatus
  end
  return nil
end
IgnoreRideSafety.Call = Call

function IgnoreRideSafety.LoadNative()
  if IgnoreRideSafety.tNative ~= nil then
    return IgnoreRideSafety.tNative
  end
  if type(package) ~= "table" or type(package.loadlib) ~= "function" then
    IgnoreRideSafety.sLoadError = "the game does not allow loading the helper"
    return nil
  end
  local sPath = GetDLLPath()
  if sPath == nil then
    IgnoreRideSafety.sLoadError = "could not determine the game folder"
    return nil
  end
  local tNative = {}
  for _, sKind in ipairs(IgnoreRideSafety.tOptionOrder) do
    local tOpt = IgnoreRideSafety.tOptions[sKind]
    for _, sName in ipairs({tOpt.sEnable, tOpt.sDisable, tOpt.sStatus}) do
      local fn, sErr = package.loadlib(sPath, sName)
      if fn == nil then
        IgnoreRideSafety.sLoadError = "IgnoreRideSafety.dll missing or not loadable (" .. tostring(sErr) .. ")"
        return nil
      end
      tNative[sName] = fn
    end
  end
  -- experimental entry points are optional: the stable options work without them
  local bExp = true
  local tExp = IgnoreRideSafety.tExperimental
  local tNames = {tExp.sEnable, tExp.sDisable, tExp.sStatus}
  for _, sName in ipairs(IgnoreRideSafety.tExperimentalChannel) do
    tNames[#tNames + 1] = sName
  end
  for _, sName in ipairs(tNames) do
    local fn = package.loadlib(sPath, sName)
    if fn == nil then
      bExp = false
    else
      tNative[sName] = fn
    end
  end
  tNative.bExperimental = bExp
  IgnoreRideSafety.tNative = tNative
  return tNative
end

-- The park manager registers itself here while a park is loaded.
IgnoreRideSafety.oManager = nil

function IgnoreRideSafety.IsAvailable()
  local m = IgnoreRideSafety.oManager
  return m ~= nil and m.tNative ~= nil and m.tStatus.fear ~= nil and m.tStatus.fear ~= IgnoreRideSafety.ST_UNSUPPORTED
end

function IgnoreRideSafety.IsEnabled(_sKind)
  local m = IgnoreRideSafety.oManager
  return m ~= nil and m:IsEnabled(_sKind or "fear")
end

function IgnoreRideSafety.GetUnavailableReason()
  if IgnoreRideSafety.sLoadError ~= nil then
    return IgnoreRideSafety.sLoadError
  end
  return "unsupported game version (needs Planet Coaster build 1.13.3.88540); see IgnoreRideSafety.log"
end

-- The options menu only renders localisation symbols, so literal text is passed through an
-- existing parameterised symbol (the same one the game uses to show ride names).
local function UIText(_s)
  return "[PopUp_Rides_RideName:RIDENAME='" .. api.ui.EscapeString(_s) .. "']"
end

-- Adds the mod's checkboxes to Options > Game (in-park settings menu).
-- Implemented by wrapping Windows.GameOptionsMenu methods at runtime; no game files are replaced.
function IgnoreRideSafety.InstallOptionsHook()
  if IgnoreRideSafety.bOptionsHooked then
    return true
  end
  local bOk, GameOptionsMenu = pcall(global.require, "Windows.GameOptionsMenu")
  local bOk2, OptionsMenuGUI = pcall(global.require, "UI.Controllers.OptionsMenuGUI")
  if not bOk or not bOk2 or type(GameOptionsMenu) ~= "table" or type(OptionsMenuGUI) ~= "table" then
    return false
  end
  local fnInit = GameOptionsMenu.Init
  local fnGetItems = GameOptionsMenu.GetItems
  local fnHandleEvent = GameOptionsMenu.HandleEvent
  local fnApplyChanges = GameOptionsMenu.ApplyChanges
  if type(fnInit) ~= "function" or type(fnGetItems) ~= "function" or type(fnHandleEvent) ~= "function" or type(fnApplyChanges) ~= "function" then
    return false
  end
  local tOptions = IgnoreRideSafety.tOptions
  local tOrder = IgnoreRideSafety.tOptionOrder
  GameOptionsMenu.Init = function(self, ...)
    local r = fnInit(self, ...)
    self.tIRSPending = {}
    for _, sKind in ipairs(tOrder) do
      self.tIRSPending[sKind] = IgnoreRideSafety.IsEnabled(sKind)
    end
    self.tIRSPending.experimental = IgnoreRideSafety.IsEnabled("experimental")
    return r
  end
  GameOptionsMenu.GetItems = function(self, _tSettingsMenuItemsData, ...)
    local r = fnGetItems(self, _tSettingsMenuItemsData, ...)
    if IgnoreRideSafety.oManager ~= nil and type(_tSettingsMenuItemsData) == "table" and type(_tSettingsMenuItemsData.items) == "table" then
      local tItems = _tSettingsMenuItemsData.items
      local bAvailable = IgnoreRideSafety.IsAvailable()
      tItems[#tItems + 1] = {
        id = "game.ignoreridesafetyheader",
        label = UIText("Mod: Ignore Ride Safety " .. IgnoreRideSafety.sVersion),
        itemRendererClass = OptionsMenuGUI.LABEL
      }
      for _, sKind in ipairs(tOrder) do
        local tOpt = tOptions[sKind]
        tItems[#tItems + 1] = {
          id = tOpt.sID,
          label = UIText(tOpt.sLabel),
          toolTip = UIText(bAvailable and tOpt.sToolTip or ("Unavailable: " .. IgnoreRideSafety.GetUnavailableReason())),
          itemRendererClass = OptionsMenuGUI.CHECK_BOX,
          toggled = self.tIRSPending ~= nil and self.tIRSPending[sKind] == true,
          enabled = bAvailable
        }
      end
      local m = IgnoreRideSafety.oManager
      if m.tNative ~= nil and m.tNative.bExperimental then
        local tExp = IgnoreRideSafety.tExperimental
        local bExpAvailable = bAvailable and m.tStatus.experimental ~= IgnoreRideSafety.ST_UNSUPPORTED
        tItems[#tItems + 1] = {
          id = tExp.sID,
          label = UIText(tExp.sLabel),
          toolTip = UIText(bExpAvailable and tExp.sToolTip or "Unavailable: experimental code checks failed; see IgnoreRideSafety.log"),
          itemRendererClass = OptionsMenuGUI.CHECK_BOX,
          toggled = self.tIRSPending ~= nil and self.tIRSPending.experimental == true,
          enabled = bExpAvailable
        }
        if m:IsEnabled("experimental") then
          for _, sLine in ipairs(m:GetDiagnosticLines()) do
            tItems[#tItems + 1] = {
              id = "game.irsdiag" .. #tItems,
              label = UIText(sLine),
              itemRendererClass = OptionsMenuGUI.LABEL
            }
          end
        end
      end
    end
    return r
  end
  GameOptionsMenu.HandleEvent = function(self, _sID, _arg, ...)
    for _, sKind in ipairs(tOrder) do
      if _sID == tOptions[sKind].sID then
        self.tIRSPending = self.tIRSPending or {}
        self.tIRSPending[sKind] = _arg == true
        return true
      end
    end
    if _sID == IgnoreRideSafety.tExperimental.sID then
      self.tIRSPending = self.tIRSPending or {}
      self.tIRSPending.experimental = _arg == true
      return true
    end
    if type(_sID) == "string" and string.sub(_sID, 1, 11) == "game.irsdia" then
      return true
    end
    return fnHandleEvent(self, _sID, _arg, ...)
  end
  GameOptionsMenu.ApplyChanges = function(self, ...)
    local r = fnApplyChanges(self, ...)
    local m = IgnoreRideSafety.oManager
    if m ~= nil and self.tIRSPending ~= nil then
      for _, sKind in ipairs(tOrder) do
        local bWant = self.tIRSPending[sKind]
        if bWant ~= nil and bWant ~= m:IsEnabled(sKind) then
          m:SetEnabled(sKind, bWant)
        end
      end
      local bWantExp = self.tIRSPending.experimental
      if bWantExp ~= nil and bWantExp ~= m:IsEnabled("experimental") then
        m:SetExperimental(bWantExp)
      end
    end
    return r
  end
  IgnoreRideSafety.bOptionsHooked = true
  return true
end

-- ACSE hook: add our manager to every park environment.
IgnoreRideSafety.AddParkManagers = function(_fnAdd)
  _fnAdd("Managers.IgnoreRideSafetyManager", {})
end
