extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <string.h>
#include <stdio.h>
#include <vector>
#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include <cxxtools/jsondeserializer.h>
#include <cxxtools/directory.h>
#include <bios_proto.h>
#include <malamute.h>

struct value {
  double value;
  time_t valid_till;
};

std::map<std::string, value> cache;

int main (int argc, char** argv) {
  std::string lua_code;

  // Read configuration
  if(argc < 2) {
    printf("Syntax: %s config\n", argv[0]);
  }
  mlm_client_t *client = mlm_client_new();
  mlm_client_connect(client, "ipc://@/malamute", 1000, argv[1]);
  mlm_client_set_producer(client, "METRICS");
  std::ifstream f(argv[1]);
  cxxtools::JsonDeserializer json(f);
  json.deserialize();
  const cxxtools::SerializationInfo *si = json.si();
  si->getMember("evaluation") >>= lua_code;

  // Subscribe to all streams
  for(auto it : si->getMember("in")) {
    std::string buff;
    it >>= buff;
    mlm_client_set_consumer(client, "METRICS", buff.c_str());
    printf("Registered to receive '%s'\n", buff.c_str());
  }


  while(!zsys_interrupted) {
    // Get message
    zmsg_t *msg = mlm_client_recv(client);
    if(msg == NULL)
      continue;
    bios_proto_t *yn = bios_proto_decode(&msg);
    if(yn == NULL)
      continue;

    // Update cache with updated values
    std::string topic = mlm_client_subject(client);
    value val;
    val.value = atof(bios_proto_value(yn));
    val.valid_till = time(NULL) + 600;
    printf("Got message '%s' with value %lf\n", topic.c_str(), val.value);
    auto f = cache.find(topic);
    if(f != cache.end()) {
      f->second = val;
    } else {
      cache.insert(std::make_pair(topic, val));
    }
    bios_proto_destroy(&yn);

    // Prepare data for computation
    lua_State *L = lua_open();
    luaL_openlibs(L);
    lua_newtable(L);
    time_t tme = time(NULL);
    for(auto i : cache) {
      if(tme > i.second.valid_till)
        continue;
      printf(" - %s, %f\n", i.first.c_str(), i.second.value);
      lua_pushstring(L, i.first.c_str());
      lua_pushnumber(L, i.second.value);
      lua_settable(L, -3);
    }
    lua_setglobal(L, "mt");

    // Do the real processing
    auto error = luaL_loadbuffer(L, lua_code.c_str(), lua_code.length(), "line") ||
                 lua_pcall(L, 0, 3, 0);
    if(error) {
      fprintf(stderr, "%s", lua_tostring(L, -1));
      goto next_iter;
    }
    printf("Total: %d\n", lua_gettop(L));
    if(lua_gettop(L) == 3) {
         if(strrchr(lua_tostring(L, -3), '@') == NULL) {
           printf("Invalid output topic\n");
           goto next_iter;
         }
         bios_proto_t *n_met = bios_proto_new(BIOS_PROTO_METRIC);
         char *buff = strdup(lua_tostring(L, -3));
         bios_proto_set_element_src(n_met, "%s", strrchr(buff, '@') + 1);
         (*strrchr(buff, '@')) = 0;
         bios_proto_set_type(n_met, "%s", buff);
         bios_proto_set_value(n_met, "%s", lua_tostring(L, -2));
         bios_proto_set_unit(n_met,  "%s", lua_tostring(L, -1));
         bios_proto_set_time(n_met,  -1);
         zmsg_t* z_met = bios_proto_encode(&n_met);
         mlm_client_send(client, lua_tostring(L, -3), &z_met);
    } else {
       fprintf(stdout, "Not enough valid data...\n");
    }
next_iter:
    lua_close(L);
  }
  mlm_client_destroy(&client);
  return 0;
}
