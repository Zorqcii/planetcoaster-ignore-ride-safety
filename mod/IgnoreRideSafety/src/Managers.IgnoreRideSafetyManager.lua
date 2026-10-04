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
local table = global.table
local require = global.require
local Mutators = require("GameObject.ModuleMutators")
local IRS = require("Database.IgnoreRideSafety")
local IgnoreRideSafetyManager = module(..., Mutators.Manager())

local c_nExperimentalInterval = 3.0
local ToInteger                -- defined below
local function ToIntegerOrZero(_n)
  return ToInteger(_n) or 0
end
local c_nProtoInterval = 0.25
-- group behaviour names (GetGroupDecisionState().sBehaviour) -> codes used in the log
local c_tBehaviourCode = {Physics = 1, Trapped = 2, Navigating = 3, Idle = 4, Lost = 5, OnRide = 6, Queueing = 7, AtSecurityGuard = 8}
-- behaviours in which a rider is NOT launched
local c_tBusyBehaviour = {[0] = true, [1] = true, [2] = true, [6] = true, [7] = true, [8] = true, [9] = true}

-- Crash-related game messages observed (log only) while the experimental option is on.
local c_tCrashMessages = {
  "MsgType_GuestPhysicsIncidentEndedMessage",
  "MsgType_GuestEnteredSoSFromCrashMessage",
  "MsgType_GroupPhysicsRecoveryMessage",
  "MsgType_GuestHiddenMessage"
}

local function S(...)
  return tostring((...))
end

IgnoreRideSafetyManager.Init = function(self, _tProperties, _tEnvironment)
  self.tNative = IRS.LoadNative()
  self.tStatus = {}
  self.nTimer = 0
  self.tDiag = {}
  self.bGuestIdMatch = nil
  self.tCrashObs = {}
  self.tCrashHandlers = {}
  self.nClock = 0
  self.tRiderSnapshot = {tIds = {}, tRideOf = {}}
  self:ResetProto()
  if self.tNative then
    for _, sKind in ipairs(IRS.tOptionOrder) do
      self.tStatus[sKind] = IRS.Call(self.tNative[IRS.tOptions[sKind].sDisable])
    end
    if self.tNative.bPrototype then
      self.tStatus.prototype = IRS.Call(self.tNative[IRS.tPrototype.sDisable])
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
  if self.tStatus.experimental == IRS.ST_ON then
    pcall(self.StartCrashObserver, self)
  else
    pcall(self.StopCrashObserver, self)
  end
  self.nTimer = c_nExperimentalInterval
  if self.tNative.bPrototype then
    -- the helper switches the prototype off together with the experimental code
    self.tStatus.prototype = IRS.Call(self.tNative[IRS.tPrototype.sStatus])
  end
  return self.tStatus.experimental == (_bEnabled and IRS.ST_ON or IRS.ST_OFF)
end

-- PROTOTYPE ---------------------------------------------------------------

IgnoreRideSafetyManager.ResetProto = function(self)
  self.tProto = {sState = "idle", nTimer = 0, nCrash = 0, nLaunches = 0, tCandidates = {}, tNoted = {}}
end

IgnoreRideSafetyManager.SetPrototype = function(self, _bEnabled)
  if not self.tNative or not self.tNative.bPrototype then
    return false
  end
  local tPt = IRS.tPrototype
  self.tStatus.prototype = IRS.Call(self.tNative[_bEnabled and tPt.sEnable or tPt.sDisable])
  self:ResetProto()
  return self.tStatus.prototype == (_bEnabled and IRS.ST_ON or IRS.ST_OFF)
end

-- Sends a list of non-negative integers through the channel (begin, bits MSB first, push).
IgnoreRideSafetyManager.SendValues = function(self, _tValues)
  local tN = self.tNative
  IRS.Call(tN.irs_exp_begin)
  for _, nId in ipairs(_tValues) do
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
end

IgnoreRideSafetyManager.ProtoNote = function(self, _tValues)
  self:SendValues(_tValues)
  IRS.Call(self.tNative.irs_pt_note)
end

local function GroupBehaviour(tW, nGroup)
  local bOk, t = pcall(tW.guests.GetGroupDecisionState, tW.guests, nGroup)
  if not bOk or type(t) ~= "table" then
    return 0, nil
  end
  return c_tBehaviourCode[t.sBehaviour] or 9, t
end

local function ListContains(t, v)
  if type(t) ~= "table" then
    return false
  end
  for _, x in pairs(t) do
    if x == v then
      return true
    end
  end
  return false
end

-- Returns reason code (0 eligible, 1 not found, 2 not in group, 3 still on ride, 4 group busy, 5 leaving park), behaviour code.
IgnoreRideSafetyManager.CheckRider = function(self, tW, nGuest)
  local bOk, nGroup = pcall(tW.guests.GetGuestGroupID, tW.guests, nGuest)
  if not bOk or nGroup == nil then
    return 1, 0
  end
  local bOk2, tMembers = pcall(tW.guests.GetGuestsInGroup, tW.guests, nGroup)
  if not bOk2 or not ListContains(tMembers, nGuest) then
    return 2, 0
  end
  local rideID = self.tRiderSnapshot.tRideOf[nGuest]
  if rideID ~= nil then
    local bOk3, tOnRide = pcall(tW.rides.GetGuestsOnRide, tW.rides, rideID)
    if not bOk3 or ListContains(tOnRide, nGuest) then
      return 3, 0
    end
  end
  local nBeh, tDecision = GroupBehaviour(tW, nGroup)
  if c_tBusyBehaviour[nBeh] then
    return 4, nBeh
  end
  if tDecision.sBehaviour == "Navigating" and tDecision.viaTargetRootEntity == nil then
    return 5, nBeh
  end
  return 0, nBeh
end

-- Runs every 0.25 s while the prototype option is on: one launch at most per crash.
IgnoreRideSafetyManager.ProtoTick = function(self)
  local tN = self.tNative
  local p = self.tProto
  local tW = api.world.GetWorldAPIs()
  local now = self.nClock
  local nRes = IRS.Call(tN.irs_pt_result)
  if p.sState == "armed" then
    if nRes == 1 then
      p.sState = "monitor"
      p.nLaunches = p.nLaunches + 1
      p.nMonitorUntil = now + 180
      p.nNextCheck = now
      p.nLastBeh = nil
      p.bGone = false
      local bOk, n = pcall(tW.guests.GetTrappedGuestCount, tW.guests)
      p.nTrapped = bOk and n or nil
    elseif nRes == 3 or nRes == 4 then
      p.sState = "select"
    end
  end
  if IRS.Call(tN.irs_pt_poll) == 1 then
    p.nCrash = p.nCrash + 1
    if p.sState == "monitor" or p.sState == "armed" then
      self:ProtoNote({9, p.nCrash})
    else
      p.sState = "select"
      p.nSelectAfter = now + 0.5
      p.nDeadline = now + 10
      p.nAttempts = 0
      p.tNoted = {}
      p.nNoted = 0
      p.tCandidates = {}
      for i, n in ipairs(self.tRiderSnapshot.tIds) do
        p.tCandidates[i] = n
      end
      self:ProtoNote({7, p.nCrash, #p.tCandidates})
    end
  end
  if p.sState == "select" and now >= p.nSelectAfter then
    if now > p.nDeadline or p.nAttempts >= 3 then
      self:ProtoNote({6, p.nCrash})
      p.sState = "idle"
    else
      for _, nGuest in ipairs(p.tCandidates) do
        local nReason, nBeh = self:CheckRider(tW, nGuest)
        if nReason == 0 then
          self:ProtoNote({1, nGuest, 0, nBeh})
          self:SendValues({nGuest})
          IRS.Call(tN.irs_pt_arm)
          p.sState = "armed"
          p.nArmed = nGuest
          p.nAttempts = p.nAttempts + 1
          for i, n in ipairs(p.tCandidates) do
            if n == nGuest then
              table.remove(p.tCandidates, i)
              break
            end
          end
          break
        elseif p.tNoted[nGuest] ~= nReason * 16 + nBeh and p.nNoted < 64 then
          p.tNoted[nGuest] = nReason * 16 + nBeh
          p.nNoted = p.nNoted + 1
          self:ProtoNote({1, nGuest, nReason, nBeh})
        end
      end
    end
  end
  if p.sState == "monitor" and now >= p.nNextCheck then
    p.nNextCheck = now + 0.5
    local bOk, nGroup = pcall(tW.guests.GetGuestGroupID, tW.guests, p.nArmed)
    if not bOk or nGroup == nil then
      if not p.bGone then
        p.bGone = true
        self:ProtoNote({4})
      end
    else
      if p.bGone then
        p.bGone = false
        self:ProtoNote({8})
      end
      local nBeh = GroupBehaviour(tW, nGroup)
      if nBeh ~= p.nLastBeh then
        p.nLastBeh = nBeh
        self:ProtoNote({2, nBeh})
      end
    end
    local bOk2, nTrapped = pcall(tW.guests.GetTrappedGuestCount, tW.guests)
    if bOk2 and nTrapped ~= p.nTrapped then
      p.nTrapped = nTrapped
      self:ProtoNote({5, ToIntegerOrZero(nTrapped)})
    end
    if now > p.nMonitorUntil then
      p.sState = "idle"
    end
  end
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

ToInteger = function(_n)
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

-- DIAGNOSTIC: guest ids of riders (GetGuestsOnRide returns an array of native 64-bit ids).
local function RiderIds(tW, rideID, tOut)
  local bOk, tGuests = pcall(tW.rides.GetGuestsOnRide, tW.rides, rideID)
  local nFound = 0
  if bOk and type(tGuests) == "table" then
    for _, v in pairs(tGuests) do
      nFound = nFound + 1
      local n = ToInteger(v)
      if n ~= nil and #tOut < 127 then
        tOut[#tOut + 1] = n
      end
    end
  end
  return nFound
end

-- DIAGNOSTIC: sends rider ids over the same channel; the first value is a marker carrying the
-- number of riders the script found (bit 62 set), so the log shows ids that could not be sent.
IgnoreRideSafetyManager.SendRiders = function(self, _tIds, _nFound)
  local tN = self.tNative
  if not tN.bDiagnostic then
    return
  end
  local tAll = {(1 << 62) | _nFound}
  for _, n in ipairs(_tIds) do
    tAll[#tAll + 1] = n
  end
  IRS.Call(tN.irs_exp_begin)
  for _, nId in ipairs(tAll) do
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
  IRS.Call(tN.irs_dx_riders_commit)
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
  local tRiders = {}
  local tRideOfRider = {}
  local nRidersFound = 0
  local tLines = {}
  for i = 1, #tTokens do
    local st = tW.ridestation:GetRideStationEntityIDFromEditToken(tTokens[i])
    if st ~= nil and st ~= api.entity.NullEntityID and tW.ridestation:IsTrackedRide(st) then
      local rideID = tW.rides:GetRideForStation(st)
      local bTested = tW.rides:IsTested(rideID) == true
      local nId = ToInteger(st)
      if not bTested and nId ~= nil and tW.attractions:IsOpen(st) then
        tIds[#tIds + 1] = nId
        -- the crash handler identifies the ride (not the station), so send the ride id too
        local nRideId = ToInteger(rideID)
        if nRideId ~= nil and nRideId ~= nId then
          tIds[#tIds + 1] = nRideId
        end
        local nBefore = #tRiders
        nRidersFound = nRidersFound + RiderIds(tW, rideID, tRiders)
        for k = nBefore + 1, #tRiders do
          tRideOfRider[tRiders[k]] = rideID
        end
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
  self:SendRiders(tRiders, nRidersFound)
  if #tRiders > 0 then
    -- the prototype picks its rider from the last list taken while riders were aboard
    self.tRiderSnapshot = {tIds = tRiders, tRideOf = tRideOfRider}
  end
  local nReport = IRS.Call(self.tNative.irs_exp_report)
  if self.tNative.bDiagnostic then
    IRS.Call(self.tNative.irs_dx_report)
  end
  if nReport == 1 then
    self.bGuestIdMatch = true
  elseif self.bGuestIdMatch == nil and nReport == 2 then
    self.bGuestIdMatch = false
  end
  tLines[#tLines + 1] = "DIAGNOSTIC: " .. (self.tNative.bDiagnostic and "observation hooks active while this option is on; results in IgnoreRideSafety.log" or "helper has no diagnostics") ..
    ". Riders on open untested rides: " .. nRidersFound .. " (ids sent " .. #tRiders .. ")"
  tLines[#tLines + 1] = "Open untested rides given assumed ratings: " .. #tIds .. ". Guest code reached one of them: " ..
    (self.bGuestIdMatch == true and "YES" or (self.bGuestIdMatch == false and "not yet" or "-"))
  self.tDiag = tLines
end

-- Observe-only: count crash-related messages and keep the fields of the first one of each kind.
local function DescribeMessage(t)
  local s = ""
  if type(t) == "table" then
    for k, v in pairs(t) do
      s = s .. " " .. S(k) .. "=" .. S(v)
    end
  else
    s = " " .. S(t)
  end
  return s
end

IgnoreRideSafetyManager.StartCrashObserver = function(self)
  self:StopCrashObserver()
  for _, sName in ipairs(c_tCrashMessages) do
    local nType = api.messaging[sName]
    if nType ~= nil then
      local fn = function(_tMessages)
        local o = self.tCrashObs[sName] or {nCount = 0}
        for _, tMsg in ipairs(_tMessages or {}) do
          o.nCount = o.nCount + 1
          if sName == "MsgType_GuestPhysicsIncidentEndedMessage" and self.tStatus.prototype == IRS.ST_ON and type(tMsg) == "table" then
            pcall(self.ProtoNote, self, {3, ToIntegerOrZero(tMsg.nGuestsInvolved)})
          end
          if o.sFirst == nil then
            o.sFirst = DescribeMessage(tMsg)
          end
        end
        self.tCrashObs[sName] = o
      end
      api.messaging.RegisterReceiver(nType, fn)
      self.tCrashHandlers[nType] = fn
    else
      self.tCrashObs[sName] = {nCount = 0, sFirst = "(message type not present in this game)"}
    end
  end
end

IgnoreRideSafetyManager.StopCrashObserver = function(self)
  for nType, fn in pairs(self.tCrashHandlers or {}) do
    api.messaging.UnregisterReceiver(nType, fn)
  end
  self.tCrashHandlers = {}
end

IgnoreRideSafetyManager.GetCrashLines = function(self)
  local tLines = {}
  local tW = api.world.GetWorldAPIs()
  local bOk, nTrapped = pcall(tW.guests.GetTrappedGuestCount, tW.guests)
  tLines[#tLines + 1] = "Crash observer: trapped (SOS) guests now " .. S(bOk and nTrapped or "?")
  for _, sName in ipairs(c_tCrashMessages) do
    local o = self.tCrashObs[sName]
    local sShort = string.gsub(string.gsub(sName, "^MsgType_", ""), "Message$", "")
    tLines[#tLines + 1] = sShort .. ": " .. S(o and o.nCount or 0) .. ((o and o.sFirst) and (" | first:" .. string.sub(o.sFirst, 1, 160)) or "")
  end
  return tLines
end

IgnoreRideSafetyManager.GetDiagnosticLines = function(self)
  local tLines = {"Experimental diagnostics (snapshot when this menu opened):"}
  if self.tNative and self.tNative.bPrototype then
    local p = self.tProto
    tLines[#tLines + 1] = "PROTOTYPE: " .. (self.tStatus.prototype == IRS.ST_ON and "ON" or "off") .. ", state " .. S(p.sState) ..
      ", crashes seen " .. S(p.nCrash) .. ", launches " .. S(p.nLaunches)
  end
  for _, s in ipairs(self.tDiag) do
    tLines[#tLines + 1] = s
  end
  local bOk, tCrash = pcall(self.GetCrashLines, self)
  if bOk then
    for _, s in ipairs(tCrash) do
      tLines[#tLines + 1] = s
    end
  end
  return tLines
end

IgnoreRideSafetyManager.Advance = function(self, _nDeltaTime)
  if self.tStatus.experimental ~= IRS.ST_ON then
    return
  end
  local nDt = api.time.GetDeltaTimeUnscaled() or 0
  self.nClock = self.nClock + nDt
  self.nTimer = self.nTimer + nDt
  if self.nTimer >= c_nExperimentalInterval then
    self.nTimer = 0
    pcall(self.UpdateExperimental, self)
  end
  if self.tStatus.prototype == IRS.ST_ON then
    self.tProto.nTimer = self.tProto.nTimer + nDt
    if self.tProto.nTimer >= c_nProtoInterval then
      self.tProto.nTimer = 0
      pcall(self.ProtoTick, self)
    end
  end
end

IgnoreRideSafetyManager.Activate = function(self)
end

IgnoreRideSafetyManager.Deactivate = function(self)
end

IgnoreRideSafetyManager.Shutdown = function(self)
  pcall(self.StopCrashObserver, self)
  if self.tNative then
    for _, sKind in ipairs(IRS.tOptionOrder) do
      IRS.Call(self.tNative[IRS.tOptions[sKind].sDisable])
    end
    if self.tNative.bPrototype then
      IRS.Call(self.tNative[IRS.tPrototype.sDisable])
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
