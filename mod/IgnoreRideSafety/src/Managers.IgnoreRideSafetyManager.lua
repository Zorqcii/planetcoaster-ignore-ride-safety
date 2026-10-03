-----------------------------------------------------------------------
-- Ignore Ride Safety: park manager (added to every park by ACSE).
--  * Two independent options, toggled from Options > Game:
--      fear   - guests ignore "ride too intense"
--      nausea - guests ignore the nausea rating of a ride
--  * Both are switched OFF when a park loads and when it is unloaded.
--  * No per-frame work: options change only when the player applies them.
-----------------------------------------------------------------------
local global = _G
local ipairs = global.ipairs
local require = global.require
local Mutators = require("GameObject.ModuleMutators")
local IRS = require("Database.IgnoreRideSafety")
local IgnoreRideSafetyManager = module(..., Mutators.Manager())

IgnoreRideSafetyManager.Init = function(self, _tProperties, _tEnvironment)
  self.tNative = IRS.LoadNative()
  self.tStatus = {}
  if self.tNative then
    for _, sKind in ipairs(IRS.tOptionOrder) do
      self.tStatus[sKind] = IRS.Call(self.tNative[IRS.tOptions[sKind].sDisable])
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

IgnoreRideSafetyManager.Advance = function(self, _nDeltaTime)
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
  end
  self.tStatus = {}
  if IRS.oManager == self then
    IRS.oManager = nil
  end
end

Mutators.VerifyManagerModule(IgnoreRideSafetyManager)
