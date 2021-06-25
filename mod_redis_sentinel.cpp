#include <switch.h>
#include <unistd.h>
#include <string>

#include <sw/redis++/redis++.h>

using namespace sw::redis;

//处理会议踢人播放一段语音后的问题。
//add by freeeyes

/*
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_example_shutdown);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_example_runtime);
*/

SWITCH_MODULE_LOAD_FUNCTION(mod_redis_sentinel_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_redis_sentinel_shutdown);
SWITCH_MODULE_DEFINITION(mod_redis_sentinel, mod_redis_sentinel_load, mod_redis_sentinel_shutdown,  NULL);

//判断一个命令行里有多少空格
int redis_get_cmd_string_space_count(const char* cmd)
{
	int arg_count = 1;
	int cmd_size = strlen(cmd);
	int i = 0;
	
	for(i = 0; i < cmd_size; i++)
	{
		if(cmd[i] == ' ')
		{
			arg_count++;
		}
	}

	return arg_count;
}

//去掉字符串前后的双引号
void check_string_double_quotes(char* buffer)
{
	if(buffer[0] == '"' && buffer[strlen(buffer) - 1] == '"')
	{
		//前后有双引号，需要去掉
		memmove(buffer, &buffer[1], strlen(buffer) - 2);
		buffer[strlen(buffer) - 2] = '\0';
	}
}

// cmd为参数列表
// sessin为当前callleg的session
// stream为当前输出流。如果想在Freeswitch控制台中输出什么，可以往这个流里写
#define SWITCH_STANDARD_API(name)  \
static switch_status_t name (_In_opt_z_ const char *cmd, _In_opt_ switch_core_session_t *session, _In_ switch_stream_handle_t *stream)

//全局变量
std::shared_ptr<Sentinel> redis_sentinel_ = nullptr;
std::shared_ptr<Redis> master_redis_ = nullptr;
bool redis_is_connect_ = false;

//配置文件信息
class Credis_sentinel_config
{
public:
	std::string redis_master_server_list = "";
	std::string redis_password = "";	
	std::string redis_master_name = "";
	int redis_connect_timeout = 100;

	void print_info()
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[Credis_sentinel_config]redis_master_server_list=%s!\n", redis_master_server_list.c_str());
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[Credis_sentinel_config]produce_ServerAddress=%s!\n", redis_password.c_str());
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[Credis_sentinel_config]produce_topic=%s!\n", redis_master_name.c_str());
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[Credis_sentinel_config]consumer_name=%d!\n", redis_connect_timeout);		
	}
};

Credis_sentinel_config redis_sentinel_config_;

//字符串切割函数
std::vector<std::string> redis_string_split(const std::string& str, const std::string& delim) 
{
	std::vector<std::string> res;
	if("" == str) return res;
	char * strs = new char[str.length() + 1] ;
	strcpy(strs, str.c_str()); 
 
	char * d = new char[delim.length() + 1];
	strcpy(d, delim.c_str());
 
	char *p = strtok(strs, d);
	while(p) {
		std::string s = p;
		res.push_back(s);
		p = strtok(NULL, d);
	}
 
	return res;
}

//拆解redis服务器链接字符串
void parse_redis_server_info(std::string server_info, std::vector<std::pair<std::string, int>>& nodes)
{
	auto server_ip_list = redis_string_split(server_info, ",");
	
	for(auto server_ip : server_ip_list)
	{
		auto server_ip_info = redis_string_split(server_ip, ":");
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[parse_server_info]server_ip=%s, port=%d!\n",  server_ip_info[0].c_str(), atoi(server_ip_info[1].c_str()));	
		nodes.push_back(std::make_pair(server_ip_info[0], atoi( server_ip_info[1].c_str())));
	}
}

//链接redis哨兵服务器
SWITCH_DECLARE(bool) connect_redis_sentinel_server()
{
	SentinelOptions sentinel_opts;

	//解析链接字符串
	std::vector<std::pair<std::string, int>> server_nodes; 
	parse_redis_server_info(redis_sentinel_config_.redis_master_server_list, server_nodes);

	sentinel_opts.nodes = server_nodes;   // Required. List of Redis Sentinel nodes.

	sentinel_opts.connect_timeout = std::chrono::milliseconds(redis_sentinel_config_.redis_connect_timeout);
	sentinel_opts.socket_timeout = std::chrono::milliseconds(redis_sentinel_config_.redis_connect_timeout);

	redis_sentinel_ = std::make_shared<Sentinel>(sentinel_opts);	
		
	ConnectionOptions connection_opts;
	connection_opts.password = redis_sentinel_config_.redis_password;  // Optional. No password by default.
	connection_opts.connect_timeout = std::chrono::milliseconds(redis_sentinel_config_.redis_connect_timeout);   // Required.
	connection_opts.socket_timeout = std::chrono::milliseconds(redis_sentinel_config_.redis_connect_timeout);    // Required.
	connection_opts.keep_alive = true; //保活

	ConnectionPoolOptions pool_opts;
	
	try
	{
		master_redis_ = std::make_shared<Redis>(redis_sentinel_, redis_sentinel_config_.redis_master_name, Role::MASTER, connection_opts, pool_opts);	
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[connect_redis_sentinel_server]connect success.\n");	

		redis_is_connect_ = true;
		return true;		
	}
	catch(sw::redis::Error err)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[connect_redis_sentinel_server]error=%s!\n", err.what());	
		redis_is_connect_ = false;
		return false;
	}
}

//关闭远程链接
void close_redis_sentinel_client()
{
	redis_is_connect_ = false;
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[close_redis_sentinel_client]close refdis ok!\n");
}

//重连redis哨兵服务器
SWITCH_DECLARE(bool) reconnect_redis_sentinel_servre()
{
	close_redis_sentinel_client();
	connect_redis_sentinel_server();
	return true;
}

//读取配置文件
static switch_status_t do_config(Credis_sentinel_config& redis_sentinel_config)
{
	std::string cf = "redis_sentinel.conf";
	switch_xml_t cfg, xml, param;
	switch_xml_t xml_profiles,xml_profile;

	if (!(xml = switch_xml_open_cfg(cf.c_str(), &cfg, NULL))) 
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "[do_config]Open of %s failed\n", cf.c_str());
		return SWITCH_STATUS_FALSE;
	}

    if ((xml_profiles = switch_xml_child(cfg, "profiles"))) 
	{
    	for (xml_profile = switch_xml_child(xml_profiles, "profile"); xml_profile;xml_profile = xml_profile->next) 
		{
            if (!(param = switch_xml_child(xml_profile, "param"))) 
			{
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "[do_config]No param, check the new config!\n");
                continue;
            }

			for (; param; param = param->next) 
			{
				char *var = (char *) switch_xml_attr_soft(param, "name");
				char *val = (char *) switch_xml_attr_soft(param, "value");

				if (!zstr(var) && !zstr(val)  ) 
				{
					if (!strcasecmp(var, "redis_server")) 
					{
						redis_sentinel_config_.redis_master_server_list = val;
					} 
					else if (!strcasecmp(var, "redis_password")) 
					{
						redis_sentinel_config_.redis_password = val;
					} 
					else if (!strcasecmp(var, "redis_master_name")) 
					{
						redis_sentinel_config_.redis_master_name = val;
					} 
					else if (!strcasecmp(var, "redis_connect_timeout")) 
					{
						redis_sentinel_config_.redis_connect_timeout = atoi(val);
					} 								
				}			
			}						
		}
	}	

	switch_xml_free(xml);

	return SWITCH_STATUS_SUCCESS;
}

//向队列里push消息
SWITCH_STANDARD_API(push_resis_sentinel_rpush) 
{
	switch_memory_pool_t *pool;
	char *mycmd = NULL;
	char *argv[2] = {0};
	int argc = 0;

    if (zstr(cmd)) {
        stream->write_function(stream, "[push_resis_sentinel_rpush]parameter missing.\n");
        return SWITCH_STATUS_SUCCESS;
    }

    switch_core_new_memory_pool(&pool);
    mycmd = switch_core_strdup(pool, cmd); 

	argc = redis_get_cmd_string_space_count(mycmd);
    if (argc != 2) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[push_resis_sentinel_rpush]parameter number is invalid, mycmd=%s, count=%d.\n", mycmd, argc);
        return SWITCH_STATUS_SUCCESS;
    }

	argc = switch_split(mycmd, ' ', argv);
	check_string_double_quotes(argv[0]);
	check_string_double_quotes(argv[1]);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[push_resis_sentinel_rpush]topic=%s.\n", argv[0]);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[push_resis_sentinel_rpush]message=%s.\n", argv[1]);	

	if(redis_is_connect_ == false)
	{
		//如果链接不存在，测试重连
		reconnect_redis_sentinel_servre();
	}

	if(redis_is_connect_ == true)
	{
		//链接存在，可以提交
		try
		{
			master_redis_->rpush(argv[0],  argv[1]);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[push_resis_sentinel_rpush]send %s success.\n", argv[0]);
			stream->write_function(stream, "ok");
		}
		catch(sw::redis::Error err)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[push_resis_sentinel_rpush]error=%s!\n", err.what());	
			redis_is_connect_ = false;
			stream->write_function(stream, "fail");
		}	
	}

	return SWITCH_STATUS_SUCCESS;
}

//读取队列的一条消息
SWITCH_STANDARD_API(pop_resis_sentinel_lpop) 
{
	switch_memory_pool_t *pool;
	char *mycmd = NULL;
	char *argv[1] = {0};
	int argc = 0;

    if (zstr(cmd)) {
        stream->write_function(stream, "[pop_resis_sentinel_lpop]parameter missing.\n");
        return SWITCH_STATUS_SUCCESS;
    }

    switch_core_new_memory_pool(&pool);
    mycmd = switch_core_strdup(pool, cmd); 

	argc = redis_get_cmd_string_space_count(mycmd);
    if (argc != 1) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[pop_resis_sentinel_lpop]parameter number is invalid, mycmd=%s, count=%d.\n", mycmd, argc);
        return SWITCH_STATUS_SUCCESS;
    }

	argc = switch_split(mycmd, ' ', argv);
	check_string_double_quotes(argv[0]);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[pop_resis_sentinel_lpop]topic=%s.\n", argv[0]);

	if(redis_is_connect_ == false)
	{
		//如果链接不存在，测试重连
		reconnect_redis_sentinel_servre();
	}

	if(redis_is_connect_ == true)
	{
		//链接存在，可以提交
		try
		{
			auto element = master_redis_->lpop(argv[0]);
			std::string redis_message = *element;
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[push_redis_sentinel_event]pop (%s) success.\n", redis_message.c_str());
			stream->write_function(stream, "%s", redis_message.c_str());
		}
		catch(sw::redis::Error err)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[pop_resis_sentinel_lpop]error=%s!\n", err.what());	
			redis_is_connect_ = false;
			stream->write_function(stream, "");
		}	
	}

	return SWITCH_STATUS_SUCCESS;
}

//读取redis一个key
SWITCH_STANDARD_API(get_resis_sentinel_value) 
{
	switch_memory_pool_t *pool;
	char *mycmd = NULL;
	char *argv[1] = {0};
	int argc = 0;

    if (zstr(cmd)) {
        stream->write_function(stream, "[get_resis_sentinel_value]parameter missing.\n");
        return SWITCH_STATUS_SUCCESS;
    }

    switch_core_new_memory_pool(&pool);
    mycmd = switch_core_strdup(pool, cmd); 

	argc = redis_get_cmd_string_space_count(mycmd);
    if (argc != 1) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[get_resis_sentinel_value]parameter number is invalid, mycmd=%s, count=%d.\n", mycmd, argc);
        return SWITCH_STATUS_SUCCESS;
    }

	argc = switch_split(mycmd, ' ', argv);
	check_string_double_quotes(argv[0]);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[get_resis_sentinel_value]topic=%s.\n", argv[0]);

	if(redis_is_connect_ == false)
	{
		//如果链接不存在，测试重连
		reconnect_redis_sentinel_servre();
	}

	if(redis_is_connect_ == true)
	{
		//链接存在，可以提交
		try
		{
			auto element = master_redis_->get(argv[0]);
			std::string redis_message = *element;
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[get_resis_sentinel_value]get (%s) success.\n", redis_message.c_str());
			stream->write_function(stream, "%s", redis_message.c_str());
		}
		catch(sw::redis::Error err)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[get_resis_sentinel_value]error=%s!\n", err.what());	
			redis_is_connect_ = false;
			stream->write_function(stream, "");
		}	
	}

	return SWITCH_STATUS_SUCCESS;
}

//读取redis一个key
SWITCH_STANDARD_API(set_resis_sentinel_value) 
{
	switch_memory_pool_t *pool;
	char *mycmd = NULL;
	char *argv[2] = {0};
	int argc = 0;

    if (zstr(cmd)) {
        stream->write_function(stream, "[set_resis_sentinel_value]parameter missing.\n");
        return SWITCH_STATUS_SUCCESS;
    }

    switch_core_new_memory_pool(&pool);
    mycmd = switch_core_strdup(pool, cmd); 

	argc = redis_get_cmd_string_space_count(mycmd);
    if (argc != 2) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[set_resis_sentinel_value]parameter number is invalid, mycmd=%s, count=%d.\n", mycmd, argc);
        return SWITCH_STATUS_SUCCESS;
    }

	argc = switch_split(mycmd, ' ', argv);
	check_string_double_quotes(argv[0]);
	check_string_double_quotes(argv[1]);	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[set_resis_sentinel_value]topic=%s.\n", argv[0]);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[set_resis_sentinel_value]value=%s.\n", argv[1]);

	if(redis_is_connect_ == false)
	{
		//如果链接不存在，测试重连
		reconnect_redis_sentinel_servre();
	}

	if(redis_is_connect_ == true)
	{
		//链接存在，可以提交
		try
		{
			master_redis_->set(argv[0], argv[1]);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[get_resis_sentinel_value]set (%s) success.\n", argv[0]);
			stream->write_function(stream, "ok");
		}
		catch(sw::redis::Error err)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[get_resis_sentinel_value]error=%s!\n", err.what());	
			redis_is_connect_ = false;
			stream->write_function(stream, "false");
		}	
	}

	return SWITCH_STATUS_SUCCESS;
}


SWITCH_MODULE_LOAD_FUNCTION(mod_redis_sentinel_load)
{
	switch_api_interface_t* commands_api_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_API(commands_api_interface, "push_resis_sentinel_rpush", "lpush redis message", push_resis_sentinel_rpush, "<key> <message>");
	SWITCH_ADD_API(commands_api_interface, "pop_resis_sentinel_lpop", "lpop redis message", pop_resis_sentinel_lpop, "<key>");
	SWITCH_ADD_API(commands_api_interface, "get_resis_sentinel_value", "get redis message", get_resis_sentinel_value, "<key>");
	SWITCH_ADD_API(commands_api_interface, "set_resis_sentinel_value", "set redis message", set_resis_sentinel_value, "<key> <value>");

  	/* 读取配置文件 */
	switch_status_t do_config_return = do_config(redis_sentinel_config_);
	if(SWITCH_STATUS_SUCCESS == do_config_return)
	{
		redis_sentinel_config_.print_info();

		//链接redis哨兵服务器
		redis_is_connect_ = false;
		connect_redis_sentinel_server();		
	}
	else
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[mod_redis_sentinel_load]do_config is fail!\n");
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[mod_redis_sentinel_load]load redis success!\n");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/* Called when the system shuts down */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_redis_sentinel_shutdown)
{
	close_redis_sentinel_client();
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[mod_redis_sentinel_shutdown]unload redis ok!\n");
	return SWITCH_STATUS_SUCCESS;
}

/*
  If it exists, this is called in it's own thread when the module-load completes
  If it returns anything but SWITCH_STATUS_TERM it will be called again automaticly
SWITCH_MODULE_RUNTIME_FUNCTION(mod_example_runtime);
{
	while(looping)
	{
		switch_yield(1000);
	}
	return SWITCH_STATUS_TERM;
}
*/
