#include "catch.hpp"

#include "test_helpers.hpp"

#include <limits.h>
#include <sstream>
#include <unistd.h>
#include <unordered_map>

static void
search_for_pid_and_check (const char * trigger,
			  const std::string & str,
			  std::unordered_map<std::string,
					     std::string> & properties)
{
  bool pidfound = false;
  std::istringstream in{
    str
  };
  std::string line;
  while (getline (in, line))
    {
      // Search for the first occurrence of (q)
      if (line.find (trigger) != std::string::npos)
	{
	  std::string child_pid_str = line.substr (0, line.find (' '));
	  REQUIRE (properties["child"] == child_pid_str);
	  pidfound = true;
	  break;
	}
    }
  REQUIRE (pidfound);
}

TEST_CASE ("Launch 'm' program")
{
  auto [str, retcode] = exec ("bash -c 'cd test && ../runlim ./m' 2>&1");

  auto properties = parse_runlim_prints (str);

  char hostnamedata[4096];
  gethostname (hostnamedata, 4096);

  REQUIRE (retcode == 0);
  REQUIRE (properties["status"] == "ok");
  REQUIRE (properties["host"] == hostnamedata);
  REQUIRE (stoi (properties["result"]) == retcode);
}

TEST_CASE ("Launch 'r' program and kill it after 1s")
{
  auto [str, retcode] =
    exec ("bash -c 'cd test && ../runlim --real-time-limit=1 ./r' 2>&1");

  auto properties = parse_runlim_prints (str);

  int grace_period = 2;

  REQUIRE (retcode == 2);
  REQUIRE (properties["status"] == "out of time");
  REQUIRE (stoi (properties["result"]) == retcode);
  REQUIRE (stoi (properties["real"]) < 1 + grace_period);
  REQUIRE (stoi (properties["samples"]) == 10);
  REQUIRE ((stoi (properties["children"]) == 2 || stoi (properties["children"]) == 3));

  search_for_pid_and_check ("(r)", str, properties);
}

TEST_CASE ("Launch 'r' program and kill it after 0s")
{
  auto [str, retcode] =
    exec ("bash -c 'cd test && ../runlim --real-time-limit=0 ./r' 2>&1");

  auto properties = parse_runlim_prints (str);

  int grace_period = 2;

  REQUIRE (retcode == 2);
  REQUIRE (properties["status"] == "out of time");
  REQUIRE (stoi (properties["result"]) == retcode);
  REQUIRE (stoi (properties["real"]) < grace_period);
  REQUIRE (stoi (properties["samples"]) == 1);
  REQUIRE ((stoi (properties["children"]) == 2 || stoi (properties["children"]) == 3));

  search_for_pid_and_check ("(r)", str, properties);
}

TEST_CASE ("Launch 'q' program and kill it after 0s")
{
  auto [str, retcode] =
    exec ("bash -c 'cd test && ../runlim --real-time-limit=0 ./q' 2>&1");

  auto properties = parse_runlim_prints (str);

  int grace_period = 2;

  REQUIRE (retcode == 2);
  REQUIRE (properties["status"] == "out of time");
  REQUIRE (stoi (properties["result"]) == retcode);
  REQUIRE (stoi (properties["real"]) < grace_period);
  REQUIRE (stoi (properties["samples"]) == 1);
  REQUIRE ((stoi (properties["children"]) == 2 || stoi (properties["children"]) == 3));

  search_for_pid_and_check ("(q)", str, properties);
}
