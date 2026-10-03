-----------------------------------------------------------------------
-- Ignore Ride Safety: content-pack entry point. The game requires
-- "Database.<PackName>LuaDatabase" for every loaded content pack.
-----------------------------------------------------------------------
local global = _G
local table = global.table
local require = global.require
local IgnoreRideSafetyLuaDatabase = module(...)

IgnoreRideSafetyLuaDatabase.AddContentToCall = function(_tContentToCall)
  table.insert(_tContentToCall, require("Database.IgnoreRideSafety"))
end
