--add by freeeyes
--处理rocket_mq传递过来的事件

base_dir = freeswitch.getGlobalVariable("base_dir")
if base_dir == nil then
	base_dir = "/usr/local/freeswitch-1.10.2"
end

package.path = package.path..";/usr/local/freeswitch-1.10.2/share/freeswitch/scripts/?.lua"
require("global_param")
scripts_dir = base_dir .."/share/freeswitch/scripts"
json = dofile(scripts_dir.."/libs/json.lua");
api = freeswitch.API();

function debug(s)
	freeswitch.consoleLog("info", "<REDIS SENTIENL EVENT>".. s .. "\n")
end

function set_redis_message()
	api_string = "set_resis_sentinel_value \"freeeyes\" \"hello\""
	debug("api_string = " .. api_string);
	cause = api:executeString(api_string);
	debug("cause = " .. cause);	
end	

function get_redis_message()
	api_string = "get_resis_sentinel_value \"freeeyes\""
	debug("api_string = " .. api_string);
	cause = api:executeString(api_string);
	debug("cause = " .. cause);	
end

debug("argv[1] value is " .. argv[1]) 

local status, params = pcall(json.decode, argv[1])

if status then 
	debug("name=" .. params["name"])
	if params["name"] == "set" then
		set_redis_message()
	else
		get_redis_message()
	end
else
	debug(" invalid args")
	return "-ERR INVALID ARGS.\n"
end
