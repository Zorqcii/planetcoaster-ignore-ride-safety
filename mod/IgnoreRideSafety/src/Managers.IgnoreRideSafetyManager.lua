-----------------------------------------------------------------------
-- Ignore Ride Safety: park manager (added to every park by ACSE).
--  * Two independent options, toggled from Options > Game:
--      fear   - guests ignore "ride too intense"
--      nausea - guests ignore the nausea rating of a ride
--  * EXPERIMENTAL option (stage 2): untested/unfinished rides may be opened;
--    the list of OPEN untested rides is sent to the native helper, which
--    gives guests assumed ratings for exactly those rides. Off by default.
--  * Everything is switched OFF when a park loads and when it is unloaded.
--  * Per-frame work only while the experimental option is on.
-----------------------------------------------------------------------
local global = _G
local api = global.api
local ipairs = global.ipairs
local pairs = global.pairs
local type = global.type
local tostring = global.tostring
local pcall = global.pcall
local math = global.math
local string = global.string
local require = global.require
local Mutators = require("GameObject.ModuleMutators")
local IRS = require("Database.IgnoreRideSafety")
local IgnoreRideSafetyManager = module(..., Mutators.Manager())

local c_nExperimentalInterval = 3.0

local function S(...)
  return tostring((...))
end

IgnoreRideSafetyManager.Init = function(self, _tProperties, _tEnvironment)
  self.tNative = IRS.LoadNative()
  self.tStatus = {}
  self.nTimer = 0
  self.tDiag = {}
  self.bGuestIdMatch = nil
  if self.tNative then
    for _, sKind in ipairs(IRS.tOptionOrder) do
      self.tStatus[sKind] = IRS.Call(self.tNative[IRS.tOptions[sKind].sDisable])
    end
    if self.tNative.bExperimental then
      self.tStatus.experimental = IRS.Call(self.tNative[IRS.tExperimental.sDisable])
    end
  end
  IRS.oManager = self
  IRS.InstallOptionsHook()
end

IgnoreRideSafetyManager.IsEnabled = function(self, _sKind)
  return self.tStatus[_sKind or "fear"] == IRS.ST_ON
end

IgnoreRideSafetyManager.SetEnabled = function(self, _sKind, _bEnabled)
  local tOpt = IRS.tOptions[_sKind]
  if not self.tNative or tOpt == nil then
    return false
  end
  self.tStatus[_sKind] = IRS.Call(self.tNative[_bEnabled and tOpt.sEnable or tOpt.sDisable])
  return self.tStatus[_sKind] == (_bEnabled and IRS.ST_ON or IRS.ST_OFF)
end

-- EXPERIMENTAL ----------------------------------------------------------

IgnoreRideSafetyManager.SetExperimental = function(self, _bEnabled)
  if not self.tNative or not self.tNative.bExperimental then
    return false
  end
  local tExp = IRS.tExperimental
  self.tStatus.experimental = IRS.Call(self.tNative[_bEnabled and tExp.sEnable or tExp.sDisable])
  pcall(self.RefreshAttractionFlags, self)
  self.nTimer = c_nExperimentalInterval
  return self.tStatus.experimental == (_bEnabled and IRS.ST_ON or IRS.ST_OFF)
end

-- The game caches each attraction's "can be opened" result and only recomputes it when one of its
-- requirements changes. Adding and immediately removing a no-testing reason forces the recompute.
-- Stations that are mid-test are skipped, because a no-testing reason would stop the test.
IgnoreRideSafetyManager.RefreshAttractionFlags = function(self)
  local tW = api.world.GetWorldAPIs()
  local tTokens = tW.ridestation:GetAllRideStationEditTokens()
  for i = 1, #tTokens do
    local st = tW.ridestation:GetRideStationEntityIDFromEditToken(tTokens[i])
    if st ~= nil and st ~= api.entity.NullEntityID and not tW.attractions:IsTesting(st) then
      tW.attractions:AddNoTestingReason(st)
      tW.attractions:RemoveNoTestingReason(st)
    end
  end
end

local function ToInteger(_n)
  if type(_n) ~= "number" then
    return nil
  end
  local n = math.tointeger(_n)
  if n == nil and _n == math.floor(_n) and _n >= 0 and _n < 2 ^ 53 then
    n = math.tointeger(math.floor(_n))
  end
  return n
end

-- Sends station ids to the native helper: begin, bits MSB first, push per id, commit.
IgnoreRideSafetyManager.SendIds = function(self, _tIds)
  local tN = self.tNative
  IRS.Call(tN.irs_exp_begin)
  for _, nId in ipairs(_tIds) do
    local nHigh = 63
    while nHigh > 0 and ((nId >> nHigh) & 1) == 0 do
      nHigh = nHigh - 1
    end
    for b = nHigh, 0, -1 do
      if ((nId >> b) & 1) == 1 then
        IRS.Call(tN.irs_exp_bit1)
      else
        IRS.Call(tN.irs_exp_bit0)
      end
    end
    IRS.Call(tN.irs_exp_push)
  end
  IRS.Call(tN.irs_exp_commit)
end

local function CountGuestsOnRide(tW, rideID)
  local bOk, tGuests = pcall(tW.rides.GetGuestsOnRide, tW.rides, rideID)
  local n = 0
  if bOk and type(tGuests) == "table" then
    for _ in pairs(tGuests) do
      n = n + 1
    end
  end
  return n
end

-- Collects the untested tracked rides, sends them to the helper and builds the diagnostic lines.
IgnoreRideSafetyManager.UpdateExperimental = function(self)
  local tW = api.world.GetWorldAPIs()
  local guestsAPI = tW.guests
  local nUntestedThought = guestsAPI.GuestThoughtType_Assessment_RideUntested
  local tTokens = tW.ridestation:GetAllRideStationEditTokens()
  local tIds = {}
  local tLines = {}
  for i = 1, #tTokens do
    local st = tW.ridestation:GetRideStationEntityIDFromEditToken(tTokens[i])
    if st ~= nil and st ~= api.entity.NullEntityID and tW.ridestation:IsTrackedRide(st) then
      local rideID = tW.rides:GetRideForStation(st)
      local bTested = tW.rides:IsTested(rideID) == true
      local nId = ToInteger(st)
      if not bTested and nId ~= nil and tW.attractions:IsOpen(st) then
        tIds[#tIds + 1] = nId
      end
      local sState = tW.attractions:IsOpen(st) and "open" or (tW.attractions:IsTesting(st) and "testing" or "closed")
      local nFree = "?"
      local th = api.track.GetTrackHolder(rideID)
      if th then
        local tFree = api.track.GetFreeEnds(th)
        nFree = 0
        for _ in pairs(tFree or {}) do
          nFree = nFree + 1
        end
      end
      local tLast = tW.ridestats:GetStationStats(st).last or {}
      local nUntested = 0
      for _, t in ipairs(guestsAPI:GetGlobalThoughtSummary(40, st) or {}) do
        if t.nType == nUntestedThought then
          nUntested = nUntested + t.nCount
        end
      end
      tLines[#tLines + 1] = S(api.ui.GetEntityName(rideID)) .. " (station " .. S(nId) .. "): " ..
        (bTested and "tested" or "UNTESTED") .. ", " .. (nFree == 0 and "track complete" or ("track open ends " .. S(nFree))) ..
        ", can open " .. S(tW.attractions:CanBeOpened(st)) .. ", " .. sState ..
        ", riders " .. S(CountGuestsOnRide(tW, rideID)) .. ", queue " .. S(tLast.QueueLength or 0) ..
        ", departures " .. S(tLast.LifetimeDepartures or "-") .. ", not-until-tested thoughts " .. S(nUntested)
    end
  end
  self:SendIds(tIds)
  local nReport = IRS.Call(self.tNative.irs_exp_report)
  if nReport == 1 then
    self.bGuestIdMatch = true
  elseif self.bGuestIdMatch == nil and nReport == 2 then
    self.bGuestIdMatch = false
  end
  tLines[#tLines + 1] = "Open untested rides given assumed ratings: " .. #tIds .. ". Guest code reached one of them: " ..
    (self.bGuestIdMatch == true and "YES" or (self.bGuestIdMatch == false and "not yet" or "-"))
  self.tDiag = tLines
end

IgnoreRideSafetyManager.GetDiagnosticLines = function(self)
  local tLines = {"Experimental diagnostics (snapshot when this menu opened):"}
  for _, s in ipairs(self.tDiag) do
    tLines[#tLines + 1] = s
  end
  return tLines
end

IgnoreRideSafetyManager.Advance = function(self, _nDeltaTime)
  if self.tStatus.experimental ~= IRS.ST_ON then
    return
  end
  self.nTimer = self.nTimer + (api.time.GetDeltaTimeUnscaled() or 0)
  if self.nTimer >= c_nExperimentalInterval then
    self.nTimer = 0
    pcall(self.UpdateExperimental, self)
  end
end

IgnoreRideSafetyManager.Activate = function(self)
end

IgnoreRideSafetyManager.Deactivate = function(self)
end

IgnoreRideSafetyManager.Shutdown = function(self)
  if self.tNative then
    for _, sKind in ipairs(IRS.tOptionOrder) do
      IRS.Call(self.tNative[IRS.tOptions[sKind].sDisable])
    end
    if self.tNative.bExperimental then
      IRS.Call(self.tNative[IRS.tExperimental.sDisable])
    end
  end
  self.tStatus = {}
  if IRS.oManager == self then
    IRS.oManager = nil
  end
end

Mutators.VerifyManagerModule(IgnoreRideSafetyManager)
